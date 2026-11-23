#include "SubscriptionManager.h"

namespace ModelBench {

SubscriptionManager::SubscriptionManager(QObject *parent) : QObject(parent) {}

QString SubscriptionManager::tierName() const {
    switch (m_tier) {
        case Pro: return QStringLiteral("Pro");
        case Ultimate: return QStringLiteral("Ultimate");
        case Teams: return QStringLiteral("Teams");
        default: return QStringLiteral("Free");
    }
}

bool SubscriptionManager::isFeatureEnabled(const QString &feature) const {
    if (m_tier == Free) {
        return feature == "inference" || feature == "chat" || feature == "models";
    }
    return true;
}

void SubscriptionManager::refreshEntitlements() {
    // Requires a configured secure backend; offline keeps current tier.
}

void SubscriptionManager::startCheckoutTier(int tier) {
    startCheckout(static_cast<Tier>(tier));
}

void SubscriptionManager::startCheckout(Tier tier) {
    Q_UNUSED(tier);
    emit checkoutReady(QString());
}

} // namespace ModelBench
