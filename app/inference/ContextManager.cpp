#include "ContextManager.h"

#include <algorithm>

namespace ModelBench {

void ContextManager::setCapacity(int capacity)
{
    m_capacity.store(std::max(0, capacity), std::memory_order_relaxed);
    const int cap = m_capacity.load(std::memory_order_relaxed);
    int used = m_used.load(std::memory_order_relaxed);
    if (used > cap) {
        used = cap;
    }
    m_used.store(std::max(0, used), std::memory_order_relaxed);
}

void ContextManager::setUsed(int tokens)
{
    const int cap = m_capacity.load(std::memory_order_relaxed);
    int value = std::max(0, tokens);
    if (cap > 0) {
        value = std::min(value, cap);
    }
    m_used.store(value, std::memory_order_relaxed);
}

void ContextManager::addTokens(int tokens)
{
    setUsed(m_used.load(std::memory_order_relaxed) + tokens);
}

void ContextManager::reset()
{
    m_used.store(0, std::memory_order_relaxed);
}

int ContextManager::available() const
{
    const int cap = m_capacity.load(std::memory_order_relaxed);
    if (cap <= 0) {
        return 0;
    }
    return std::max(0, cap - m_used.load(std::memory_order_relaxed));
}

bool ContextManager::fits(int tokens) const
{
    return tokens >= 0 && tokens <= available();
}

double ContextManager::usageRatio() const
{
    const int cap = m_capacity.load(std::memory_order_relaxed);
    if (cap <= 0) {
        return 0.0;
    }
    return static_cast<double>(m_used.load(std::memory_order_relaxed)) / static_cast<double>(cap);
}

int ContextManager::maxPromptTokens(int reserve) const
{
    const int cap = m_capacity.load(std::memory_order_relaxed);
    if (cap <= 0) {
        return 0;
    }
    return std::max(0, cap - std::max(0, reserve));
}

} // namespace ModelBench
