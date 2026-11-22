#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QVariantMap>

namespace ModelFit {

struct ChatMessage {
    QString id;
    QString conversationId;
    QString role; // user/assistant/system/tool
    QString content;
    qint64 createdAt = 0;
};

class ConversationManager : public QObject {
    Q_OBJECT
public:
    explicit ConversationManager(QObject *parent = nullptr);
    QString createConversation(const QString &title, const QString &modelId);
    void addMessage(const QString &convId, const ChatMessage &msg);
    QList<ChatMessage> messages(const QString &convId) const;
    QStringList conversationIds() const;
    void deleteConversation(const QString &convId);
signals:
    void conversationCreated(const QString &id);
    void messageAdded(const QString &convId, const ChatMessage &msg);
private:
    struct Conv { QString id, title, modelId; QList<ChatMessage> msgs; };
    QList<Conv> m_convs;
};

} // namespace ModelFit
