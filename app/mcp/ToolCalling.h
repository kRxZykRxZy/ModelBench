#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonValue>
#include <QHash>
#include <QDateTime>
#include <QString>

namespace ModelFit {

class MCPClient;

// Structured result of a tool call orchestrated through MCPClient.
struct ToolCallResult {
    bool ok = false;
    QString serverId;
    QString toolName;
    QJsonObject output;      // structured content returned by the server
    QString error;           // human-readable error message when !ok
    int jsonRpcCode = 0;     // JSON-RPC error code when from a server error
    double durationMs = 0;   // wall-clock duration of the call
    bool truncated = false;  // true when output was capped by maxOutputBytes
};

// Tool calling orchestration helper.
//   - validates tool arguments against inputSchema (JSON Schema subset:
//     required, type, enum)
//   - executes via MCPClient (approval gate respected: the call waits for
//     external approval via MCPClient::approveToolCall)
//   - enforces per-call timeout and output size caps
//   - emits structured events for the UI
class ToolCalling : public QObject {
    Q_OBJECT
public:
    explicit ToolCalling(QObject *parent = nullptr);
    ~ToolCalling() override;

    void setClient(MCPClient *client, const QString &serverId = {});
    MCPClient *client() const { return m_client; }
    void setServerId(const QString &serverId);
    QString serverId() const { return m_serverId; }

    void setDefaultTimeoutMs(int ms);
    int defaultTimeoutMs() const { return m_defaultTimeoutMs; }

    void setMaxOutputBytes(qint64 bytes);
    qint64 maxOutputBytes() const { return m_maxOutputBytes; }

    // JSON Schema subset validation (required, type, enum). Usable without
    // a client. Returns {ok: bool, errors?: [string]}.
    static QJsonObject validateArgs(const QJsonObject &inputSchema,
                                    const QJsonObject &args);

    // Validate + request execution through the MCP client's approval gate.
    // The result arrives asynchronously through toolCallFinished.
    void call(const QString &toolName, const QJsonObject &args = {});

signals:
    void toolCallStarted(const QString &serverId, const QString &toolName,
                         const QJsonObject &args);
    void toolCallFinished(const QString &serverId, const QString &toolName,
                          const ToolCallResult &result);
    void validationError(const QString &toolName, const QString &error);

private:
    static bool checkType(const QJsonValue &value, const QString &type);

    void onClientToolResult(qint64 callId, const QString &name,
                            const QJsonObject &result);
    void onClientToolError(qint64 callId, const QString &name,
                           const QString &error);
    void onClientToolCallDenied(qint64 requestId, const QString &name);

    struct PendingCall {
        QString toolName;
        QJsonObject args;
        qint64 startedMs = 0;
    };
    // Per-tool FIFO queue: results for a tool name complete calls in order.
    QHash<QString, QList<PendingCall>> m_pendingByTool;

    MCPClient *m_client = nullptr;
    QString m_serverId;
    int m_defaultTimeoutMs = 30000;
    qint64 m_maxOutputBytes = 1024 * 1024; // 1 MiB
};

} // namespace ModelFit

Q_DECLARE_METATYPE(ModelFit::ToolCallResult)
