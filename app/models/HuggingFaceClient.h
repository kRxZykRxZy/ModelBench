#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>
#include <QVariantList>

namespace ModelBench {

class HuggingFaceClient : public QObject {
    Q_OBJECT
public:
    explicit HuggingFaceClient(QObject *parent = nullptr);
    Q_INVOKABLE void setToken(const QString &token);
    Q_INVOKABLE void searchModels(const QString &query, int limit = 20);
    Q_INVOKABLE void fetchModelFiles(const QString &repoId);

signals:
    void searchFinished(const QVariantList &models);
    void searchFailed(const QString &error);
    void filesFinished(const QString &repoId, const QVariantList &files);
    void filesFailed(const QString &repoId, const QString &error);

private:
    QNetworkRequest makeRequest(const QString &url) const;
    QNetworkAccessManager m_nam;
    QString m_token;
};

} // namespace ModelBench
