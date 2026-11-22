#include "MCPRegistry.h"

#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QDebug>

namespace {

QList<ModelFit::MCPServerListing> parseServerListings(const QJsonDocument &doc) {
    QList<ModelFit::MCPServerListing> out;
    QJsonArray arr;
    if (doc.isArray()) {
        arr = doc.array();
    } else if (doc.isObject()) {
        QJsonObject o = doc.object();
        if (o.contains("servers"))
            arr = o["servers"].toArray();
        else if (o.contains("results"))
            arr = o["results"].toArray();
        else if (o.contains("data"))
            arr = o["data"].toArray();
    }
    for (const auto &v : arr) {
        QJsonObject o = v.toObject();
        ModelFit::MCPServerListing s;
        s.id = o.value("id").toString(o.value("name").toString());
        s.name = o.value("name").toString(o.value("title").toString());
        s.description = o.value("description").toString();
        QJsonObject pub = o.value("publisher").toObject();
        s.publisher = pub.value("name").toString(o.value("author").toString());
        QJsonObject repo = o.value("repository").toObject();
        s.repository = repo.value("url").toString(o.value("repository").toString());
        QJsonArray pkgs = o.value("packages").toArray();
        if (!pkgs.isEmpty()) {
            QJsonObject p = pkgs.first().toObject();
            QJsonObject tp = p.value("transport").toObject();
            s.transport = tp.value("type").toString("stdio");
            s.installCommand = p.value("identifier").toString();
        } else {
            s.transport = o.value("transport").toString("stdio");
        }
        out << s;
    }
    return out;
}

} // namespace

namespace ModelFit {

OfficialMCPRegistryProvider::OfficialMCPRegistryProvider(QObject *parent)
    : IMCPRegistryProvider(parent), m_nam(new QNetworkAccessManager(this)) {}

void OfficialMCPRegistryProvider::setBaseUrl(const QString &url) { m_baseUrl = url; }

QNetworkRequest OfficialMCPRegistryProvider::makeRequest(const QString &url) const {
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "ModelFit/0.1");
    req.setRawHeader("Accept", "application/json");
    return req;
}

void OfficialMCPRegistryProvider::searchServers(const QString &query) {
    QUrl url(m_baseUrl + "/v0.1/servers");
    QUrlQuery q;
    if (!query.isEmpty()) q.addQueryItem("search", query);
    q.addQueryItem("limit", "30");
    url.setQuery(q);
    QNetworkReply *reply = m_nam->get(makeRequest(url.toString()));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit searchFailed("MCP registry request failed: " + reply->errorString());
            reply->deleteLater();
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        reply->deleteLater();
        emit searchFinished(parseServerListings(doc));
    });
}

void OfficialMCPRegistryProvider::fetchServerDetails(const QString &serverId) {
    QString url = m_baseUrl + "/v0.1/servers/" + serverId;
    QNetworkReply *reply = m_nam->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, serverId]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit detailsFailed(serverId, reply->errorString());
            reply->deleteLater();
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        reply->deleteLater();
        emit detailsFinished(serverId, doc.isObject() ? doc.object() : QJsonObject());
    });
}

// ---------------------------------------------------------------------------
// Third-party registry provider (BYOK)
// ---------------------------------------------------------------------------

ThirdPartyMCPRegistryProvider::ThirdPartyMCPRegistryProvider(QObject *parent)
    : IMCPRegistryProvider(parent), m_nam(new QNetworkAccessManager(this)) {}

void ThirdPartyMCPRegistryProvider::setBaseUrl(const QString &url) {
    if (m_baseUrl == url) return;
    m_baseUrl = url;
    emit configurationChanged();
}

void ThirdPartyMCPRegistryProvider::setApiKey(const QString &apiKey) {
    if (m_apiKey == apiKey) return;
    m_apiKey = apiKey;
    emit configurationChanged();
}

void ThirdPartyMCPRegistryProvider::setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    emit configurationChanged();
}

QNetworkRequest ThirdPartyMCPRegistryProvider::authedRequest(
    const QString &url) const {
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "ModelFit/0.1");
    req.setRawHeader("Accept", "application/json");
    if (!m_apiKey.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
    return req;
}

void ThirdPartyMCPRegistryProvider::searchServers(const QString &query) {
    if (!m_enabled || m_baseUrl.isEmpty() || m_apiKey.isEmpty()) {
        emit searchFailed(
            "Third-party MCP registry provider is not configured: "
            "set a base URL and API key (BYOK) and enable the provider.");
        return;
    }
    QUrl url(m_baseUrl);
    QUrlQuery q;
    if (!query.isEmpty()) q.addQueryItem("search", query);
    q.addQueryItem("limit", "30");
    url.setQuery(q);
    QNetworkReply *reply = m_nam->get(authedRequest(url.toString()));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit searchFailed("Third-party MCP registry request failed: " +
                              reply->errorString());
            reply->deleteLater();
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        reply->deleteLater();
        emit searchFinished(parseServerListings(doc));
    });
}

void ThirdPartyMCPRegistryProvider::fetchServerDetails(const QString &serverId) {
    if (!m_enabled || m_baseUrl.isEmpty() || m_apiKey.isEmpty()) {
        emit detailsFailed(
            serverId,
            "Third-party MCP registry provider is not configured (BYOK).");
        return;
    }
    QString url = m_baseUrl + "/servers/" + serverId;
    QNetworkReply *reply = m_nam->get(authedRequest(url));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, serverId]() {
                if (reply->error() != QNetworkReply::NoError) {
                    emit detailsFailed(serverId, reply->errorString());
                    reply->deleteLater();
                    return;
                }
                QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                reply->deleteLater();
                emit detailsFinished(serverId,
                                     doc.isObject() ? doc.object()
                                                    : QJsonObject());
            });
}

} // namespace ModelFit

