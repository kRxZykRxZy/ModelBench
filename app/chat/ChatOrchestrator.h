#pragma once

#include <QObject>
#include <QString>

namespace ModelFit {

class ChatOrchestrator : public QObject {
    Q_OBJECT
public:
    explicit ChatOrchestrator(QObject *parent = nullptr);
    void sendMessage(const QString &conversationId, const QString &text);
signals:
    void tokenReceived(const QString &conversationId, const QString &token);
    void generationFinished(const QString &conversationId);
    void generationError(const QString &conversationId, const QString &error);
};

} // namespace ModelFit
