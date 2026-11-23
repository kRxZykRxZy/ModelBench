#include "ToolRegistry.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QTimer>
#include <cmath>

namespace ModelBench {

Tool::Tool(QObject *parent) : QObject(parent) {}
Tool::~Tool() = default;

ToolRegistry::ToolRegistry(ToolApproval *approval, QObject *parent)
    : QObject(parent) {
    qRegisterMetaType<ToolResult>("ModelBench::ToolResult");
    if (!approval) approval = new ToolApproval(this);
    setApproval(approval);
}

ToolRegistry::~ToolRegistry() = default;

void ToolRegistry::setApproval(ToolApproval *approval) {
    if (m_approval == approval) return;
    if (m_approval) {
        disconnect(m_approval, &ToolApproval::approvalDecided, this,
                   &ToolRegistry::onApprovalDecided);
        disconnect(m_approval, &ToolApproval::approvalCancelled, this,
                   &ToolRegistry::onApprovalCancelled);
    }
    m_approval = approval;
    if (m_approval) {
        connect(m_approval, &ToolApproval::approvalDecided, this,
                &ToolRegistry::onApprovalDecided);
        connect(m_approval, &ToolApproval::approvalCancelled, this,
                &ToolRegistry::onApprovalCancelled);
    }
}

bool ToolRegistry::registerTool(Tool *tool) {
    if (!tool || tool->name().isEmpty()) return false;
    if (m_tools.contains(tool->name())) return false;

    tool->setParent(this);
    m_tools.insert(tool->name(), tool);
    connect(tool, &Tool::finished, this, &ToolRegistry::onToolFinished);
    connect(tool, &Tool::output, this, &ToolRegistry::onToolOutput);
    emit toolRegistered(tool->name());
    return true;
}

bool ToolRegistry::unregisterTool(const QString &name) {
    Tool *tool = m_tools.take(name);
    if (!tool) return false;

    // Fail any calls in flight so agents never wait forever.
    const QList<qint64> ids = m_running.keys();
    for (qint64 id : ids) {
        if (m_running.value(id).name == name) {
            ToolResult r;
            r.ok = false;
            r.toolName = name;
            r.error = QStringLiteral("Tool was unregistered while running");
            r.errorCode = QStringLiteral("tool_unregistered");
            finishCall(id, r);
        }
    }
    disconnect(tool, nullptr, this, nullptr);
    tool->deleteLater();
    emit toolUnregistered(name);
    return true;
}

Tool *ToolRegistry::tool(const QString &name) const {
    return m_tools.value(name, nullptr);
}

QStringList ToolRegistry::toolNames() const {
    QStringList names = m_tools.keys();
    names.sort(Qt::CaseInsensitive);
    return names;
}

QList<Tool *> ToolRegistry::tools() const { return m_tools.values(); }

QJsonArray ToolRegistry::describeTools() const {
    QJsonArray descriptions;
    for (const QString &name : toolNames()) {
        Tool *t = m_tools.value(name);
        QJsonObject entry;
        entry[QStringLiteral("name")] = t->name();
        entry[QStringLiteral("description")] = t->description();
        entry[QStringLiteral("inputSchema")] = t->inputSchema();
        const ApprovalLevel level = t->approvalLevel();
        entry[QStringLiteral("approvalLevel")] = ToolApproval::levelName(level);
        entry[QStringLiteral("requiresApproval")] =
            ToolApproval::requiresApproval(level);
        descriptions.append(entry);
    }
    return descriptions;
}

bool ToolRegistry::checkType(const QJsonValue &value, const QString &type) {
    if (type == QLatin1String("string")) return value.isString();
    if (type == QLatin1String("number")) return value.isDouble();
    if (type == QLatin1String("integer"))
        return value.isDouble() &&
               (value.toDouble() == std::floor(value.toDouble()));
    if (type == QLatin1String("boolean")) return value.isBool();
    if (type == QLatin1String("object")) return value.isObject();
    if (type == QLatin1String("array")) return value.isArray();
    if (type == QLatin1String("null")) return value.isNull();
    // Unknown constraints do not fail validation.
    return true;
}

QJsonObject ToolRegistry::validateArgs(const QJsonObject &schema,
                                       const QJsonObject &args) {
    QJsonObject result;
    result[QStringLiteral("ok")] = true;
    if (schema.isEmpty()) return result;

    const QString type = schema.value(QStringLiteral("type")).toString();
    if (!type.isEmpty() && !checkType(args, type)) {
        result[QStringLiteral("ok")] = false;
        result[QStringLiteral("error")] =
            QStringLiteral("arguments are not of type %1").arg(type);
        return result;
    }

    const QJsonArray required = schema.value(QStringLiteral("required")).toArray();
    QStringList missing;
    for (const QJsonValue &r : required) {
        if (!args.contains(r.toString())) missing << r.toString();
    }
    if (!missing.isEmpty()) {
        result[QStringLiteral("ok")] = false;
        result[QStringLiteral("error")] =
            QStringLiteral("missing required properties: %1").arg(missing.join(u", "));
        return result;
    }

    const QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
    for (auto it = properties.constBegin(); it != properties.constEnd(); ++it) {
        const QString &key = it.key();
        if (!args.contains(key)) continue;

        const QJsonValue value = args.value(key);
        const QJsonObject propSchema = it.value().toObject();
        const QString ptype = propSchema.value(QStringLiteral("type")).toString();

        if (!ptype.isEmpty() && !checkType(value, ptype)) {
            result[QStringLiteral("ok")] = false;
            result[QStringLiteral("error")] =
                QStringLiteral("property '%1' is not of type %2").arg(key, ptype);
            return result;
        }
        if (propSchema.contains(QStringLiteral("enum"))) {
            if (!propSchema.value(QStringLiteral("enum")).toArray().contains(value)) {
                result[QStringLiteral("ok")] = false;
                result[QStringLiteral("error")] =
                    QStringLiteral("property '%1' is not an allowed value").arg(key);
                return result;
            }
        }
        if (ptype == QLatin1String("object") &&
            propSchema.contains(QStringLiteral("properties")) && value.isObject()) {
            const QJsonObject nested = validateArgs(propSchema, value.toObject());
            if (!nested[QStringLiteral("ok")].toBool()) {
                result[QStringLiteral("ok")] = false;
                result[QStringLiteral("error")] =
                    QStringLiteral("property '%1': %2")
                        .arg(key, nested[QStringLiteral("error")].toString());
                return result;
            }
        }
    }
    return result;
}

qint64 ToolRegistry::execute(const QString &name, const QJsonObject &args) {
    const qint64 id = m_nextCallId++;

    PendingCall call;
    call.name = name;
    call.args = args;
    call.startedMs = QDateTime::currentMSecsSinceEpoch();
    m_running.insert(id, call);

    Tool *t = m_tools.value(name, nullptr);
    if (!t) {
        failLater(id, name,
                  QStringLiteral("Unknown tool: %1").arg(name),
                  QStringLiteral("unknown_tool"));
        return id;
    }
    call.tool = t;
    m_running.insert(id, call);

    const QJsonObject validation = validateArgs(t->inputSchema(), args);
    if (!validation[QStringLiteral("ok")].toBool()) {
        failLater(id, name, validation[QStringLiteral("error")].toString(),
                  QStringLiteral("validation_error"));
        return id;
    }

    call.level = t->approvalLevelFor(args);
    m_running.insert(id, call);

    emit toolStarted(id, name, args);

    if (m_approval && ToolApproval::requiresApproval(call.level)) {
        call.awaitingApproval = true;
        m_running.insert(id, call);
        const QJsonObject preview =
            ToolApproval::makePreview(name, args, call.level);
        if (m_approval->mode() == ApprovalMode::Prompt)
            emit toolApprovalRequired(id, name, args, preview, call.level);
        m_approval->request(id, name, args, call.level);
        return id;
    }

    beginExecution(id);
    return id;
}

void ToolRegistry::beginExecution(qint64 callId) {
    auto it = m_running.find(callId);
    if (it == m_running.end()) return; // cancelled while awaiting approval
    it->awaitingApproval = false;
    Tool *t = it->tool;
    const QJsonObject args = it->args;

    if (m_defaultTimeoutMs > 0) {
        QTimer *timer = new QTimer(this);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, [this, callId]() {
            onWatchdog(callId);
        });
        m_watchdogs.insert(callId, timer);
        timer->start(m_defaultTimeoutMs);
    }

    // execute() may complete synchronously; subscribers use queued
    // connections (or can rely on finishCall emitting after this returns).
    t->execute(callId, args);
}

void ToolRegistry::finishCall(qint64 callId, ToolResult result) {
    auto it = m_running.find(callId);
    if (it == m_running.end()) return; // already finished / cancelled
    const PendingCall call = *it;
    m_running.erase(it);
    stopWatchdog(callId);

    result.toolName = call.name;
    if (result.durationMs == 0) {
        result.durationMs = QDateTime::currentMSecsSinceEpoch() - call.startedMs;
    }
    emit toolFinished(callId, call.name, result);
}

void ToolRegistry::failLater(qint64 callId, const QString &name,
                             const QString &message, const QString &code) {
    QMetaObject::invokeMethod(
        this,
        [this, callId, name, message, code]() {
            ToolResult r;
            r.ok = false;
            r.toolName = name;
            r.error = message;
            r.errorCode = code;
            finishCall(callId, r);
        },
        Qt::QueuedConnection);
}

void ToolRegistry::onToolFinished(qint64 callId, const ToolResult &result) {
    finishCall(callId, result);
}

void ToolRegistry::onToolOutput(qint64 callId, const QString &stream,
                                const QString &chunk) {
    auto it = m_running.find(callId);
    if (it == m_running.end()) return;
    emit toolOutput(callId, it->name, stream, chunk);
}

void ToolRegistry::onApprovalDecided(qint64 callId, bool approved) {
    auto it = m_running.find(callId);
    if (it == m_running.end() || !it->awaitingApproval) return;

    if (!approved) {
        ToolResult r;
        r.ok = false;
        r.error = QStringLiteral("Tool call denied by user");
        r.errorCode = QStringLiteral("denied");
        r.cancelled = true;
        finishCall(callId, r);
        return;
    }
    beginExecution(callId);
}

void ToolRegistry::onApprovalCancelled(qint64 callId) {
    auto it = m_running.find(callId);
    if (it == m_running.end() || !it->awaitingApproval) return;
    ToolResult r;
    r.ok = false;
    r.error = QStringLiteral("Tool call cancelled");
    r.errorCode = QStringLiteral("cancelled");
    r.cancelled = true;
    finishCall(callId, r);
}

void ToolRegistry::onWatchdog(qint64 callId) {
    auto it = m_running.find(callId);
    if (it == m_running.end() || it->awaitingApproval) return;
    if (it->tool) it->tool->cancel(callId);

    ToolResult r;
    r.ok = false;
    r.error = QStringLiteral("Tool execution timed out");
    r.errorCode = QStringLiteral("timeout");
    r.cancelled = true;
    finishCall(callId, r);
}

void ToolRegistry::approve(qint64 callId, bool allowed) {
    if (m_approval) m_approval->decide(callId, allowed);
}

void ToolRegistry::cancel(qint64 callId) {
    auto it = m_running.find(callId);
    if (it == m_running.end()) return;
    if (it->awaitingApproval) {
        if (m_approval) m_approval->cancel(callId);
        return;
    }
    if (it->tool) it->tool->cancel(callId);
}

void ToolRegistry::cancelAll() {
    const QList<qint64> ids = m_running.keys();
    for (qint64 id : ids) cancel(id);
}

void ToolRegistry::stopWatchdog(qint64 callId) {
    auto it = m_watchdogs.find(callId);
    if (it == m_watchdogs.end()) return;
    QTimer *timer = *it;
    m_watchdogs.erase(it);
    timer->stop();
    timer->deleteLater();
}

void ToolRegistry::setDefaultTimeoutMs(int ms) {
    m_defaultTimeoutMs = ms > 0 ? ms : 0;
}

void ToolRegistry::setMaxOutputBytes(int bytes) {
    if (bytes > 0) m_maxOutputBytes = bytes;
}

} // namespace ModelBench
