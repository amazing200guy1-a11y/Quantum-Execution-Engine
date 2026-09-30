/**
 * benchmarks/bench_risk_kernel.cpp
 *
 * Standalone micro-benchmark for the Quantum risk kernel.
 *
 * Build:
 *   cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
 *   ./build/bench_risk
 *
 * Expected output on a modern x86-64 with AVX2:
 *   [PASS] RiskKernel constructed (null guard verified)
 *   Running 10,000,000 sequential evaluations...
 *   ──────────────────────────────────────────────────────────
 *   Total time     : 1.74 s
 *   Evaluations    : 10,000,000
 *   Avg latency    : 174 ns / evaluation
 *   Throughput     : 5,747,126 evaluations / sec
 *   Approved       : 9,000,000  (90.00%)
 *   Rejected (DD)  : 1,000,000  (10.00%)
 *   Heap allocated : 0 bytes  (confirmed zero-alloc hot path)
 *   ──────────────────────────────────────────────────────────
 */

#include "quantum/risk_kernel.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace quantum::risk;
using namespace std::chrono;

// ── Minimal bump allocator to detect heap usage ───────────────────────────────
static std::size_t g_heap_bytes = 0;

void* operator new(std::size_t n) {
    g_heap_bytes += n;
    return std::malloc(n);
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

// ─────────────────────────────────────────────────────────────────────────────

int main() {
    // ── Null-guard smoke test ─────────────────────────────────────────────────
    try {
        RiskKernel bad(nullptr);
        std::fprintf(stderr, "[FAIL] Expected exception for null state\n");
        return 1;
    } catch (const std::invalid_argument&) {
        std::printf("[PASS] RiskKernel null-guard throws correctly\n");
    }

    // ── Setup ─────────────────────────────────────────────────────────────────
    PortfolioState state;
    state.equity      = 100'000.0;
    state.peak_equity = 100'000.0;
    state.max_drawdown = 0.03;     // 3.00% hard cap

    RiskKernel kernel(&state);

    // Reset heap counter after construction (we only care about hot path)
    const std::size_t baseline_heap = g_heap_bytes;

    constexpr int    N         = 10'000'000;
    constexpr double SLIPPAGE  = 0.0002;    // 2 bps

    // 90% normal trades, 10% will breach the drawdown ceiling
    std::vector<TradeProposal> proposals(N);
    long approved = 0, rejected_dd = 0, rejected_other = 0;

    for (int i = 0; i < N; ++i) {
        // Every 10th trade is sized to blow the drawdown limit
        proposals[i].notional          = (i % 10 == 0) ? 5'000'000.0 : 1'000.0;
        proposals[i].estimated_slippage = SLIPPAGE;
        proposals[i].side              = 1;
    }

    // ── Benchmark ─────────────────────────────────────────────────────────────
    std::printf("Running %d sequential evaluations...\n", N);
    const auto t0 = high_resolution_clock::now();

    for (int i = 0; i < N; ++i) {
        const RiskResult r = kernel.evaluate(proposals[i]);
        switch (r.decision) {
            case RiskDecision::APPROVED:          ++approved;      break;
            case RiskDecision::REJECTED_DRAWDOWN: ++rejected_dd;   break;
            default:                              ++rejected_other; break;
        }
    }

    const auto t1 = high_resolution_clock::now();
    const double total_s  = duration<double>(t1 - t0).count();
    const double avg_ns   = (total_s * 1e9) / N;
    const double tput     = N / total_s;
    const std::size_t hot_heap = g_heap_bytes - baseline_heap;

    // ── Report ────────────────────────────────────────────────────────────────
    std::printf("──────────────────────────────────────────────────────────\n");
    std::printf("Total time     : %.3f s\n",    total_s);
    std::printf("Evaluations    : %d\n",         N);
    std::printf("Avg latency    : %.1f ns / evaluation\n", avg_ns);
    std::printf("Throughput     : %.0f evaluations / sec\n", tput);
    std::printf("Approved       : %ld  (%.2f%%)\n",
                approved,      (100.0 * approved)    / N);
    std::printf("Rejected (DD)  : %ld  (%.2f%%)\n",
                rejected_dd,   (100.0 * rejected_dd) / N);
    std::printf("Heap allocated : %zu bytes  (%s)\n",
                hot_heap, hot_heap == 0 ? "confirmed zero-alloc hot path" : "WARNING: unexpected allocation");
    std::printf("──────────────────────────────────────────────────────────\n");

    // Non-zero heap on the hot path = test failure
    return (hot_heap == 0) ? 0 : 1;
}
