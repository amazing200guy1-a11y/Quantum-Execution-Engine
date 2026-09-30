/**
 * quantum/risk_kernel.hpp
 *
 * Public API for the Quantum Execution Engine risk governor.
 * This header is the ONLY interface consumers should include.
 *
 * Design invariants:
 *   - All hot-path functions are [[nodiscard]] noexcept
 *   - No dynamic allocations occur on the critical path
 *   - PortfolioState is 64-byte aligned for cache-line affinity
 *   - Thread safety: evaluate() is read-only and lock-free;
 *     update_state() uses sequential_cst atomics
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <stdexcept>

namespace quantum::risk {

// ── Tuning constants ──────────────────────────────────────────────────────────
inline constexpr double  DEFAULT_MAX_DRAWDOWN = 0.03;   // 3.00 %
inline constexpr double  DEFAULT_MAX_NOTIONAL = 1'000'000.0;
inline constexpr uint32_t STALE_VERSION_RETRIES = 3;

// ── Decision enum ─────────────────────────────────────────────────────────────
enum class RiskDecision : uint8_t {
    APPROVED            = 0,
    REJECTED_DRAWDOWN   = 1,   ///< projected drawdown exceeds ceiling
    REJECTED_NOTIONAL   = 2,   ///< notional <= 0 or exceeds max
    REJECTED_STALE      = 3,   ///< version changed mid-evaluation
};

const char* decision_str(RiskDecision d) noexcept;

// ── Data contracts ─────────────────────────────────────────────────────────────

/// 64-byte cache-line aligned: no false sharing on multi-core writers.
struct alignas(64) PortfolioState {
    double   equity       = 100'000.0;
    double   peak_equity  = 100'000.0;
    double   max_drawdown = DEFAULT_MAX_DRAWDOWN;
    double   max_notional = DEFAULT_MAX_NOTIONAL;
    std::atomic<uint64_t> version{0};

    // Prevent accidental copy of atomic members
    PortfolioState() = default;
    PortfolioState(const PortfolioState&) = delete;
    PortfolioState& operator=(const PortfolioState&) = delete;
};

struct TradeProposal {
    double   notional          = 0.0;
    double   estimated_slippage = 0.0;  ///< fraction, e.g. 0.0002 = 2 bps
    int      side              = 0;     ///< +1 = long, -1 = short
};

struct RiskResult {
    RiskDecision decision          = RiskDecision::REJECTED_STALE;
    double       projected_drawdown = 0.0;
    double       equity_after       = 0.0;
    uint64_t     state_version      = 0;
    uint64_t     eval_ns            = 0;   ///< nanoseconds taken (set by caller)
};

// ── Core evaluator ─────────────────────────────────────────────────────────────

class RiskKernel {
public:
    explicit RiskKernel(PortfolioState* state);

    /**
     * Single-proposal evaluation — the hot path.
     * Thread-safe: read-only, uses only atomic loads.
     * Expected latency: < 200 ns on modern x86-64 with AVX2.
     */
    [[nodiscard]] RiskResult evaluate(const TradeProposal& trade) const noexcept;

    /**
     * Batch evaluation — SIMD-friendly sequential loop.
     * count proposals written to results (caller owns both buffers).
     */
    void evaluate_batch(
        const TradeProposal* proposals,
        RiskResult*          results,
        std::size_t          count) const noexcept;

    /**
     * Update equity after a fill. Uses sequential_cst to publish to all readers.
     */
    void update_equity(double new_equity) noexcept;

private:
    PortfolioState* state_;  ///< non-owning; lifetime managed by caller
};

} // namespace quantum::risk
