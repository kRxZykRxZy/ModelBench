#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>

namespace ModelFit {

class WhopClient : public QObject {
    Q_OBJECT
public:
    explicit WhopClient(QObject *parent = nullptr);
    void setBackendBaseUrl(const QString &url);
    // Mint a checkout session via secure backend (no secrets in client).
    void createCheckoutSession(const QString &planId, const QString &userId);
    // Verify/refresh entitlements via backend.
    void fetchEntitlements(const QString &userId);
signals:
    void checkoutSessionReady(const QString &checkoutUrl);
    void checkoutFailed(const QString &error);
    void entitlementsReady(const QJsonObject &entitlements);
    void entitlementsFailed(const QString &error);
private:
    QString m_backendBaseUrl;
};

} // namespace ModelFit
