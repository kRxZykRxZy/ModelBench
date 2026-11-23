#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace ModelBench {

class AccountManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY loggedInChanged)
    Q_PROPERTY(QString email READ email NOTIFY emailChanged)
    Q_PROPERTY(QString tier READ tier NOTIFY tierChanged)
    Q_PROPERTY(QString serverUrl READ serverUrl WRITE setServerUrl NOTIFY serverUrlChanged)
public:
    explicit AccountManager(QObject *parent = nullptr);

    bool loggedIn() const { return !m_accessToken.isEmpty(); }
    QString email() const { return m_email; }
    QString tier() const { return m_tier; }
    QString serverUrl() const { return m_serverUrl; }
    void setServerUrl(const QString &url);

    Q_INVOKABLE void signup(const QString &email, const QString &password);
    Q_INVOKABLE void login(const QString &email, const QString &password);
    Q_INVOKABLE void logout();
    Q_INVOKABLE void refreshEntitlements();

signals:
    void loggedInChanged();
    void emailChanged();
    void tierChanged();
    void serverUrlChanged();
    void authError(const QString &message);
    void authSuccess(const QString &message);

private:
    void postJson(const QString &path, const QJsonObject &body, std::function<void(QJsonObject,bool)> cb);
    QString m_serverUrl = "https://your-site-url.example.com";
    QString m_accessToken;
    QString m_email;
    QString m_tier = "free";
    QNetworkAccessManager m_nam;
};

} // namespace ModelBench
