#pragma once

#include <QObject>
#include <QProcess>
#include <QNetworkAccessManager>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QVariantMap>
#include <QHash>
#include <QTimer>
#include <QElapsedTimer>

namespace ModelFit {

struct MCPTool {
    QString name;
    QString description;
    QJsonObject inputSchema; // JSON Schema (subset validated by ToolCalling)
};

struct MCPResource {
    QString uri;
    QString name;
    QString description;
    QString mimeType;
};

struct MCPPrompt {
    QString name;
    QString description;
    QJsonArray arguments; // {name, description, required?}
};

enum class MCPTransport { None, Stdio, StreamableHttp };

enum class MCPClientError {
    None,
    TransportError,   // process/HTTP failure
    ProtocolError,    // malformed or unexpected protocol message
    ParseError,       // JSON parse failure
    RequestTimeout,   // per-request timeout expired
    RequestCancelled, // request cancelled by caller
    NotConnected,     // no transport / not connected
    ServerError,      // JSON-RPC error response from server
    ApprovalDenied,   // tool call not approved
};

struct MCPErrorInfo {
    MCPClientError code = MCPClientError::None;
    int jsonRpcCode = 0; // set when code == ServerError
    QString message;
};

// MCP client with stdio and Streamable HTTP transports.
// All JSON-RPC responses are matched to requests by id. A per-request
// QTimer enforces timeouts; calls can be cancelled. Tool calls pass
// through an approval gate (toolCallRequested / approveToolCall).
class MCPClient : public QObject {
    Q_OBJECT
    Q_ENUM(MCPClientError)
public:
    explicit MCPClient(QObject *parent = nullptr);
    ~MCPClient() override;

    // ----- transports -------------------------------------------------
    bool connectStdio(const QString &command, const QStringList &args,
                      const QString &workingDir = {});
    bool connectHttp(const QString &url, const QString &bearerToken = {},
                     const QVariantMap &customHeaders = {});
    void setHttpCustomHeaders(const QVariantMap &headers);
    void disconnect();
    void reconnect();

    bool isConnected() const { return m_connected; }
    bool handshakeDone() const { return m_handshakeDone; }
    MCPTransport transport() const { return m_transport; }
    QString transportName() const;

    QString serverProtocolVersion() const { return m_serverProtocolVersion; }
    QString serverName() const { return m_serverName; }
    QString serverVersion() const { return m_serverVersion; }

    void setAutoReconnect(bool enabled);
    bool autoReconnect() const { return m_autoReconnect; }
    void setMaxReconnectDelay(int ms);
    int maxReconnectDelay() const { return m_maxReconnectDelay; }
    void setMaxReconnectAttempts(int max); // <0 = unlimited
    int maxReconnectAttempts() const { return m_maxReconnectAttempts; }

    void setDefaultTimeoutMs(int ms);
    int defaultTimeoutMs() const { return m_defaultTimeoutMs; }

    // ----- discovery --------------------------------------------------
    QList<MCPTool> tools() const { return m_tools; }
    QList<MCPResource> resources() const { return m_resources; }
    QList<MCPPrompt> prompts() const { return m_prompts; }

    void listTools();
    void listResources();
    void readResource(const QString &uri);
    void listPrompts();
    void getPrompt(const QString &name, const QJsonObject &arguments = {});

    // ----- tool calling with approval gate ----------------------------
    // Emits toolCallRequested(id, name, args). The call is only executed
    // after approveToolCall(requestId, true).
    void callTool(const QString &name, const QJsonObject &args = {});
    void approveToolCall(qint64 requestId, bool allowed);
    bool isAwaitingApproval(qint64 requestId) const;

    // ----- generic JSON-RPC -------------------------------------------
    // Returns the request id (>0) or -1 if the client cannot send.
    qint64 sendRequest(const QString &method, const QJsonObject &params = {},
                       int timeoutMs = -1, const QString &toolName = {});
    void cancelRequest(qint64 requestId);
    // Respond to a server-to-client request (e.g. ping).
    void respondToRequest(qint64 requestId, const QJsonValue &result);
    void respondToRequest(qint64 requestId, int code, const QString &message,
                          const QJsonValue &data = QJsonValue());

signals:
    void connected();
    void disconnected();
    void error(MCPClientError code, const QString &message);
    void log(const QString &line);

    void handshakeFinished(bool ok, const QString &error);

    void toolsDiscovered(const QList<MCPTool> &tools);
    void resourcesDiscovered(const QList<MCPResource> &resources);
    void promptsDiscovered(const QList<MCPPrompt> &prompts);

    void resourceRead(const QString &uri, const QJsonObject &contents);
    void resourceReadError(const QString &uri, const QString &error);
    void promptGot(const QString &name, const QJsonObject &result);
    void promptGetError(const QString &name, const QString &error);

    // Matched to requests by id.
    void requestResult(qint64 id, const QJsonValue &result);
    void requestError(qint64 id, const MCPErrorInfo &info);

    // Server -> client requests / notifications.
    void serverRequest(qint64 requestId, const QString &method,
                       const QJsonObject &params);
    void notification(const QString &method, const QJsonObject &params);

    // Approval gate.
    void toolCallRequested(qint64 requestId, const QString &name,
                           const QJsonObject &args);
    void toolCallApproved(qint64 requestId);
    void toolCallDenied(qint64 requestId, const QString &name);

    // Tool call results (carry the JSON-RPC request id as callId).
    void toolResult(qint64 callId, const QString &name,
                    const QJsonObject &result);
    void toolError(qint64 callId, const QString &name, const QString &error);

private:
    // transport startup
    bool startStdio();
    void startHttp();
    void doHandshake();
    void openSseStream();

    void sendJsonRpc(const QJsonObject &msg);
    void sendHttpPost(qint64 id, const QJsonObject &msg);

    // stdio
    void handleStdioStdout();
    void handleStdioStderr();
    void onProcessStarted();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);

    // http
    void onHttpPostFinished(QNetworkReply *reply, qint64 id);
    void onSseReadyRead();
    void onSseFinished(QNetworkReply *reply);
    void processSseBlock(const QByteArray &block);

    // message dispatch
    void processJsonMessage(const QJsonDocument &doc);
    void handleInitializeResult(qint64 id, const QJsonObject &result);
    void sendInitializedNotification();

    // reconnect
    void scheduleReconnect();
    void retryConnect();
    void resetReconnectBackoff();
    void fail(const QString &message, MCPClientError code);

    // request tracking / timeouts / cancellation
    qint64 nextId();
    void onRequestTimeout(qint64 id);

    // approval gate
    void routeToolCallResult(qint64 callId, const QJsonValue &result);
    void routeToolCallError(qint64 callId, const MCPErrorInfo &info);

    QProcess m_proc;
    QNetworkAccessManager m_nam;

    QString m_stdioCommand;
    QStringList m_stdioArgs;
    QString m_stdioWorkingDir;

    QString m_httpUrl;
    QString m_httpBearer;
    QVariantMap m_httpCustomHeaders;
    QString m_httpSessionId;

    QNetworkReply *m_sseReply = nullptr;
    QByteArray m_sseBuffer;
    QHash<qint64, QNetworkReply *> m_httpReplies;

    MCPTransport m_transport = MCPTransport::None;
    MCPTransport m_reconnectTransport = MCPTransport::None; // transport to restore on reconnect
    bool m_connected = false;
    bool m_handshakeDone = false;
    bool m_intentionalDisconnect = false;

    bool m_autoReconnect = true;
    int m_reconnectDelay = 1000;
    int m_maxReconnectDelay = 30000;
    int m_maxReconnectAttempts = -1;
    int m_reconnectAttempts = 0;
    QTimer m_reconnectTimer;

    int m_defaultTimeoutMs = 30000;

    QString m_serverProtocolVersion;
    QString m_serverName;
    QString m_serverVersion;

    QList<MCPTool> m_tools;
    QList<MCPResource> m_resources;
    QList<MCPPrompt> m_prompts;
    struct PendingRequest {
        qint64 id = 0;
        QString method;
        QString toolName;   // only meaningful for tools/call
        QString context;    // uri for resources/read, name for prompts/get
        QTimer *timer = nullptr;
    };
    QHash<qint64, PendingRequest> m_pending;
    qint64 m_nextId = 1;

    qint64 m_initializeId = -1;

    struct PendingApproval {
        qint64 requestId = 0;
        QString toolName;
        QJsonObject args;
    };
    QHash<qint64, PendingApproval> m_pendingApprovals;
    qint64 m_approvalId = 1;
};

} // namespace ModelFit

Q_DECLARE_METATYPE(ModelFit::MCPClientError)
Q_DECLARE_METATYPE(ModelFit::MCPErrorInfo)
