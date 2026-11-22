#pragma once

#include <atomic>

namespace ModelFit {

/**
 * Tracks how much of the model's context window is in use.
 *
 * The backend updates this from the inference worker thread while the UI may
 * query it from the GUI thread, so the counters are atomic.
 */
class ContextManager {
public:
    void setCapacity(int capacity);
    int capacity() const { return m_capacity.load(std::memory_order_relaxed); }

    void setUsed(int tokens);
    void addTokens(int tokens);
    void reset();
    int used() const { return m_used.load(std::memory_order_relaxed); }

    int available() const;
    bool fits(int tokens) const;
    double usageRatio() const;

    /// Maximum number of prompt tokens that still leave room for reserve.
    int maxPromptTokens(int reserve) const;

private:
    std::atomic<int> m_capacity{0};
    std::atomic<int> m_used{0};
};

} // namespace ModelFit
