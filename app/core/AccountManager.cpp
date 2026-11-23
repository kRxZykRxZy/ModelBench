#include "AccountManager.h"

#include <QNetworkRequest>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

namespace ModelBench {

AccountManager::AccountManager(QObject *parent) : QObject(parent) {}

void AccountManager::setServerUrl(const QString &url) {
    if (m_serverUrl == url) return;
    m_serverUrl = url;
    emit serverUrlChanged();
}

void AccountManager::postJson(const QString &path, const QJsonObject &body, std::function<void(QJsonObject,bool)> cb) {
    QNetworkRequest req{QUrl(m_serverUrl + path)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb]() {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject obj = doc.isObject() ? doc.object() : QJsonObject();
        bool ok = reply->error() == QNetworkReply::NoError;
        if (!ok && obj.isEmpty()) obj["error"] = reply->errorString();
        reply->deleteLater();
        cb(obj, ok);
    });
}

void AccountManager::signup(const QString &email, const QString &password) {
    postJson("/auth/signup", QJsonObject{{"email", email}, {"password", password}}, [this, email](QJsonObject o, bool ok) {
        if (!ok) { emit authError(o.value("error").toString("Signup failed")); return; }
        if (o.contains("session") && o["session"].isObject()) {
            m_accessToken = o["session"].toObject().value("access_token").toString();
            m_email = email;
            emit loggedInChanged();
            emit emailChanged();
            refreshEntitlements();
        }
        emit authSuccess("Account created");
    });
}

void AccountManager::login(const QString &email, const QString &password) {
    postJson("/auth/login", QJsonObject{{"email", email}, {"password", password}}, [this, email](QJsonObject o, bool ok) {
        if (!ok) { emit authError(o.value("error").toString("Login failed")); return; }
        if (o.contains("session") && o["session"].isObject()) {
            m_accessToken = o["session"].toObject().value("access_token").toString();
            m_email = email;
            emit loggedInChanged();
            emit emailChanged();
            refreshEntitlements();
        }
        emit authSuccess("Logged in");
    });
}

void AccountManager::logout() {
    m_accessToken.clear();
    m_email.clear();
    m_tier = "free";
    emit loggedInChanged();
    emit emailChanged();
    emit tierChanged();
}

void AccountManager::refreshEntitlements() {
    if (m_accessToken.isEmpty()) return;
    QNetworkRequest req{QUrl(m_serverUrl + "/user/entitlements")};
    req.setRawHeader("Authorization", ("Bearer " + m_accessToken).toUtf8());
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject()) {
            m_tier = doc.object().value("tier").toString(m_tier);
            emit tierChanged();
        }
        reply->deleteLater();
    });
}

} // namespace ModelBench
