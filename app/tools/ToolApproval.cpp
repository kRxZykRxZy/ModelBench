#include "ToolApproval.h"

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QMetaObject>

namespace ModelBench {

ToolApproval::ToolApproval(QObject *parent) : QObject(parent) {}
ToolApproval::~ToolApproval() = default;

void ToolApproval::setMode(ApprovalMode mode) {
    if (m_mode == mode) return;
    m_mode = mode;
    emit modeChanged(mode);
}

bool ToolApproval::requiresApproval(ApprovalLevel level) {
    return level != ApprovalLevel::ReadOnly;
}

QString ToolApproval::levelName(ApprovalLevel level) {
    switch (level) {
    case ApprovalLevel::ReadOnly:  return QStringLiteral("read-only");
    case ApprovalLevel::Write:     return QStringLiteral("write");
    case ApprovalLevel::Execute:   return QStringLiteral("execute");
    case ApprovalLevel::Network:   return QStringLiteral("network");
    case ApprovalLevel::Dangerous: return QStringLiteral("dangerous");
    }
    return QStringLiteral("unknown");
}

QJsonObject ToolApproval::makePreview(const QString &toolName,
                                      const QJsonObject &args,
                                      ApprovalLevel level) {
    static const QStringList kSecretHints{
        QStringLiteral("token"),     QStringLiteral("password"),
        QStringLiteral("secret"),    QStringLiteral("apikey"),
        QStringLiteral("api_key"),   QStringLiteral("authorization"),
        QStringLiteral("credential"), QStringLiteral("privatekey"),
    };

    QJsonObject safeArgs;
    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        const QString key = it.key();
        const QJsonValue value = it.value();

        bool secret = false;
        for (const QString &hint : kSecretHints) {
            if (key.contains(hint, Qt::CaseInsensitive)) {
                secret = true;
                break;
            }
        }
        if (secret) {
            safeArgs[key] = QStringLiteral("<redacted>");
            continue;
        }

        if (value.isString()) {
            const QString text = value.toString();
            if (text.size() > 400) {
                safeArgs[key] = text.left(400) +
                    QStringLiteral(" \u2026 [%1 characters]").arg(text.size());
            } else {
                safeArgs[key] = text;
            }
        } else if (value.isArray()) {
            QJsonArray arr = value.toArray();
            if (arr.size() > 24) {
                QJsonArray trimmed;
                for (int i = 0; i < 24; ++i) trimmed.append(arr.at(i));
                trimmed.append(QStringLiteral("... %1 more").arg(arr.size() - 24));
                safeArgs[key] = trimmed;
            } else {
                safeArgs[key] = value;
            }
        } else {
            safeArgs[key] = value;
        }
    }

    QJsonObject preview;
    preview[QStringLiteral("tool")] = toolName;
    preview[QStringLiteral("approvalLevel")] = levelName(level);
    preview[QStringLiteral("requiresApproval")] = requiresApproval(level);
    preview[QStringLiteral("args")] = safeArgs;
    return preview;
}

void ToolApproval::request(qint64 callId, const QString &toolName,
                           const QJsonObject &args, ApprovalLevel level) {
    Pending pending;
    pending.toolName = toolName;
    pending.args = args;
    pending.level = level;
    m_pending.insert(callId, pending);

    if (!requiresApproval(level)) {
        autoDecide(callId, true);
        return;
    }
    if (m_alwaysAllowed.contains(toolName)) {
        autoDecide(callId, true);
        return;
    }
    const QString path = pathFromArgs(args);
    if (!path.isEmpty() && isScopeAllowed(toolName, path)) {
        autoDecide(callId, true);
        return;
    }

    switch (m_mode) {
    case ApprovalMode::AutoApprove:
        autoDecide(callId, true);
        break;
    case ApprovalMode::AutoDeny:
        autoDecide(callId, false);
        break;
    case ApprovalMode::Prompt:
        emit approvalRequested(callId, toolName, args,
                               makePreview(toolName, args, level), level);
        break;
    }
}

void ToolApproval::logDecision(qint64 callId, const Pending &pending,
                               const QString &decision) {
    QJsonObject entry;
    entry[QStringLiteral("timestamp")] = QDateTime::currentMSecsSinceEpoch();
    entry[QStringLiteral("callId")] = callId;
    entry[QStringLiteral("tool")] = pending.toolName;
    entry[QStringLiteral("level")] = levelName(pending.level);
    entry[QStringLiteral("decision")] = decision;
    entry[QStringLiteral("path")] = pathFromArgs(pending.args);
    m_log.append(entry);
    while (m_log.size() > m_logLimit) m_log.removeAt(0);
}

QString ToolApproval::pathFromArgs(const QJsonObject &args) {
    static const QStringList keys{QStringLiteral("path"), QStringLiteral("cwd"),
                                  QStringLiteral("file"), QStringLiteral("uri")};
    for (const QString &key : keys) {
        if (args.contains(key) && args.value(key).isString())
            return args.value(key).toString();
    }
    return {};
}

void ToolApproval::autoDecide(qint64 callId, bool approved) {
    const auto it = m_pending.constFind(callId);
    if (it != m_pending.constEnd())
        logDecision(callId, *it, approved ? QStringLiteral("auto-approved")
                                          : QStringLiteral("auto-denied"));

    // Always asynchronous, so callers can finish setting up state before the
    // decision is observed (avoids re-entrancy inside registry.execute()).
    QMetaObject::invokeMethod(
        this,
        [this, callId, approved]() { emit approvalDecided(callId, approved); },
        Qt::QueuedConnection);
}

void ToolApproval::decide(qint64 callId, bool approved) {
    auto it = m_pending.find(callId);
    if (it == m_pending.end()) return;
    const Pending pending = *it;
    m_pending.erase(it);
    logDecision(callId, pending, approved ? QStringLiteral("approved")
                                          : QStringLiteral("denied"));
    emit approvalDecided(callId, approved);
}

void ToolApproval::cancel(qint64 callId) {
    auto it = m_pending.find(callId);
    if (it == m_pending.end()) return;
    const Pending pending = *it;
    m_pending.erase(it);
    logDecision(callId, pending, QStringLiteral("cancelled"));
    emit approvalCancelled(callId);
}

void ToolApproval::cancelAll() {
    const QList<qint64> ids = m_pending.keys();
    for (qint64 id : ids) cancel(id);
}

void ToolApproval::setAlwaysAllowed(const QString &toolName, bool allowed) {
    if (allowed) {
        if (!m_alwaysAllowed.contains(toolName)) m_alwaysAllowed.append(toolName);
    } else {
        m_alwaysAllowed.removeAll(toolName);
    }
}

bool ToolApproval::isAlwaysAllowed(const QString &toolName) const {
    return m_alwaysAllowed.contains(toolName);
}

void ToolApproval::clearAlwaysAllowed() { m_alwaysAllowed.clear(); }

void ToolApproval::setScopedAllowance(const QString &toolName,
                                      const QString &pathGlob, bool allowed) {
    for (int i = m_scopeRules.size() - 1; i >= 0; --i) {
        if (m_scopeRules.at(i).toolName == toolName &&
            m_scopeRules.at(i).pathGlob == pathGlob)
            m_scopeRules.removeAt(i);
    }
    if (allowed && !toolName.isEmpty() && !pathGlob.isEmpty()) {
        ScopeRule rule;
        rule.toolName = toolName;
        rule.pathGlob = pathGlob;
        m_scopeRules.append(rule);
    }
}

bool ToolApproval::isScopeAllowed(const QString &toolName,
                                  const QString &path) const {
    if (toolName.isEmpty() || path.isEmpty()) return false;
    const QString clean = QDir::fromNativeSeparators(QDir::cleanPath(path));
    for (const ScopeRule &rule : m_scopeRules) {
        if (rule.toolName != toolName) continue;
        if (QDir::match(rule.pathGlob, clean)) return true;
        // Allow globs written relative to the clean path as well.
        if (QDir::match(QDir::fromNativeSeparators(QDir::cleanPath(rule.pathGlob)),
                        clean))
            return true;
    }
    return false;
}

QJsonArray ToolApproval::permissionRules() const {
    QJsonArray rules;
    for (const ScopeRule &rule : m_scopeRules) {
        QJsonObject entry;
        entry[QStringLiteral("tool")] = rule.toolName;
        entry[QStringLiteral("pathGlob")] = rule.pathGlob;
        rules.append(entry);
    }
    for (const QString &tool : m_alwaysAllowed) {
        QJsonObject entry;
        entry[QStringLiteral("tool")] = tool;
        entry[QStringLiteral("pathGlob")] = QStringLiteral("*");
        rules.append(entry);
    }
    return rules;
}

void ToolApproval::clearPermissions() {
    m_alwaysAllowed.clear();
    m_scopeRules.clear();
}

QJsonArray ToolApproval::approvalLog() const { return m_log; }

void ToolApproval::clearApprovalLog() { m_log = QJsonArray(); }

} // namespace ModelBench
