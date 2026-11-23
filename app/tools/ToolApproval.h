#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QList>
#include <QStringList>

namespace ModelBench {

// How risky a tool call is. ReadOnly calls run without user confirmation;
// every other level must pass the approval gate.
enum class ApprovalLevel {
    ReadOnly,   // informational reads, auto-approved
    Write,      // creates / modifies / deletes files
    Execute,    // runs a local command
    Network,    // performs network I/O (git push / pull)
    Dangerous,  // destructive or privileged operation (force push, etc.)
};

// Behaviour of the gate when a call needs confirmation.
enum class ApprovalMode {
    Prompt,       // emit approvalRequested and wait for decide()
    AutoApprove,  // approve everything (test / fully trusted contexts)
    AutoDeny,     // deny everything that requires approval
};

// Central approval gate. Lives between the tool registry and the UI: the
// registry asks for a decision before executing privileged tools; the UI
// shows the preview and calls decide(). No tool can execute while its
// request is pending.
class ToolApproval : public QObject {
    Q_OBJECT
    Q_ENUM(ApprovalLevel)
    Q_ENUM(ApprovalMode)
    Q_PROPERTY(ApprovalMode mode READ mode WRITE setMode NOTIFY modeChanged)

public:
    explicit ToolApproval(QObject *parent = nullptr);
    ~ToolApproval() override;

    ApprovalMode mode() const { return m_mode; }
    void setMode(ApprovalMode mode);

    static bool requiresApproval(ApprovalLevel level);
    static QString levelName(ApprovalLevel level);

    // Human-readable, sanitized argument preview for confirmation dialogs.
    // Secret-looking keys are redacted and long values truncated.
    static QJsonObject makePreview(const QString &toolName,
                                   const QJsonObject &args,
                                   ApprovalLevel level);

    // Registers a pending request. In Auto* modes the decision is delivered
    // via a queued approvalDecided signal; in Prompt mode the UI must call
    // decide() in response to approvalRequested.
    void request(qint64 callId, const QString &toolName,
                 const QJsonObject &args, ApprovalLevel level);

    void decide(qint64 callId, bool approved);
    void cancel(qint64 callId);
    void cancelAll();

    bool isPending(qint64 callId) const { return m_pending.contains(callId); }
    QList<qint64> pendingIds() const { return m_pending.keys(); }

    // Remember a UI decision for the whole session ("always allow").
    Q_INVOKABLE void setAlwaysAllowed(const QString &toolName, bool allowed);
    Q_INVOKABLE bool isAlwaysAllowed(const QString &toolName) const;
    Q_INVOKABLE void clearAlwaysAllowed();

    // Path-scoped grants: e.g. allow write_file only under "C:/repo/src/*".
    // Matches the first path-like argument (path, cwd, file).
    Q_INVOKABLE void setScopedAllowance(const QString &toolName,
                                        const QString &pathGlob, bool allowed);
    Q_INVOKABLE bool isScopeAllowed(const QString &toolName,
                                    const QString &path) const;
    QJsonArray permissionRules() const;
    Q_INVOKABLE void clearPermissions();

    // Audit trail of every decision, including automatic ones.
    QJsonArray approvalLog() const;
    Q_INVOKABLE void clearApprovalLog();

signals:
    void modeChanged(ApprovalMode mode);
    void approvalRequested(qint64 callId, const QString &toolName,
                           const QJsonObject &args, const QJsonObject &preview,
                           ApprovalLevel level);
    void approvalDecided(qint64 callId, bool approved);
    void approvalCancelled(qint64 callId);

private:
    struct Pending {
        QString toolName;
        QJsonObject args;
        ApprovalLevel level = ApprovalLevel::ReadOnly;
    };
    struct ScopeRule {
        QString toolName;
        QString pathGlob;
    };

    void autoDecide(qint64 callId, bool approved);
    void logDecision(qint64 callId, const Pending &pending,
                     const QString &decision);
    static QString pathFromArgs(const QJsonObject &args);

    QHash<qint64, Pending> m_pending;
    ApprovalMode m_mode = ApprovalMode::Prompt;
    QStringList m_alwaysAllowed;
    QList<ScopeRule> m_scopeRules;
    QJsonArray m_log;
    int m_logLimit = 200;
};

} // namespace ModelBench
