#include "SubscriptionManager.h"

namespace ModelFit {

SubscriptionManager::SubscriptionManager(QObject *parent) : QObject(parent) {}

bool SubscriptionManager::isFeatureEnabled(const QString &feature) const {
    if (m_tier == Free) {
        // Free tier: core inference always available
        return feature == "inference" || feature == "chat" || feature == "models";
    }
    return true;
}

void SubscriptionManager::refreshEntitlements() {
    // Boundary for secure backend verification; no network call without credentials.
}

void SubscriptionManager::startCheckout(Tier tier) {
    Q_UNUSED(tier);
    // Embedded checkout URL would be minted by secure backend; not available offline.
    emit checkoutReady(QString());
}

} // namespace ModelFit
