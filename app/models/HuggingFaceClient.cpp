#include "HuggingFaceClient.h"

#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>

namespace ModelFit {

HuggingFaceClient::HuggingFaceClient(QObject *parent) : QObject(parent) {}

void HuggingFaceClient::setToken(const QString &token) { m_token = token; }

QNetworkRequest HuggingFaceClient::makeRequest(const QString &url) const {
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "ModelFit/0.1");
    req.setRawHeader("Accept", "application/json");
    if (!m_token.isEmpty()) req.setRawHeader("Authorization", ("Bearer " + m_token).toUtf8());
    return req;
}

void HuggingFaceClient::searchModels(const QString &query, int limit) {
    QUrl url("https://huggingface.co/api/models");
    QUrlQuery q;
    if (!query.isEmpty()) q.addQueryItem("search", query);
    q.addQueryItem("filter", "gguf");
    q.addQueryItem("limit", QString::number(limit));
    q.addQueryItem("sort", "downloads");
    q.addQueryItem("direction", "-1");
    url.setQuery(q);
    QNetworkReply *reply = m_nam.get(makeRequest(url.toString()));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit searchFailed(reply->errorString());
            reply->deleteLater();
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        reply->deleteLater();
        QList<HFModel> out;
        if (doc.isArray()) {
            for (const auto &v : doc.array()) {
                QJsonObject o = v.toObject();
                HFModel m;
                m.id = o.value("id").toString();
                m.author = o.value("author").toString();
                m.sha = o.value("sha").toString();
                m.downloads = o.value("downloads").toVariant().toLongLong();
                m.likes = o.value("likes").toVariant().toLongLong();
                m.pipelineTag = o.value("pipeline_tag").toString();
                m.lastModified = o.value("lastModified").toString();
                for (const auto &t : o.value("tags").toArray()) m.tags << t.toString();
                out << m;
            }
        }
        emit searchFinished(out);
    });
}

void HuggingFaceClient::fetchModelFiles(const QString &repoId) {
    QString url = "https://huggingface.co/api/models/" + repoId + "/tree/main";
    QNetworkReply *reply = m_nam.get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, repoId]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit filesFailed(repoId, reply->errorString());
            reply->deleteLater();
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        reply->deleteLater();
        QJsonArray arr = doc.isArray() ? doc.array() : QJsonArray();
        emit filesFinished(repoId, arr);
    });
}

} // namespace ModelFit
