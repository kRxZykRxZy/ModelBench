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
    Q_INVOKABLE int currentTier() const { return static_cast<int>(m_tier); }
    Q_INVOKABLE QString tierName() const;
    Q_INVOKABLE QStringList planNames() const { return {"Free", "Pro", "Ultimate", "Teams"}; }
    Q_INVOKABLE QStringList planPrices() const { return {"$0", "$5/mo", "$12/mo", "$29/mo"}; }
    Q_INVOKABLE bool isFeatureEnabled(const QString &feature) const;
    Q_INVOKABLE void refreshEntitlements();
    Q_INVOKABLE void startCheckoutTier(int tier);
    void startCheckout(Tier tier);
signals:
    void tierChanged(Tier tier);
    void checkoutReady(const QString &url);
private:
    Tier m_tier = Free;
};

} // namespace ModelFit
