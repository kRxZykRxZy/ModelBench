#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QFile>
#include <QNetworkReply>

namespace ModelFit {

class ModelDownloader : public QObject {
    Q_OBJECT
public:
    explicit ModelDownloader(QObject *parent = nullptr);
    void download(const QString &url, const QString &destPath);
    void cancel();
    bool isRunning() const { return m_running; }

signals:
    void progress(qint64 received, qint64 total, double speedBytesPerSec);
    void finished(const QString &path);
    void failed(const QString &error);

private:
    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    QFile m_file;
    bool m_running = false;
    qint64 m_received = 0;
    qint64 m_total = 0;
};

} // namespace ModelFit
