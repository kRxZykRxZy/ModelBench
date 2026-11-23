#include "ModelRegistry.h"

namespace ModelBench {

void ModelRegistry::add(const InstalledModel &m) { m_models << m; }
void ModelRegistry::remove(const QString &id) {
    m_models.erase(std::remove_if(m_models.begin(), m_models.end(),
        [&](const InstalledModel &m){ return m.id == id; }), m_models.end());
}
QList<InstalledModel> ModelRegistry::all() const { return m_models; }
InstalledModel ModelRegistry::find(const QString &id) const {
    for (const auto &m : m_models) if (m.id == id) return m;
    return {};
}

} // namespace ModelBench
