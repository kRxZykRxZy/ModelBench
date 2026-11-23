#pragma once

#include <QObject>
#include <QString>

namespace ModelBench {

class LocalApiServer : public QObject {
    Q_OBJECT
public:
    explicit LocalApiServer(QObject *parent = nullptr);
    bool start(const QString &host = "127.0.0.1", quint16 port = 8080);
    void stop();
    bool isRunning() const { return m_running; }
    QString endpoint() const { return m_endpoint; }
signals:
    void started(const QString &endpoint);
    void stopped();
private:
    bool m_running = false;
    QString m_endpoint;
};

} // namespace ModelBench
