#include "MCPServerManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <algorithm>

namespace ModelBench {

MCPServerManager::MCPServerManager(QObject *parent) : QObject(parent) {
    m_storagePath =
        QCoreApplication::applicationDirPath() + "/mcp";
    // Prefer app data dir; falls back to the application dir where that is
    // unavailable/writable.
    QString appData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appData.isEmpty()) m_storagePath = appData + "/mcp";

    loadServers();
}

MCPServerManager::~MCPServerManager() {
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        it.value()->disconnect();
        it.value()->deleteLater();
    }
    m_clients.clear();
}

int MCPServerManager::indexOf(const QString &id) const {
    for (int i = 0; i < m_servers.size(); ++i)
        if (m_servers[i].id == id) return i;
    return -1;
}

void MCPServerManager::addLog(const QString &id, const QString &line) {
    QStringList &list = m_logs[id];
    list << line;
    trimLogs(id);
    emit serverLog(id, line);
}

void MCPServerManager::trimLogs(const QString &id) {
    QStringList &list = m_logs[id];
    while (list.size() > MaxLogEntries) list.removeFirst();
}

QJsonObject MCPServerManager::configToJson(const MCPServerConfig &c) const {
    QJsonObject o;
    o["id"] = c.id;
    o["name"] = c.name;
    o["transport"] = c.transport;
    o["command"] = c.command;
    o["args"] = QJsonArray::fromStringList(c.args);
    o["workingDir"] = c.workingDir;
    o["url"] = c.url;
    o["authToken"] = c.authToken;
    o["customHeaders"] = QJsonObject::fromVariantMap(c.customHeaders);
    o["enabled"] = c.enabled;
    o["autoReconnect"] = c.autoReconnect;
    return o;
}

MCPServerConfig MCPServerManager::configFromJson(const QJsonObject &o) const {
    MCPServerConfig c;
    c.id = o.value("id").toString();
    c.name = o.value("name").toString();
    c.transport = o.value("transport").toString("stdio");
    c.command = o.value("command").toString();
    for (const auto &v : o.value("args").toArray()) c.args << v.toString();
    c.workingDir = o.value("workingDir").toString();
    c.url = o.value("url").toString();
    c.authToken = o.value("authToken").toString();
    c.customHeaders =
        o.value("customHeaders").toObject().toVariantMap();
    c.enabled = o.value("enabled").toBool(true);
    c.autoReconnect = o.value("autoReconnect").toBool(true);
    return c;
}

void MCPServerManager::setStoragePath(const QString &path) {
    m_storagePath = path;
}

void MCPServerManager::loadServers() {
    QDir dir(m_storagePath);
    if (!dir.exists()) dir.mkpath(".");

    QFile file(m_storagePath + "/servers.json");
    if (!file.exists()) {
        emit loadFinished(true);
        emit configsChanged();
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        emit loadFinished(false);
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    m_servers.clear();
    if (doc.isArray()) {
        for (const QJsonValue &v : doc.array()) {
            if (!v.isObject()) continue;
            MCPServerConfig c = configFromJson(v.toObject());
            if (c.id.isEmpty()) c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            m_servers << c;
        }
    }
    emit loadFinished(true);
    emit configsChanged();
}

void MCPServerManager::saveServers() {
    QDir dir(m_storagePath);
    if (!dir.exists()) dir.mkpath(".");

    QJsonArray arr;
    for (const MCPServerConfig &c : m_servers) arr.append(configToJson(c));

    QFile file(m_storagePath + "/servers.json");
    if (!file.open(QIODevice::WriteOnly)) {
        emit saveFinished(false);
        return;
    }
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    file.close();
    emit saveFinished(true);
}

void MCPServerManager::addServer(const MCPServerConfig &cfg) {
    MCPServerConfig c = cfg;
    if (c.id.isEmpty())
        c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_servers << c;
    saveServers();
    emit serverAdded(c.id);
    emit configsChanged();
}

void MCPServerManager::addServerConfig(const QString &name, const QString &transport,
                                       const QString &command, const QString &args, const QString &url) {
    MCPServerConfig c;
    c.name = name;
    c.transport = transport;
    c.command = command;
    c.args = args.split(' ', Qt::SkipEmptyParts);
    c.url = url;
    addServer(c);
}

QVariantList MCPServerManager::getServers() const {
    QVariantList out;
    for (const auto &c : m_servers) {
        QVariantMap m;
        m["id"] = c.id;
        m["name"] = c.name;
        m["transport"] = c.transport;
        m["command"] = c.command;
        m["url"] = c.url;
        m["enabled"] = c.enabled;
        out << m;
    }
    return out;
}

void MCPServerManager::removeServer(const QString &id) {
    disconnectServer(id);
    m_clients.remove(id);

    int idx = indexOf(id);
    if (idx < 0) return;
    m_servers.removeAt(idx);
    m_status.remove(id);
    m_logs.remove(id);
    m_lastError.remove(id);
    saveServers();
    emit serverRemoved(id);
    emit configsChanged();
}

void MCPServerManager::setEnabled(const QString &id, bool enabled) {
    int idx = indexOf(id);
    if (idx < 0) return;
    m_servers[idx].enabled = enabled;
    saveServers();
    emit serverEnabledChanged(id, enabled);
    emit configsChanged();
}

bool MCPServerManager::isEnabled(const QString &id) const {
    int idx = indexOf(id);
    return idx >= 0 && m_servers[idx].enabled;
}

void MCPServerManager::setAutoReconnect(const QString &id, bool enabled) {
    int idx = indexOf(id);
    if (idx < 0) return;
    m_servers[idx].autoReconnect = enabled;
    saveServers();
    emit configsChanged();
}

void MCPServerManager::connectServer(const QString &id) {
    int idx = indexOf(id);
    if (idx < 0) {
        emit serverError(id, "Server not found");
        return;
    }
    const MCPServerConfig &cfg = m_servers[idx];
    if (!cfg.enabled) {
        emit serverError(id, "Server is disabled");
        return;
    }

    m_status[id] = MCPServerStatus::Connecting;
    m_lastError.remove(id);
    emit serverStatusChanged(id, MCPServerStatus::Connecting);

    MCPClient *client = new MCPClient(this);
    client->setAutoReconnect(cfg.autoReconnect);
    client->setDefaultTimeoutMs(30000);

    connect(client, &MCPClient::connected, this,
            [this, id]() { onClientConnected(id); });
    connect(client, &MCPClient::disconnected, this,
            [this, id]() { onClientDisconnected(id); });
    connect(client, &MCPClient::error, this,
            [this, id](MCPClientError code, const QString &message) {
                Q_UNUSED(code);
                onClientError(id, code, message);
            });
    connect(client, &MCPClient::log, this,
            [this, id](const QString &line) { onClientLog(id, line); });

    m_clients[id] = client;

    bool ok = false;
    if (cfg.transport == "stdio")
        ok = client->connectStdio(cfg.command, cfg.args, cfg.workingDir);
    else if (cfg.transport == "streamable-http")
        ok = client->connectHttp(cfg.url, cfg.authToken, cfg.customHeaders);
    else {
        emit serverError(id, QString("Unknown transport: %1").arg(cfg.transport));
        m_status[id] = MCPServerStatus::Error;
        emit serverStatusChanged(id, MCPServerStatus::Error);
        return;
    }
    Q_UNUSED(ok); // connection state is reported asynchronously via signals
}

void MCPServerManager::disconnectServer(const QString &id) {
    if (m_clients.contains(id)) {
        m_clients[id]->disconnect();
        // The client object is retained so restartServer() can reuse it;
        // its status is updated via the disconnected() signal above.
    } else {
        m_status[id] = MCPServerStatus::Disconnected;
        emit serverStatusChanged(id, MCPServerStatus::Disconnected);
        emit serverDisconnected(id);
    }
}

void MCPServerManager::restartServer(const QString &id) {
    disconnectServer(id);
    // Reconnect immediately; the process teardown in MCPClient::disconnect()
    // is synchronous enough for the stdio transport.
    connectServer(id);
}

MCPClient *MCPServerManager::client(const QString &id) const {
    return m_clients.value(id, nullptr);
}

MCPServerStatus MCPServerManager::status(const QString &id) const {
    return m_status.value(id, MCPServerStatus::Disconnected);
}

MCPServerConfig MCPServerManager::config(const QString &id) const {
    int idx = indexOf(id);
    return idx >= 0 ? m_servers[idx] : MCPServerConfig();
}

QStringList MCPServerManager::logs(const QString &id) const {
    return m_logs.value(id);
}

QString MCPServerManager::lastError(const QString &id) const {
    return m_lastError.value(id);
}

void MCPServerManager::onClientConnected(const QString &id) {
    m_status[id] = MCPServerStatus::Connected;
    m_lastError.remove(id);
    emit serverStatusChanged(id, MCPServerStatus::Connected);
    emit serverConnected(id);
    addLog(id, "Connected");
}

void MCPServerManager::onClientDisconnected(const QString &id) {
    m_status[id] = MCPServerStatus::Disconnected;
    emit serverStatusChanged(id, MCPServerStatus::Disconnected);
    emit serverDisconnected(id);
    addLog(id, "Disconnected");
}

void MCPServerManager::onClientError(const QString &id, MCPClientError /*code*/,
                                     const QString &message) {
    m_status[id] = MCPServerStatus::Error;
    m_lastError[id] = message;
    emit serverStatusChanged(id, MCPServerStatus::Error);
    emit serverError(id, message);
    addLog(id, "Error: " + message);
}

void MCPServerManager::onClientLog(const QString &id,
                                   const QString &line) {
    addLog(id, line);
}

} // namespace ModelBench
