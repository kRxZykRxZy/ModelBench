#include "ConversationManager.h"

#include <QUuid>
#include <QDateTime>

namespace ModelFit {

ConversationManager::ConversationManager(QObject *parent) : QObject(parent) {}

QString ConversationManager::createConversation(const QString &title, const QString &modelId) {
    Conv c;
    c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.title = title;
    c.modelId = modelId;
    m_convs << c;
    emit conversationCreated(c.id);
    return c.id;
}

void ConversationManager::addMessage(const QString &convId, const ChatMessage &msg) {
    for (auto &c : m_convs) {
        if (c.id == convId) { c.msgs << msg; emit messageAdded(convId, msg); return; }
    }
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

} // namespace ModelFit
