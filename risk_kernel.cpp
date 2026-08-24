/**
 * Quantum-Execution-Engine — Risk Kernel (C++20)
 *
 * High-performance drawdown governor.
 * Uses aligned memory, atomic state, and SIMD-friendly loops
 * to evaluate portfolio risk in sub-microsecond budgets.
 *
 * Design goals:
 *   - No heap allocation on the hot path
 *   - Lock-free reads of portfolio state
 *   - Strict 16-byte alignment for vectorization
 */

#include <atomic>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace quantum::risk {

// ---------------------------------------------------------------------------
// Aligned portfolio snapshot (cache-line friendly)
// ---------------------------------------------------------------------------
struct alignas(64) PortfolioState {
    double equity;          // current equity
    double peak_equity;     // high-water mark
    double max_drawdown;    // allowed fraction, e.g. 0.03 = 3 %
    std::atomic<uint64_t> version{0};
};

// ---------------------------------------------------------------------------
// Trade proposal (POD, no virtuals)
// ---------------------------------------------------------------------------
struct TradeProposal {
    double notional;
    double estimated_slippage;
    int    side;            // +1 long / -1 short
};

// ---------------------------------------------------------------------------
// Result of a risk check
// ---------------------------------------------------------------------------
enum class RiskDecision : uint8_t {
    APPROVED = 0,
    REJECTED_DRAWDOWN,
    REJECTED_NOTIONAL,
    REJECTED_STALE_STATE
};

struct RiskResult {
    RiskDecision decision;
    double       projected_drawdown;
    uint64_t     state_version;
};

// ---------------------------------------------------------------------------
// Core evaluator
// ---------------------------------------------------------------------------
class RiskKernel {
public:
    explicit RiskKernel(PortfolioState* state) noexcept
        : state_(state) {
        if (!state_) {
            // Fail closed — never proceed with null state
            throw std::invalid_argument("PortfolioState pointer must not be null");
        }
    }

    /**
     * Evaluate a trade against the current portfolio.
     * Hot path: no locks, only atomic load of version + plain reads.
     * Expected latency budget: < 10 µs on modern x86-64.
     */
    [[nodiscard]] RiskResult evaluate(const TradeProposal& trade) const noexcept {
        // Snapshot version first (acquire semantics)
        const uint64_t ver = state_->version.load(std::memory_order_acquire);

        const double equity      = state_->equity;
        const double peak        = state_->peak_equity;
        const double max_dd      = state_->max_drawdown;

        // Simple projected equity after fill (conservative)
        const double projected   = equity - (trade.notional * trade.estimated_slippage);
        const double dd_from_peak = (peak > 0.0) ? (peak - projected) / peak : 0.0;

        RiskResult result{};
        result.state_version      = ver;
        result.projected_drawdown = dd_from_peak;

        // Stale-state guard (another thread may have updated)
        if (state_->version.load(std::memory_order_relaxed) != ver) {
            result.decision = RiskDecision::REJECTED_STALE_STATE;
            return result;
        }

        if (trade.notional <= 0.0) {
            result.decision = RiskDecision::REJECTED_NOTIONAL;
            return result;
        }

        if (dd_from_peak > max_dd) {
            result.decision = RiskDecision::REJECTED_DRAWDOWN;
            return result;
        }

        result.decision = RiskDecision::APPROVED;
        return result;
    }

    /**
     * SIMD-friendly batch check for multiple proposals.
     * Demonstrates vector-friendly layout; real production would use
     * std::experimental::simd or explicit AVX2 intrinsics.
     */
    void evaluate_batch(const TradeProposal* proposals,
                        RiskResult* results,
                        std::size_t count) const noexcept {
        for (std::size_t i = 0; i < count; ++i) {
            results[i] = evaluate(proposals[i]);
        }
    }

private:
    PortfolioState* state_;   // non-owning, lifetime managed by caller
};

} // namespace quantum::risk
