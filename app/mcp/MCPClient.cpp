#include "MCPClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QTimer>
#include <QDateTime>
#include <QCoreApplication>
#include <QDebug>
#include <cmath>

namespace ModelFit {

using enum MCPClientError;

MCPClient::MCPClient(QObject *parent) : QObject(parent) {
    qRegisterMetaType<MCPClientError>("ModelFit::MCPClientError");
    qRegisterMetaType<MCPErrorInfo>("ModelFit::MCPErrorInfo");

    connect(&m_proc, &QProcess::readyReadStandardOutput, this,
            &MCPClient::handleStdioStdout);
    connect(&m_proc, &QProcess::readyReadStandardError, this,
            &MCPClient::handleStdioStderr);
    connect(&m_proc, &QProcess::started, this, &MCPClient::onProcessStarted);
    connect(&m_proc,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MCPClient::onProcessFinished);
    connect(&m_proc,
            QOverload<QProcess::ProcessError>::of(&QProcess::errorOccurred),
            this, &MCPClient::onProcessError);

    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this,
            &MCPClient::retryConnect);
}

MCPClient::~MCPClient() {
    m_autoReconnect = false;
    m_intentionalDisconnect = true;
    m_reconnectTimer.stop();
    disconnect();
}

QString MCPClient::transportName() const {
    switch (m_transport) {
    case MCPTransport::Stdio:
        return "stdio";
    case MCPTransport::StreamableHttp:
        return "streamable-http";
    default:
        return "none";
    }
}

// ---------------------------------------------------------------------------
// Transports
// ---------------------------------------------------------------------------

bool MCPClient::connectStdio(const QString &command, const QStringList &args,
                             const QString &workingDir) {
    m_stdioCommand = command;
    m_stdioArgs = args;
    m_stdioWorkingDir = workingDir;
    m_transport = MCPTransport::Stdio;
    m_reconnectTransport = MCPTransport::Stdio;
    m_httpUrl.clear();
    m_httpSessionId.clear();
    m_intentionalDisconnect = false;

    return startStdio();
}

bool MCPClient::connectHttp(const QString &url, const QString &bearerToken,
                            const QVariantMap &customHeaders) {
    m_httpUrl = url;
    m_httpBearer = bearerToken;
    m_httpCustomHeaders = customHeaders;
    m_httpSessionId.clear();
    m_stdioCommand.clear();
    m_transport = MCPTransport::StreamableHttp;
    m_reconnectTransport = MCPTransport::StreamableHttp;
    m_intentionalDisconnect = false;

    startHttp();
    return true;
}

void MCPClient::setHttpCustomHeaders(const QVariantMap &headers) {
    m_httpCustomHeaders = headers;
}

void MCPClient::disconnect() {
    m_intentionalDisconnect = true;
    m_reconnectTimer.stop();

    if (m_sseReply) {
        m_sseReply->abort();
        m_sseReply->deleteLater();
        m_sseReply = nullptr;
    }
    for (QNetworkReply *reply : std::as_const(m_httpReplies)) {
        reply->abort();
        reply->deleteLater();
    }
    m_httpReplies.clear();

    if (m_proc.state() != QProcess::NotRunning) {
        m_proc.terminate();
        if (!m_proc.waitForFinished(3000))
            m_proc.kill();
    }

    if (m_connected) {
        m_connected = false;
        m_handshakeDone = false;
        m_transport = MCPTransport::None;
        m_reconnectTransport = MCPTransport::None;
        emit disconnected();
    }
}

void MCPClient::reconnect() {
    m_intentionalDisconnect = false;
    m_reconnectAttempts = 0;
    m_reconnectDelay = 1000;
    if (m_transport == MCPTransport::None)
        m_transport = m_reconnectTransport;
    if (m_transport == MCPTransport::Stdio)
        startStdio();
    else if (m_transport == MCPTransport::StreamableHttp)
        startHttp();
}

void MCPClient::setAutoReconnect(bool enabled) { m_autoReconnect = enabled; }

void MCPClient::setMaxReconnectDelay(int ms) {
    if (ms > 0) m_maxReconnectDelay = ms;
}

void MCPClient::setMaxReconnectAttempts(int max) {
    m_maxReconnectAttempts = max;
}

void MCPClient::setDefaultTimeoutMs(int ms) {
    if (ms > 0) m_defaultTimeoutMs = ms;
}

// ---------------------------------------------------------------------------
// Transport startup
// ---------------------------------------------------------------------------

bool MCPClient::startStdio() {
    m_proc.setWorkingDirectory(m_stdioWorkingDir);
    m_proc.start(m_stdioCommand, m_stdioArgs);
    if (!m_proc.waitForStarted(5000)) {
        fail("Failed to start process: " + m_proc.errorString(),
             TransportError);
        return false;
    }
    // The handshake is kicked off in onProcessStarted().
    return true;
}

void MCPClient::startHttp() {
    doHandshake(); // POST initialize; SSE stream opened after initialize
}

void MCPClient::onProcessStarted() { doHandshake(); }

void MCPClient::doHandshake() {
    if (m_transport == MCPTransport::None) return;

    QJsonObject params;
    params["protocolVersion"] = "2025-03-26";
    QJsonObject clientInfo;
    clientInfo["name"] = "ModelFit";
    clientInfo["version"] = "0.1.0";
    params["clientInfo"] = clientInfo;
    QJsonObject capabilities;
    QJsonObject roots;
    roots["listChanged"] = false;
    capabilities["roots"] = roots;
    params["capabilities"] = capabilities;

    m_initializeId = sendRequest("initialize", params, m_defaultTimeoutMs);
}

void MCPClient::sendInitializedNotification() {
    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["method"] = "notifications/initialized";
    msg["params"] = QJsonObject();
    sendJsonRpc(msg);
}

void MCPClient::openSseStream() {
    if (m_httpUrl.isEmpty()) return;
    if (m_sseReply) {
        m_sseReply->abort();
        m_sseReply->deleteLater();
        m_sseReply = nullptr;
    }
    m_sseBuffer.clear();

    QNetworkRequest req{QUrl(m_httpUrl)};
    req.setRawHeader("Accept", "text/event-stream");
    if (!m_httpBearer.isEmpty())
        req.setRawHeader("Authorization",
                         ("Bearer " + m_httpBearer).toUtf8());
    if (!m_httpSessionId.isEmpty())
        req.setRawHeader("Mcp-Session-Id", m_httpSessionId.toUtf8());
    for (auto it = m_httpCustomHeaders.cbegin();
         it != m_httpCustomHeaders.cend(); ++it)
        req.setRawHeader(it.key().toUtf8(), it.value().toString().toUtf8());

    QNetworkReply *reply = m_nam.get(req);
    reply->setParent(nullptr);
    m_sseReply = reply;
    connect(reply, &QNetworkReply::readyRead, this,
            &MCPClient::onSseReadyRead);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply]() { onSseFinished(reply); });
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------

void MCPClient::sendJsonRpc(const QJsonObject &msg) {
    QByteArray data =
        QJsonDocument(msg).toJson(QJsonDocument::Compact) + "\n";

    switch (m_transport) {
    case MCPTransport::Stdio:
        if (m_proc.state() == QProcess::Running)
            m_proc.write(data);
        break;
    case MCPTransport::StreamableHttp: {
        qint64 id = msg.value("id").toVariant().toLongLong();
        sendHttpPost(id, msg);
        break;
    }
    default:
        break;
    }
}

void MCPClient::sendHttpPost(qint64 id, const QJsonObject &msg) {
    if (m_httpUrl.isEmpty()) return;

    QByteArray body =
        QJsonDocument(msg).toJson(QJsonDocument::Compact);

    QNetworkRequest req{QUrl(m_httpUrl)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json, text/event-stream");
    if (!m_httpBearer.isEmpty())
        req.setRawHeader("Authorization",
                         ("Bearer " + m_httpBearer).toUtf8());
    if (!m_httpSessionId.isEmpty())
        req.setRawHeader("Mcp-Session-Id", m_httpSessionId.toUtf8());
    for (auto it = m_httpCustomHeaders.cbegin();
         it != m_httpCustomHeaders.cend(); ++it)
        req.setRawHeader(it.key().toUtf8(), it.value().toString().toUtf8());

    QNetworkReply *reply = m_nam.post(req, body);
    reply->setParent(nullptr);
    m_httpReplies.insert(id, reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, id]() { onHttpPostFinished(reply, id); });
}

// ---------------------------------------------------------------------------
// stdio
// ---------------------------------------------------------------------------

void MCPClient::handleStdioStdout() {
    while (m_proc.canReadLine()) {
        QByteArray line = m_proc.readLine().trimmed();
        if (line.isEmpty()) continue;
        QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isNull() || !doc.isObject()) {
            emit log("Malformed line on stdout: " +
                     QString::fromUtf8(line.left(200)));
            continue;
        }
        processJsonMessage(doc);
    }
}

void MCPClient::handleStdioStderr() {
    QByteArray err = m_proc.readAllStandardError();
    if (!err.isEmpty()) emit log(QString::fromUtf8(err).trimmed());
}

void MCPClient::onProcessError(QProcess::ProcessError error) {
    Q_UNUSED(error);
    QString msg = "Process error: " + m_proc.errorString();
    emit log(msg);
    if (m_transport != MCPTransport::None && !m_intentionalDisconnect &&
        !m_connected)
        fail(msg, TransportError);
}

void MCPClient::onProcessFinished(int exitCode,
                                  QProcess::ExitStatus /*exitStatus*/) {
    if (m_intentionalDisconnect) {
        if (m_connected) {
            m_connected = false;
            m_handshakeDone = false;
            m_transport = MCPTransport::None;
            m_reconnectTransport = MCPTransport::None;
            emit disconnected();
        }
        return;
    }

    emit log(QString("Process exited (code %1)").arg(exitCode));

    if (m_connected || m_handshakeDone || m_initializeId != -1) {
        // Unexpected exit while connected or mid-handshake: reconnect.
        scheduleReconnect();
    } else {
        fail(QString("Process exited before handshake (code %1)")
                 .arg(exitCode),
             TransportError);
    }
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------

void MCPClient::onHttpPostFinished(QNetworkReply *reply, qint64 id) {
    m_httpReplies.remove(id);

    // A timeout or cancellation may already have removed the pending request
    // and reported it; do not report it again.
    if (!m_pending.contains(id)) {
        reply->deleteLater();
        return;
    }

    QByteArray body = reply->readAll();
    QString contentType =
        reply->header(QNetworkRequest::ContentTypeHeader).toString();
    QVariant statusVar =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    int status = statusVar.isValid() ? statusVar.toInt() : 0;

    QString session =
        QString::fromUtf8(reply->rawHeader("Mcp-Session-Id"));
    if (!session.isEmpty() && session != m_httpSessionId) {
        m_httpSessionId = session;
        emit log("Mcp-Session-Id: " + m_httpSessionId);
    }

    bool transportError = reply->error() != QNetworkReply::NoError;
    reply->deleteLater();

    if (transportError) {
        emit requestError(id, MCPErrorInfo{
                                MCPClientError::TransportError, 0,
                                reply->errorString()});
        emit log("HTTP POST error: " + reply->errorString());
        if (m_transport != MCPTransport::None && !m_intentionalDisconnect)
            scheduleReconnect();
        return;
    }

    if (status >= 400) {
        QString msg =
            QString("HTTP %1 for JSON-RPC request %2").arg(status).arg(id);
        emit requestError(id, MCPErrorInfo{MCPClientError::TransportError, 0,
                                           msg});
        emit log(msg);
        if (status == 401 || status == 403)
            emit log("Hint: the endpoint requires authentication "
                     "(bearer token or custom headers).");
        if (m_transport != MCPTransport::None && !m_intentionalDisconnect)
            scheduleReconnect();
        return;
    }

    if (status == 202) {
        // Accepted: any result arrives on the SSE stream.
        return;
    }

    if (contentType.contains("text/event-stream")) {
        processSseBlock(body);
        return;
    }

    if (!body.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isNull() || !doc.isObject()) {
            emit log("Non-JSON HTTP response: " +
                     QString::fromUtf8(body.left(200)));
        } else {
            processJsonMessage(doc);
        }
    }
}

void MCPClient::onSseReadyRead() {
    if (!m_sseReply) return;
    m_sseBuffer += m_sseReply->readAll();

    while (true) {
        int idx = m_sseBuffer.indexOf("\n\n");
        if (idx == -1) break;
        QByteArray block = m_sseBuffer.left(idx);
        m_sseBuffer = m_sseBuffer.mid(idx + 2);
        while (!m_sseBuffer.isEmpty() &&
               (m_sseBuffer[0] == '\n' || m_sseBuffer[0] == '\r'))
            m_sseBuffer = m_sseBuffer.mid(1);
        processSseBlock(block);
    }
}

void MCPClient::onSseFinished(QNetworkReply *reply) {
    if (m_sseReply == reply) m_sseReply = nullptr;
    reply->deleteLater();

    if (m_connected && !m_intentionalDisconnect &&
        m_transport == MCPTransport::StreamableHttp) {
        // Stream ended while connected: reconnect and re-handshake.
        scheduleReconnect();
    }
}

void MCPClient::processSseBlock(const QByteArray &block) {
    QString data;
    QStringList lines = QString::fromUtf8(block).split('\n');
    for (const QString &line : lines) {
        if (line.startsWith("data: ")) {
            data += line.mid(6) + "\n";
        } else if (line.startsWith("data:")) {
            data += line.mid(5) + "\n";
        }
        // event:, id:, retry: and comment lines are ignored.
    }
    if (data.isEmpty()) return;
    data.chop(1); // trailing newline
    QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        emit log("SSE: invalid JSON-RPC message");
        return;
    }
    processJsonMessage(doc);
}

// ---------------------------------------------------------------------------
// Message dispatch
// ---------------------------------------------------------------------------

void MCPClient::processJsonMessage(const QJsonDocument &doc) {
    if (doc.isNull() || !doc.isObject()) {
        emit log("Invalid JSON-RPC message (not an object)");
        return;
    }

    QJsonObject o = doc.object();
    QJsonValue idVal = o.value("id");
    bool hasId = !idVal.isNull();
    qint64 id = -1;
    if (hasId) {
        bool ok = false;
        id = idVal.toVariant().toLongLong(&ok);
        if (!ok) {
            emit log("JSON-RPC message with non-numeric id");
            id = -1;
        }
    }

    QString method = o.value("method").toString();

    // ----- server -> client request / notification -----------------------
    if (!method.isEmpty()) {
        QJsonObject params = o.value("params").toObject();
        if (!hasId) {
            emit notification(method, params);
            return;
        }
        emit serverRequest(id, method, params);
        if (method == "ping") {
            respondToRequest(id, QJsonObject());
        }
        return;
    }

    // ----- response to a client request ----------------------------------
    if (!hasId || id < 0) {
        emit log("JSON-RPC response without id");
        return;
    }

    auto it = m_pending.find(id);
    if (it == m_pending.end()) {
        emit log(QString("Unexpected JSON-RPC response id %1").arg(id));
        return;
    }

    PendingRequest pr = *it;
    QTimer *timer = pr.timer;
    m_pending.erase(it);
    if (timer) {
        timer->stop();
        timer->deleteLater();
    }

    resetReconnectBackoff();

    if (o.contains("error")) {
        QJsonObject err = o.value("error").toObject();
        MCPErrorInfo info;
        info.code = MCPClientError::ServerError;
        info.jsonRpcCode = err.value("code").toInt();
        info.message = err.value("message").toString();
        emit requestError(id, info);

        if (id == m_initializeId) {
            m_initializeId = -1;
            fail(info.message, ProtocolError);
            return;
        }
        if (pr.method == "tools/call")
            emit toolError(id, pr.toolName, info.message);
        return;
    }

    QJsonValue result = o.value("result");

    if (id == m_initializeId) {
        m_serverProtocolVersion =
            result.toObject().value("protocolVersion").toString("2025-03-26");
        QJsonObject serverInfo = result.toObject().value("serverInfo").toObject();
        m_serverName = serverInfo.value("name").toString();
        m_serverVersion = serverInfo.value("version").toString();

        m_handshakeDone = true;
        m_connected = true;
        m_initializeId = -1;
        resetReconnectBackoff();

        emit handshakeFinished(true, {});
        emit connected();

        // Client-side notification required by the MCP spec after initialize.
        sendInitializedNotification();

        // Then discover tools (as promised: handshake, then tools/list).
        listTools();
        return;
    }

    emit requestResult(id, result);

    if (pr.method == "tools/call") {
        emit toolResult(id, pr.toolName, result.toObject());
        return;
    }
    if (pr.method == "tools/list") {
        QJsonObject res = result.toObject();
        QJsonArray arr = res.value("tools").toArray();
        m_tools.clear();
        for (const QJsonValue &v : arr) {
            QJsonObject t = v.toObject();
            MCPTool tool;
            tool.name = t.value("name").toString();
            tool.description = t.value("description").toString();
            tool.inputSchema = t.value("inputSchema").toObject();
            m_tools << tool;
        }
        emit toolsDiscovered(m_tools);
        return;
    }
    if (pr.method == "resources/list") {
        QJsonObject res = result.toObject();
        QJsonArray arr = res.value("resources").toArray();
        m_resources.clear();
        for (const QJsonValue &v : arr) {
            QJsonObject r = v.toObject();
            MCPResource resrc;
            resrc.uri = r.value("uri").toString();
            resrc.name = r.value("name").toString();
            resrc.description = r.value("description").toString();
            resrc.mimeType = r.value("mimeType").toString();
            m_resources << resrc;
        }
        emit resourcesDiscovered(m_resources);
        return;
    }
    if (pr.method == "resources/read") {
        emit resourceRead(pr.context, result.toObject());
        return;
    }
    if (pr.method == "prompts/list") {
        QJsonObject res = result.toObject();
        QJsonArray arr = res.value("prompts").toArray();
        m_prompts.clear();
        for (const QJsonValue &v : arr) {
            QJsonObject p = v.toObject();
            MCPPrompt prm;
            prm.name = p.value("name").toString();
            prm.description = p.value("description").toString();
            prm.arguments = p.value("arguments").toArray();
            m_prompts << prm;
        }
        emit promptsDiscovered(m_prompts);
        return;
    }
    if (pr.method == "prompts/get") {
        emit promptGot(pr.context, result.toObject());
        return;
    }
}

// ---------------------------------------------------------------------------
// Discovery requests
// ---------------------------------------------------------------------------

void MCPClient::listTools() {
    sendRequest("tools/list", QJsonObject(), m_defaultTimeoutMs);
}

void MCPClient::listResources() {
    sendRequest("resources/list", QJsonObject(), m_defaultTimeoutMs);
}

void MCPClient::readResource(const QString &uri) {
    QJsonObject params;
    params["uri"] = uri;
    sendRequest("resources/read", params, m_defaultTimeoutMs, uri);
}

void MCPClient::listPrompts() {
    sendRequest("prompts/list", QJsonObject(), m_defaultTimeoutMs);
}

void MCPClient::getPrompt(const QString &name,
                          const QJsonObject &arguments) {
    QJsonObject params;
    params["name"] = name;
    if (!arguments.isEmpty()) params["arguments"] = arguments;
    sendRequest("prompts/get", params, m_defaultTimeoutMs, name);
}

// ---------------------------------------------------------------------------
// Tool calling with approval gate
// ---------------------------------------------------------------------------

void MCPClient::callTool(const QString &name, const QJsonObject &args) {
    if (!m_connected) {
        emit toolError(0, name, "Not connected");
        return;
    }
    qint64 id = m_approvalId++;
    PendingApproval pa;
    pa.requestId = id;
    pa.toolName = name;
    pa.args = args;
    m_pendingApprovals.insert(id, pa);
    emit toolCallRequested(id, name, args);
}

void MCPClient::approveToolCall(qint64 requestId, bool allowed) {
    auto it = m_pendingApprovals.find(requestId);
    if (it == m_pendingApprovals.end()) return;
    PendingApproval pa = *it;
    m_pendingApprovals.erase(it);

    if (!allowed) {
        emit toolCallDenied(requestId, pa.toolName);
        emit toolError(0, pa.toolName, "Tool call denied by user");
        return;
    }

    emit toolCallApproved(requestId);

    if (!m_connected) {
        emit toolError(0, pa.toolName, "Not connected");
        return;
    }

    QJsonObject params;
    params["name"] = pa.toolName;
    params["arguments"] = pa.args;
    sendRequest("tools/call", params, m_defaultTimeoutMs, pa.toolName);
}

bool MCPClient::isAwaitingApproval(qint64 requestId) const {
    return m_pendingApprovals.contains(requestId);
}

// ---------------------------------------------------------------------------
// Generic JSON-RPC
// ---------------------------------------------------------------------------

qint64 MCPClient::sendRequest(const QString &method,
                              const QJsonObject &params, int timeoutMs,
                              const QString &toolName) {
    if (m_transport == MCPTransport::None) {
        fail("No transport connected", MCPClientError::NotConnected);
        return -1;
    }

    qint64 id = nextId();
    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["id"] = id;
    msg["method"] = method;
    if (!params.isEmpty()) msg["params"] = params;

    PendingRequest pr;
    pr.id = id;
    pr.method = method;
    pr.toolName = toolName;
    if (timeoutMs < 0) timeoutMs = m_defaultTimeoutMs;

    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this,
            [this, id]() { onRequestTimeout(id); });
    pr.timer = timer;
    m_pending.insert(id, pr);
    if (timeoutMs > 0) timer->start(timeoutMs);

    sendJsonRpc(msg);
    return id;
}

void MCPClient::cancelRequest(qint64 requestId) {
    auto it = m_pending.find(requestId);
    if (it == m_pending.end()) return;
    PendingRequest pr = *it;
    QTimer *timer = pr.timer;
    m_pending.erase(it);
    if (timer) {
        timer->stop();
        timer->deleteLater();
    }
    if (auto reply = m_httpReplies.value(requestId, nullptr))
        reply->abort();

    emit requestError(requestId,
                      MCPErrorInfo{MCPClientError::RequestCancelled, 0,
                                   "Request cancelled"});
    if (pr.method == "tools/call")
        emit toolError(requestId, pr.toolName, "Request cancelled");
}

void MCPClient::respondToRequest(qint64 requestId, const QJsonValue &result) {
    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["id"] = requestId;
    msg["result"] = result;
    sendJsonRpc(msg);
}

void MCPClient::respondToRequest(qint64 requestId, int code,
                                 const QString &message,
                                 const QJsonValue &data) {
    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["id"] = requestId;
    QJsonObject err;
    err["code"] = code;
    err["message"] = message;
    if (!data.isNull()) err["data"] = data;
    msg["error"] = err;
    sendJsonRpc(msg);
}

// ---------------------------------------------------------------------------
// Request tracking, timeouts, reconnect
// ---------------------------------------------------------------------------

qint64 MCPClient::nextId() { return ++m_nextId; }

void MCPClient::onRequestTimeout(qint64 id) {
    auto it = m_pending.find(id);
    if (it == m_pending.end()) return;
    PendingRequest pr = *it;
    QTimer *timer = pr.timer;
    m_pending.erase(it);
    if (timer) {
        timer->stop();
        timer->deleteLater();
    }
    if (auto reply = m_httpReplies.value(id, nullptr)) reply->abort();

    MCPErrorInfo info;
    info.code = MCPClientError::RequestTimeout;
    info.message = QString("Request timed out: %1").arg(pr.method);
    emit requestError(id, info);
    emit log(info.message);
    if (pr.method == "tools/call")
        emit toolError(id, pr.toolName, info.message);
}

void MCPClient::scheduleReconnect() {
    if (!m_autoReconnect || m_intentionalDisconnect ||
        m_transport == MCPTransport::None)
        return;
    if (m_maxReconnectAttempts >= 0 &&
        m_reconnectAttempts >= m_maxReconnectAttempts) {
        emit log("Reconnect attempts exhausted");
        m_connected = false;
        m_handshakeDone = false;
        m_transport = MCPTransport::None;
        m_reconnectTransport = MCPTransport::None;
        return;
    }

    m_reconnectTransport = m_transport;
    bool wasConnected = m_connected;
    m_connected = false;
    m_handshakeDone = false;
    m_transport = MCPTransport::None;
    if (wasConnected) emit disconnected();

    int delay = m_reconnectDelay;
    m_reconnectDelay = qMin(m_reconnectDelay * 2, m_maxReconnectDelay);
    ++m_reconnectAttempts;
    emit log(QString("Reconnecting in %1 ms (attempt %2)...")
                 .arg(delay)
                 .arg(m_reconnectAttempts));
    m_reconnectTimer.start(delay);
}

void MCPClient::retryConnect() {
    if (m_intentionalDisconnect) return;
    if (m_transport != MCPTransport::None) return;
    if (m_reconnectTransport == MCPTransport::None) return;
    if (m_reconnectTransport == MCPTransport::Stdio)
        startStdio();
    else if (m_reconnectTransport == MCPTransport::StreamableHttp)
        startHttp();
}

void MCPClient::resetReconnectBackoff() {
    m_reconnectDelay = 1000;
    m_reconnectAttempts = 0;
}

void MCPClient::fail(const QString &message, MCPClientError code) {
    emit log(message);
    emit error(code, message);
    if (m_connected) {
        m_connected = false;
        m_handshakeDone = false;
        m_transport = MCPTransport::None;
        m_reconnectTransport = MCPTransport::None;
        emit disconnected();
    }
    if (!m_intentionalDisconnect && m_autoReconnect &&
        m_transport != MCPTransport::None)
        scheduleReconnect();
}

} // namespace ModelFit
