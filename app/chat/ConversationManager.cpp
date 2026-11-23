#include "ConversationManager.h"

#include <QUuid>
#include <QDateTime>

namespace ModelBench {

ConversationManager::ConversationManager(QObject *parent) : QObject(parent) {
    qRegisterMetaType<ChatMessage>("ModelBench::ChatMessage");
}

QString ConversationManager::createConversation(const QString &title, const QString &modelId) {
    Conv c;
    c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.title = title.isEmpty() ? QStringLiteral("New chat") : title;
    c.modelId = modelId;
    m_convs << c;
    emit conversationCreated(c.id);
    return c.id;
}

void ConversationManager::addMessage(const QString &convId, const QString &role, const QString &content) {
    ChatMessage msg;
    msg.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    msg.conversationId = convId;
    msg.role = role;
    msg.content = content;
    msg.createdAt = QDateTime::currentSecsSinceEpoch();
    for (auto &c : m_convs) {
        if (c.id == convId) { c.msgs << msg; emit messageAdded(convId, msg); return; }
    }
}

QVariantList ConversationManager::getMessages(const QString &convId) const {
    QVariantList out;
    for (const auto &m : messages(convId)) {
        QVariantMap item;
        item["id"] = m.id;
        item["role"] = m.role;
        item["content"] = m.content;
        out << item;
    }
    return out;
}

QVariantList ConversationManager::getConversations() const {
    QVariantList out;
    for (const auto &c : m_convs) {
        QVariantMap item;
        item["id"] = c.id;
        item["title"] = c.title;
        item["modelId"] = c.modelId;
        item["messageCount"] = c.msgs.size();
        out << item;
    }
    return out;
}

QString ConversationManager::getConversationTitle(const QString &convId) const {
    for (const auto &c : m_convs) if (c.id == convId) return c.title;
    return {};
}

QList<ChatMessage> ConversationManager::messages(const QString &convId) const {
    for (const auto &c : m_convs) if (c.id == convId) return c.msgs;
    return {};
}

QStringList ConversationManager::conversationIds() const {
    QStringList ids;
    for (const auto &c : m_convs) ids << c.id;
    return ids;
}

void ConversationManager::deleteConversation(const QString &convId) {
    m_convs.erase(std::remove_if(m_convs.begin(), m_convs.end(),
        [&](const Conv &c){ return c.id == convId; }), m_convs.end());
}

} // namespace ModelBench
