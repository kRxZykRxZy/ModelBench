#include "ModelDownloader.h"
#include "core/PathSecurity.h"
#include "core/Paths.h"

#include <QNetworkRequest>
#include <QUrl>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

namespace ModelBench {

ModelDownloader::ModelDownloader(QObject *parent) : QObject(parent) {}

QString ModelDownloader::modelsDirectory() const {
    return Paths::modelsDirectory();
}

void ModelDownloader::download(const QString &url, const QString &destPath) {
    if (m_running) return;
    m_running = true;
    m_received = 0;
    m_file.setFileName(destPath);
    qint64 existing = 0;
    QFileInfo fi(destPath);
    if (fi.exists()) existing = fi.size();
    QDir().mkpath(fi.absolutePath());
    if (!m_file.open(QIODevice::Append | QIODevice::WriteOnly)) {
        m_running = false;
        emit failed("Cannot open destination file");
        return;
    }
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "ModelBench/0.1");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    if (existing > 0) req.setRawHeader("Range", ("bytes=" + QString::number(existing) + "-").toUtf8());
    m_reply = m_nam.get(req);
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 rx, qint64 total) {
        m_received = rx;
        m_total = total > 0 ? total : m_total;
        emit progress(m_received, m_total, 0.0);
    });
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (m_file.isOpen()) m_file.write(m_reply->readAll());
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, destPath]() {
        if (m_file.isOpen()) m_file.close();
        if (m_reply->error() != QNetworkReply::NoError &&
            m_reply->error() != QNetworkReply::OperationCanceledError) {
            emit failed(m_reply->errorString());
        } else {
            emit finished(destPath);
        }
        m_reply->deleteLater();
        m_reply = nullptr;
        m_running = false;
    });
}

void ModelDownloader::cancel() {
    if (m_reply) m_reply->abort();
    if (m_file.isOpen()) m_file.close();
    m_running = false;
}

} // namespace ModelBench
