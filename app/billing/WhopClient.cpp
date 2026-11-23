#include "WhopClient.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>

namespace ModelBench {

WhopClient::WhopClient(QObject *parent) : QObject(parent) {}

void WhopClient::setBackendBaseUrl(const QString &url) { m_backendBaseUrl = url; }

void WhopClient::createCheckoutSession(const QString &planId, const QString &userId) {
    if (m_backendBaseUrl.isEmpty()) {
        emit checkoutFailed("Billing backend URL not configured. Set it in Settings.");
        return;
    }
    QNetworkAccessManager *nam = new QNetworkAccessManager(this);
    QUrl url(m_backendBaseUrl + "/billing/checkout");
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject body;
    body["planId"] = planId;
    body["userId"] = userId;
    QNetworkReply *reply = nam->post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit checkoutFailed(reply->errorString());
        } else {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QString u = doc.object().value("checkoutUrl").toString();
            if (u.isEmpty()) emit checkoutFailed("Backend did not return a checkout URL");
            else emit checkoutSessionReady(u);
        }
        reply->deleteLater();
        nam->deleteLater();
    });
}

void WhopClient::fetchEntitlements(const QString &userId) {
    if (m_backendBaseUrl.isEmpty()) {
        emit entitlementsFailed("Billing backend URL not configured.");
        return;
    }
    QNetworkAccessManager *nam = new QNetworkAccessManager(this);
    QUrl url(m_backendBaseUrl + "/billing/entitlements?userId=" + userId);
    QNetworkReply *reply = nam->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit entitlementsFailed(reply->errorString());
        } else {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            emit entitlementsReady(doc.isObject() ? doc.object() : QJsonObject());
        }
        reply->deleteLater();
        nam->deleteLater();
    });
}

} // namespace ModelBench
