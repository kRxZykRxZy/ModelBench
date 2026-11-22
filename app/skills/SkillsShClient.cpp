#include "SkillsShClient.h"

#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

namespace ModelFit {

SkillsShClient::SkillsShClient(QObject *parent) : QObject(parent) {}

void SkillsShClient::setBaseUrl(const QString &url) { m_baseUrl = url; }
void SkillsShClient::setVercelOidcToken(const QString &token) { m_token = token; }

QNetworkRequest SkillsShClient::makeRequest(const QString &url) const {
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "ModelFit/0.1");
    req.setRawHeader("Accept", "application/json");
    if (!m_token.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + m_token).toUtf8());
    return req;
}

QString SkillsShClient::httpFailure(QNetworkReply *reply,
                                    const QString &context) const {
    QVariant status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (status.isValid()) {
        int code = status.toInt();
        if (code == 401 || code == 403) {
            return QString("%1: HTTP %2 - authentication required. "
                           "skills.sh requires a Vercel OIDC token on this "
                           "endpoint; call setVercelOidcToken() with a "
                           "short-lived token.")
                .arg(context)
                .arg(code);
        }
        return QString("%1: HTTP %2 (%3)").arg(context).arg(code).arg(
            reply->errorString());
    }
    return QString("%1: %2").arg(context).arg(reply->errorString());
}

static QList<SkillListing> parseListings(const QByteArray &body) {
    QList<SkillListing> out;
    QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isNull()) return out;

    QJsonArray arr;
    if (doc.isArray()) {
        arr = doc.array();
    } else if (doc.isObject()) {
        QJsonObject o = doc.object();
        if (o.contains("results"))
            arr = o["results"].toArray();
        else if (o.contains("skills"))
            arr = o["skills"].toArray();
        else if (o.contains("data"))
            arr = o["data"].toArray();
    }
    for (const auto &v : arr) {
        if (!v.isObject()) continue;
        QJsonObject o = v.toObject();
        SkillListing s;
        s.id = o.value("id").toString(o.value("slug").toString());
        s.name = o.value("name").toString();
        s.description = o.value("description").toString();
        s.publisher = o.value("publisher").toString(o.value("author").toString());
        s.repository = o.value("repository").toString(o.value("repo").toString());
        s.version = o.value("version").toString();
        s.installs = o.value("installs").toVariant().toLongLong();
        out << s;
    }
    return out;
}

void SkillsShClient::search(const QString &query, int limit) {
    QUrl url(m_baseUrl + "/api/search");
    QUrlQuery q;
    if (!query.isEmpty()) q.addQueryItem("q", query);
    q.addQueryItem("limit", QString::number(limit));
    url.setQuery(q);

    QNetworkReply *reply = m_nam.get(makeRequest(url.toString()));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        if (status.isValid() && status.toInt() >= 400) {
            emit searchFailed(httpFailure(reply, "skills.sh search"));
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit searchFailed(httpFailure(reply, "skills.sh search"));
            reply->deleteLater();
            return;
        }
        emit searchFinished(parseListings(reply->readAll()));
        reply->deleteLater();
    });
}

void SkillsShClient::fetchDetails(const QString &skillId) {
    QString url = m_baseUrl + "/api/skills/" + skillId;
    QNetworkReply *reply = m_nam.get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, skillId]() {
        QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        if (status.isValid() && status.toInt() >= 400) {
            emit detailsFailed(skillId, httpFailure(reply, "skills.sh details"));
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit detailsFailed(skillId, httpFailure(reply, "skills.sh details"));
            reply->deleteLater();
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        reply->deleteLater();
        emit detailsFinished(skillId,
                             doc.isObject() ? doc.object() : QJsonObject());
    });
}

void SkillsShClient::fetchSkillContent(const QString &skillId) {
    // Convention: SKILL.md is served under /api/skills/<id>/content.
    // The response may be plain text or JSON {"content": "..."}.
    QString url = m_baseUrl + "/api/skills/" + skillId + "/content";
    QNetworkReply *reply = m_nam.get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, skillId]() {
                QVariant status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
                if (status.isValid() && status.toInt() >= 400) {
                    emit skillContentFailed(
                        skillId, httpFailure(reply, "skills.sh skill content"));
                    reply->deleteLater();
                    return;
                }
                if (reply->error() != QNetworkReply::NoError) {
                    emit skillContentFailed(
                        skillId, httpFailure(reply, "skills.sh skill content"));
                    reply->deleteLater();
                    return;
                }
                QByteArray body = reply->readAll();
                reply->deleteLater();

                QString content = QString::fromUtf8(body);
                QJsonDocument doc = QJsonDocument::fromJson(body);
                if (!doc.isNull() && doc.isObject()) {
                    QJsonObject o = doc.object();
                    if (o.contains("content") && o["content"].isString())
                        content = o["content"].toString();
                }
                emit skillContentFinished(skillId, content);
            });
}

} // namespace ModelFit
