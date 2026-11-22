#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>

namespace ModelFit {

struct HFModel {
    QString id;
    QString author;
    QString sha;
    qint64 downloads = 0;
    qint64 likes = 0;
    QStringList tags;
    QString pipelineTag;
    QString lastModified;
};

class HuggingFaceClient : public QObject {
    Q_OBJECT
public:
    explicit HuggingFaceClient(QObject *parent = nullptr);
    void setToken(const QString &token);
    void searchModels(const QString &query, int limit = 20);
    void fetchModelFiles(const QString &repoId);

signals:
    void searchFinished(const QList<HFModel> &models);
    void searchFailed(const QString &error);
    void filesFinished(const QString &repoId, const QJsonArray &files);
    void filesFailed(const QString &repoId, const QString &error);

private:
    QNetworkRequest makeRequest(const QString &url) const;
    QNetworkAccessManager m_nam;
    QString m_token;
};

} // namespace ModelFit
