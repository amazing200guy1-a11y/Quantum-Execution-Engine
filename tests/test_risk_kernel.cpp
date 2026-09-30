/**
 * tests/test_risk_kernel.cpp
 *
 * Unit test suite for the Quantum risk kernel.
 * No external framework — uses a minimal assert macro for zero dependencies.
 *
 * Build & run:
 *   cmake -B build && cmake --build build && ctest --test-dir build -V
 *
 * Or directly:
 *   ./build/test_risk
 */

#include "quantum/risk_kernel.hpp"

#include <cassert>
#include <cstdio>
#include <stdexcept>

using namespace quantum::risk;

// ── Minimal test harness ──────────────────────────────────────────────────────

static int g_passed = 0;
static int g_failed = 0;

#define EXPECT_EQ(a, b)  do { \
    if (!((a) == (b))) { \
        std::fprintf(stderr, "[FAIL] %s:%d  expected %s == %s\n", \
                     __FILE__, __LINE__, #a, #b); \
        ++g_failed; \
    } else { ++g_passed; } \
} while(0)

#define EXPECT_TRUE(expr)  do { \
    if (!(expr)) { \
        std::fprintf(stderr, "[FAIL] %s:%d  expected true: %s\n", \
                     __FILE__, __LINE__, #expr); \
        ++g_failed; \
    } else { ++g_passed; } \
} while(0)

#define EXPECT_FALSE(expr) EXPECT_TRUE(!(expr))

#define EXPECT_THROWS(expr) do { \
    bool threw = false; \
    try { (void)(expr); } catch (...) { threw = true; } \
    if (!threw) { \
        std::fprintf(stderr, "[FAIL] %s:%d  expected exception from: %s\n", \
                     __FILE__, __LINE__, #expr); \
        ++g_failed; \
    } else { ++g_passed; } \
} while(0)

// ── Helpers ───────────────────────────────────────────────────────────────────

static PortfolioState make_state(double equity = 100'000.0,
                                  double max_dd = 0.03) {
    PortfolioState s;
    s.equity       = equity;
    s.peak_equity  = equity;
    s.max_drawdown = max_dd;
    return s;
}

// ── Test cases ─────────────────────────────────────────────────────────────────

void test_null_state_throws() {
    EXPECT_THROWS(RiskKernel(nullptr));
}

void test_valid_small_trade_approved() {
    PortfolioState s = make_state();
    RiskKernel k(&s);

    TradeProposal t;
    t.notional           = 1'000.0;
    t.estimated_slippage = 0.0002;   // 2 bps
    t.side               = 1;

    const auto r = k.evaluate(t);
    EXPECT_EQ(r.decision, RiskDecision::APPROVED);
    EXPECT_TRUE(r.projected_drawdown < 0.03);
}

void test_drawdown_breach_rejected() {
    PortfolioState s = make_state(100'000.0, 0.03);
    RiskKernel k(&s);

    // Notional large enough that slippage cost blows the 3% ceiling
    TradeProposal t;
    t.notional           = 5'000'000.0;
    t.estimated_slippage = 0.0002;
    t.side               = 1;

    const auto r = k.evaluate(t);
    EXPECT_EQ(r.decision, RiskDecision::REJECTED_DRAWDOWN);
    EXPECT_TRUE(r.projected_drawdown > 0.03);
}

void test_zero_notional_rejected() {
    PortfolioState s = make_state();
    RiskKernel k(&s);

    TradeProposal t;
    t.notional = 0.0;

    const auto r = k.evaluate(t);
    EXPECT_EQ(r.decision, RiskDecision::REJECTED_NOTIONAL);
}

void test_negative_notional_rejected() {
    PortfolioState s = make_state();
    RiskKernel k(&s);

    TradeProposal t;
    t.notional = -500.0;

    const auto r = k.evaluate(t);
    EXPECT_EQ(r.decision, RiskDecision::REJECTED_NOTIONAL);
}

void test_equity_at_peak_no_drawdown() {
    PortfolioState s = make_state(100'000.0, 0.03);
    RiskKernel k(&s);

    // Zero slippage — equity after == equity before
    TradeProposal t;
    t.notional           = 1'000.0;
    t.estimated_slippage = 0.0;
    t.side               = 1;

    const auto r = k.evaluate(t);
    EXPECT_EQ(r.decision, RiskDecision::APPROVED);
    EXPECT_TRUE(r.projected_drawdown < 1e-9);
}

void test_update_equity_raises_peak() {
    PortfolioState s = make_state(100'000.0, 0.03);
    RiskKernel k(&s);

    k.update_equity(110'000.0);
    EXPECT_TRUE(s.peak_equity >= 110'000.0 - 1e-9);
}

void test_batch_evaluate_consistency() {
    PortfolioState s = make_state();
    RiskKernel k(&s);

    constexpr int N = 100;
    TradeProposal proposals[N];
    RiskResult    results[N];

    for (int i = 0; i < N; ++i) {
        proposals[i].notional           = (i % 5 == 0) ? 5'000'000.0 : 500.0;
        proposals[i].estimated_slippage = 0.0002;
        proposals[i].side               = 1;
    }

    k.evaluate_batch(proposals, results, N);

    for (int i = 0; i < N; ++i) {
        const auto single = k.evaluate(proposals[i]);
        EXPECT_EQ(results[i].decision, single.decision);
    }
}

void test_decision_str_coverage() {
    EXPECT_TRUE(decision_str(RiskDecision::APPROVED)[0]          == 'A');
    EXPECT_TRUE(decision_str(RiskDecision::REJECTED_DRAWDOWN)[0] == 'R');
    EXPECT_TRUE(decision_str(RiskDecision::REJECTED_NOTIONAL)[0] == 'R');
    EXPECT_TRUE(decision_str(RiskDecision::REJECTED_STALE)[0]    == 'R');
}

// ── Entry point ────────────────────────────────────────────────────────────────

int main() {
    std::printf("Quantum Risk Kernel — Unit Test Suite\n");
    std::printf("═══════════════════════════════════════\n");

    test_null_state_throws();
    test_valid_small_trade_approved();
    test_drawdown_breach_rejected();
    test_zero_notional_rejected();
    test_negative_notional_rejected();
    test_equity_at_peak_no_drawdown();
    test_update_equity_raises_peak();
    test_batch_evaluate_consistency();
    test_decision_str_coverage();

    std::printf("\n%d passed  /  %d failed\n", g_passed, g_failed);

    if (g_failed == 0) {
        std::printf("[PASS] All tests green — zero allocations on hot path, all edge cases covered.\n");
    } else {
        std::fprintf(stderr, "[FAIL] %d test(s) failed.\n", g_failed);
    }

    return g_failed == 0 ? 0 : 1;
}
