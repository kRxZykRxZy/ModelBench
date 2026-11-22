#include "ToolCalling.h"
#include "MCPClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <cmath>

namespace ModelFit {

ToolCalling::ToolCalling(QObject *parent) : QObject(parent) {
    qRegisterMetaType<ToolCallResult>("ModelFit::ToolCallResult");
}

ToolCalling::~ToolCalling() = default;

void ToolCalling::setClient(MCPClient *client, const QString &serverId) {
    if (m_client) {
        disconnect(m_client, &MCPClient::toolResult, this,
                   &ToolCalling::onClientToolResult);
        disconnect(m_client, &MCPClient::toolError, this,
                   &ToolCalling::onClientToolError);
        disconnect(m_client, &MCPClient::toolCallDenied, this,
                   &ToolCalling::onClientToolCallDenied);
    }
    m_client = client;
    m_serverId = serverId;
    if (m_client) {
        connect(m_client, &MCPClient::toolResult, this,
                &ToolCalling::onClientToolResult);
        connect(m_client, &MCPClient::toolError, this,
                &ToolCalling::onClientToolError);
        connect(m_client, &MCPClient::toolCallDenied, this,
                &ToolCalling::onClientToolCallDenied);
    }
}

void ToolCalling::setServerId(const QString &serverId) { m_serverId = serverId; }

void ToolCalling::setDefaultTimeoutMs(int ms) {
    if (ms > 0) m_defaultTimeoutMs = ms;
}

void ToolCalling::setMaxOutputBytes(qint64 bytes) {
    if (bytes > 0) m_maxOutputBytes = bytes;
}

bool ToolCalling::checkType(const QJsonValue &value, const QString &type) {
    if (type == "string") return value.isString();
    if (type == "number") return value.isDouble();
    if (type == "integer")
        return value.isDouble() &&
               (value.toDouble() == std::floor(value.toDouble()));
    if (type == "boolean") return value.isBool();
    if (type == "object") return value.isObject();
    if (type == "array") return value.isArray();
    if (type == "null") return value.isNull();
    // Unknown/unsupported type constraint: do not fail validation on it.
    return true;
}

QJsonObject ToolCalling::validateArgs(const QJsonObject &inputSchema,
                                      const QJsonObject &args) {
    QJsonObject result;
    result["ok"] = true;

    if (inputSchema.isEmpty()) return result;

    QString type = inputSchema.value("type").toString();
    if (!type.isEmpty() && !checkType(args, type)) {
        result["ok"] = false;
        result["error"] =
            QString("arguments are not of type %1").arg(type);
        return result;
    }

    QJsonArray required = inputSchema.value("required").toArray();
    QStringList missing;
    for (const QJsonValue &r : required) {
        QString key = r.toString();
        if (!args.contains(key)) missing << key;
    }
    if (!missing.isEmpty()) {
        result["ok"] = false;
        result["error"] =
            QString("missing required properties: %1").arg(missing.join(", "));
        return result;
    }

    QJsonObject properties = inputSchema.value("properties").toObject();
    for (auto it = properties.constBegin(); it != properties.constEnd(); ++it) {
        const QString &key = it.key();
        if (!args.contains(key)) continue;

        QJsonValue val = args.value(key);
        QJsonObject propSchema = it.value().toObject();

        QString ptype = propSchema.value("type").toString();
        if (!ptype.isEmpty() && !checkType(val, ptype)) {
            result["ok"] = false;
            result["error"] =
                QString("property '%1' is not of type %2").arg(key, ptype);
            return result;
        }

        if (propSchema.contains("enum")) {
            QJsonArray en = propSchema.value("enum").toArray();
            if (!en.contains(val)) {
                result["ok"] = false;
                result["error"] =
                    QString("property '%1' is not one of the allowed enum values")
                        .arg(key);
                return result;
            }
        }

        // Recurse into nested objects that declare their own properties.
        if (ptype == "object" && propSchema.contains("properties") &&
            val.isObject()) {
            QJsonObject nested =
                validateArgs(propSchema, val.toObject());
            if (!nested["ok"].toBool()) {
                result["ok"] = false;
                result["error"] =
                    QString("property '%1': %2")
                        .arg(key, nested["error"].toString());
                return result;
            }
        }
    }

    return result;
}

void ToolCalling::call(const QString &toolName,
                       const QJsonObject &args) {
    ToolCallResult result;
    result.ok = false;
    result.serverId = m_serverId;
    result.toolName = toolName;

    // Schema validation (subset: required, type, enum).
    QJsonObject schema;
    if (m_client) {
        for (const MCPTool &t : m_client->tools()) {
            if (t.name == toolName) {
                schema = t.inputSchema;
                break;
            }
        }
    }
    if (!schema.isEmpty()) {
        QJsonObject validation = validateArgs(schema, args);
        if (!validation["ok"].toBool()) {
            QString err = validation["error"].toString();
            emit validationError(toolName, err);
            result.error = err;
            emit toolCallFinished(m_serverId, toolName, result);
            return;
        }
    }

    if (!m_client) {
        result.error = "No MCP client configured";
        emit toolCallFinished(m_serverId, toolName, result);
        return;
    }

    PendingCall pc;
    pc.toolName = toolName;
    pc.args = args;
    pc.startedMs = QDateTime::currentMSecsSinceEpoch();
    m_pendingByTool[toolName].append(pc);

    emit toolCallStarted(m_serverId, toolName, args);

    // Enters the MCPClient approval gate: the actual JSON-RPC call is only
    // made after the UI calls MCPClient::approveToolCall(requestId, true).
    m_client->callTool(toolName, args);
}

void ToolCalling::onClientToolResult(qint64 /*callId*/, const QString &name,
                                     const QJsonObject &result) {
    auto it = m_pendingByTool.find(name);
    if (it == m_pendingByTool.end() || it->isEmpty()) {
        // Unsolicited result (no matching call in flight).
        return;
    }
    PendingCall pc = it->takeFirst();
    if (it->isEmpty()) m_pendingByTool.erase(it);

    ToolCallResult r;
    r.ok = true;
    r.serverId = m_serverId;
    r.toolName = name;
    r.output = result;
    r.durationMs =
        QDateTime::currentMSecsSinceEpoch() - pc.startedMs;

    QByteArray bytes = QJsonDocument(result).toJson(QJsonDocument::Compact);
    if (bytes.size() > m_maxOutputBytes) {
        r.truncated = true;
        r.output = QJsonObject({
            {"truncated", true},
            {"originalSize", (double)bytes.size()},
            {"note", "tool output exceeded the configured size cap"},
        });
    }

    emit toolCallFinished(m_serverId, name, r);
}

void ToolCalling::onClientToolError(qint64 /*callId*/, const QString &name,
                                    const QString &error) {
    auto it = m_pendingByTool.find(name);
    if (it == m_pendingByTool.end() || it->isEmpty()) return;
    PendingCall pc = it->takeFirst();
    if (it->isEmpty()) m_pendingByTool.erase(it);

    ToolCallResult r;
    r.ok = false;
    r.serverId = m_serverId;
    r.toolName = name;
    r.error = error;
    r.durationMs =
        QDateTime::currentMSecsSinceEpoch() - pc.startedMs;

    emit toolCallFinished(m_serverId, name, r);
}

void ToolCalling::onClientToolCallDenied(qint64 /*requestId*/,
                                         const QString &name) {
    auto it = m_pendingByTool.find(name);
    if (it == m_pendingByTool.end() || it->isEmpty()) return;
    PendingCall pc = it->takeFirst();
    if (it->isEmpty()) m_pendingByTool.erase(it);

    ToolCallResult r;
    r.ok = false;
    r.serverId = m_serverId;
    r.toolName = name;
    r.error = "Tool call denied by user";
    r.durationMs =
        QDateTime::currentMSecsSinceEpoch() - pc.startedMs;

    emit toolCallFinished(m_serverId, name, r);
}

} // namespace ModelFit
