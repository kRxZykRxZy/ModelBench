#pragma once

#include <QObject>
#include <QString>

namespace ModelFit {

class SubscriptionManager : public QObject {
    Q_OBJECT
public:
    explicit SubscriptionManager(QObject *parent = nullptr);
    enum Tier { Free, Pro, Ultimate, Teams };
    Q_ENUM(Tier)
    Tier currentTier() const { return m_tier; }
    bool isFeatureEnabled(const QString &feature) const;
    void refreshEntitlements();
    void startCheckout(Tier tier);
signals:
    void tierChanged(Tier tier);
    void checkoutReady(const QString &url);
private:
    Tier m_tier = Free;
};

} // namespace ModelFit
