#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "ToolApproval.h"

namespace ModelBench {

// Result of a tool execution. Structured output is preferred (UI-friendly);
// `error` is populated when ok == false.
struct ToolResult {
    bool ok = false;
    QString toolName;
    QJsonObject output;
    QString error;
    QString errorCode;
    qint64 durationMs = 0;
    bool cancelled = false;
    bool truncated = false;
};

// Abstract base for one registered tool. Implementations must emit
// finished(callId, result) exactly once, including on failure, and may emit
// output() any number of times before finishing (streaming tools).
class Tool : public QObject {
    Q_OBJECT
public:
    explicit Tool(QObject *parent = nullptr);
    ~Tool() override;

    virtual QString name() const = 0;
    virtual QString description() const = 0;
    virtual QJsonObject inputSchema() const = 0;
    virtual ApprovalLevel approvalLevel() const { return ApprovalLevel::ReadOnly; }
    // Some tools (e.g. git_branch) have different risk depending on args.
    virtual ApprovalLevel approvalLevelFor(const QJsonObject & /*args*/) const {
        return approvalLevel();
    }

    virtual void execute(qint64 callId, const QJsonObject &args) = 0;
    virtual void cancel(qint64 /*callId*/) {} // best effort

signals:
    void output(qint64 callId, const QString &stream, const QString &chunk);
    void finished(qint64 callId, const ToolResult &result);
};

// Central registry: owns tools (QObject parent), validates arguments, routes
// calls through the approval gate and dispatches to the right tool.
//
// All results are delivered through toolFinished, always asynchronously, so
// callers can safely connect after invoking execute().
class ToolRegistry : public QObject {
    Q_OBJECT
public:
    // A registry without an explicit gate creates its own child ToolApproval.
    explicit ToolRegistry(ToolApproval *approval = nullptr,
                          QObject *parent = nullptr);
    ~ToolRegistry() override;

    ToolApproval *approval() const { return m_approval; }
    void setApproval(ToolApproval *approval);

    // Takes ownership of `tool` (reparents it to this registry).
    bool registerTool(Tool *tool);
    bool unregisterTool(const QString &name);
    Tool *tool(const QString &name) const;
    QStringList toolNames() const; // sorted
    QList<Tool *> tools() const;
    QJsonArray describeTools() const; // name/description/schema/approvalLevel

    // JSON Schema subset validation: required, type, enum. Returns
    // {ok: bool, error?: string}. Usable standalone (e.g. from tests).
    static QJsonObject validateArgs(const QJsonObject &schema,
                                    const QJsonObject &args);

    // Returns a unique call id. The outcome arrives via toolFinished.
    qint64 execute(const QString &name, const QJsonObject &args = {});
    void approve(qint64 callId, bool allowed);
    void cancel(qint64 callId);
    void cancelAll();

    bool isRunning(qint64 callId) const { return m_running.contains(callId); }
    int runningCount() const { return m_running.size(); }

    int defaultTimeoutMs() const { return m_defaultTimeoutMs; }
    void setDefaultTimeoutMs(int ms); // <= 0 disables the watchdog
    int maxOutputBytes() const { return m_maxOutputBytes; }
    void setMaxOutputBytes(int bytes);

signals:
    void toolStarted(qint64 callId, const QString &name, const QJsonObject &args);
    void toolFinished(qint64 callId, const QString &name, const ToolResult &result);
    void toolOutput(qint64 callId, const QString &name, const QString &stream,
                    const QString &chunk);
    void toolApprovalRequired(qint64 callId, const QString &name,
                              const QJsonObject &args, const QJsonObject &preview,
                              ApprovalLevel level);
    void toolError(qint64 callId, const QString &name, const QString &error);
    void toolRegistered(const QString &name);
    void toolUnregistered(const QString &name);

private slots:
    void onToolFinished(qint64 callId, const ToolResult &result);
    void onToolOutput(qint64 callId, const QString &stream, const QString &chunk);
    void onApprovalDecided(qint64 callId, bool approved);
    void onApprovalCancelled(qint64 callId);
    void onWatchdog(qint64 callId);

private:
    struct PendingCall {
        QString name;
        QJsonObject args;
        Tool *tool = nullptr; // non-owning: registry owns tools
        ApprovalLevel level = ApprovalLevel::ReadOnly;
        bool awaitingApproval = false;
        qint64 startedMs = 0;
    };

    void beginExecution(qint64 callId);
    void finishCall(qint64 callId, ToolResult result);
    void failLater(qint64 callId, const QString &name, const QString &message,
                   const QString &code);
    void stopWatchdog(qint64 callId);

    static bool checkType(const QJsonValue &value, const QString &type);

    QHash<QString, Tool *> m_tools;
    QHash<qint64, PendingCall> m_running;
    QHash<qint64, QTimer *> m_watchdogs; // owned by this registry
    ToolApproval *m_approval = nullptr;  // non-owning (child of this when created)
    qint64 m_nextCallId = 1;
    int m_defaultTimeoutMs = 120000;
    int m_maxOutputBytes = 1024 * 1024;
};

} // namespace ModelBench

Q_DECLARE_METATYPE(ModelBench::ToolResult)
