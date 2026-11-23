#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QStringList>
#include <QJsonObject>
#include "MCPClient.h"

namespace ModelBench {

enum class MCPServerStatus { Disconnected, Connecting, Connected, Error };

struct MCPServerConfig {
    QString id;
    QString name;
    QString transport; // stdio | streamable-http
    QString command;
    QStringList args;
    QString workingDir;
    QString url;
    QString authToken;       // bearer token for HTTP transport
    QVariantMap customHeaders;
    bool enabled = true;
    bool autoReconnect = true;
};

// Manages the lifecycle of MCP servers: configuration persistence (JSON file
// in the app data directory), connect/disconnect/restart, per-server status
// tracking and a connection log buffer. Does not own the UI layer.
class MCPServerManager : public QObject {
    Q_OBJECT
    Q_ENUM(MCPServerStatus)
public:
    explicit MCPServerManager(QObject *parent = nullptr);
    ~MCPServerManager() override;

    QString storagePath() const { return m_storagePath; }
    void setStoragePath(const QString &path);

    // Configuration
    Q_INVOKABLE void addServer(const MCPServerConfig &cfg);
    Q_INVOKABLE void addServerConfig(const QString &name, const QString &transport,
                                     const QString &command, const QString &args, const QString &url);
    Q_INVOKABLE QVariantList getServers() const;
    Q_INVOKABLE void removeServer(const QString &id);
    Q_INVOKABLE void setEnabled(const QString &id, bool enabled);
    bool isEnabled(const QString &id) const;
    void setAutoReconnect(const QString &id, bool enabled);

    // Lifecycle
    void connectServer(const QString &id);
    void disconnectServer(const QString &id);
    void restartServer(const QString &id);
    Q_INVOKABLE void connectServerQml(const QString &id) { connectServer(id); }
    Q_INVOKABLE void disconnectServerQml(const QString &id) { disconnectServer(id); }

    // Inspection
    MCPClient *client(const QString &id) const;
    MCPServerStatus status(const QString &id) const;
    MCPServerConfig config(const QString &id) const;
    QList<MCPServerConfig> servers() const { return m_servers; }
    QStringList logs(const QString &id) const;
    QString lastError(const QString &id) const;

    // Persistence
    void loadServers();
    void saveServers();

signals:
    void serverAdded(const QString &id);
    void serverRemoved(const QString &id);
    void serverEnabledChanged(const QString &id, bool enabled);
    void serverStatusChanged(const QString &id, MCPServerStatus status);
    void serverConnected(const QString &id);
    void serverDisconnected(const QString &id);
    void serverError(const QString &id, const QString &error);
    void serverLog(const QString &id, const QString &line);
    void configsChanged();
    void loadFinished(bool ok);
    void saveFinished(bool ok);

private:
    int indexOf(const QString &id) const;
    void addLog(const QString &id, const QString &line);
    void trimLogs(const QString &id);
    void onClientConnected(const QString &id);
    void onClientDisconnected(const QString &id);
    void onClientError(const QString &id, MCPClientError code,
                       const QString &message);
    void onClientLog(const QString &id, const QString &line);

    QJsonObject configToJson(const MCPServerConfig &c) const;
    MCPServerConfig configFromJson(const QJsonObject &o) const;

    QString m_storagePath;
    QList<MCPServerConfig> m_servers;
    QMap<QString, MCPClient *> m_clients;
    QMap<QString, MCPServerStatus> m_status;
    QMap<QString, QStringList> m_logs;
    QMap<QString, QString> m_lastError;
    static constexpr int MaxLogEntries = 1000;
};

} // namespace ModelBench
