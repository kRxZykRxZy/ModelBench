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
    Q_INVOKABLE QString createConversation(const QString &title, const QString &modelId = {});
    Q_INVOKABLE void addMessage(const QString &convId, const QString &role, const QString &content);
    Q_INVOKABLE QVariantList getMessages(const QString &convId) const;
    Q_INVOKABLE QStringList conversationIds() const;
    Q_INVOKABLE QVariantList getConversations() const;
    Q_INVOKABLE void deleteConversation(const QString &convId);
    Q_INVOKABLE QString getConversationTitle(const QString &convId) const;
signals:
    void conversationCreated(const QString &id);
    void messageAdded(const QString &convId, const ChatMessage &msg);
private:
    QList<ChatMessage> messages(const QString &convId) const;
    struct Conv { QString id, title, modelId; QList<ChatMessage> msgs; };
    QList<Conv> m_convs;
};

} // namespace ModelFit

Q_DECLARE_METATYPE(ModelFit::ChatMessage)
