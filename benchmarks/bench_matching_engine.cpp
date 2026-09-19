#include <benchmark/benchmark.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include "quantengine/core/events.hpp"
#include "quantengine/engine/generic_matching_engine.hpp"
#include "quantengine/optimized/optimized_order_book.hpp"
#include "quantengine/reference/reference_order_book.hpp"
#include "workload_generator.hpp"

using namespace quantengine;
using namespace quantengine::bench;
using namespace quantengine::engine;

// Benchmark batch size
constexpr std::size_t kBatchSize = 50'000;

// ---------------------------------------------------------------------------
// Throughput Benchmarks
// ---------------------------------------------------------------------------

template <typename EngineType, WorkloadType WType>
static void BM_Throughput(benchmark::State& state) {
    const auto commands = WorkloadGenerator::generate(WType, kBatchSize);
    EngineType engine;

    for (auto _ : state) {
        state.PauseTiming();
        engine.reset();
        state.ResumeTiming();

        for (const auto& cmd : commands) {
            auto report = engine.process_command(cmd);
            benchmark::DoNotOptimize(report);
        }
    }

    const auto total_ops = state.iterations() * static_cast<int64_t>(commands.size());
    state.SetItemsProcessed(total_ops);
    state.counters["ops/sec"] =
        benchmark::Counter(static_cast<double>(total_ops), benchmark::Counter::kIsRate);
    state.counters["Mops/sec"] =
        benchmark::Counter(static_cast<double>(total_ops) / 1e6, benchmark::Counter::kIsRate);
}

// ---------------------------------------------------------------------------
// Latency Percentile Benchmarks
// ---------------------------------------------------------------------------

template <typename EngineType, WorkloadType WType>
static void BM_Latency(benchmark::State& state) {
    const auto commands = WorkloadGenerator::generate(WType, kBatchSize);
    EngineType engine;
    std::vector<std::uint64_t> latencies;
    latencies.reserve(commands.size());

    for (auto _ : state) {
        state.PauseTiming();
        engine.reset();
        latencies.clear();
        state.ResumeTiming();

        for (const auto& cmd : commands) {
            const auto start = std::chrono::steady_clock::now();
            auto report = engine.process_command(cmd);
            benchmark::DoNotOptimize(report);
            const auto end = std::chrono::steady_clock::now();

            const auto dur_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
            latencies.push_back(dur_ns);
        }
    }

    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        const auto n = latencies.size();
        const auto p50 = static_cast<double>(latencies[n * 50 / 100]);
        const auto p90 = static_cast<double>(latencies[n * 90 / 100]);
        const auto p99 = static_cast<double>(latencies[n * 99 / 100]);
        const auto p99_9 = static_cast<double>(latencies[n * 999 / 1000]);
        const auto max_lat = static_cast<double>(latencies.back());

        state.counters["p50_ns"] = p50;
        state.counters["p90_ns"] = p90;
        state.counters["p99_ns"] = p99;
        state.counters["p99.9_ns"] = p99_9;
        state.counters["max_ns"] = max_lat;
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(commands.size()));
}

// ---------------------------------------------------------------------------
// Register Google Benchmarks
// ---------------------------------------------------------------------------

// Reference Engine Throughput
BENCHMARK_TEMPLATE(BM_Throughput, ReferenceMatchingEngine, WorkloadType::Balanced)
    ->Name("Throughput/Reference/Balanced")
    ->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_Throughput, ReferenceMatchingEngine, WorkloadType::AddHeavy)
    ->Name("Throughput/Reference/AddHeavy")
    ->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_Throughput, ReferenceMatchingEngine, WorkloadType::CrossingHeavy)
    ->Name("Throughput/Reference/CrossingHeavy")
    ->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_Throughput, ReferenceMatchingEngine, WorkloadType::CancelHeavy)
    ->Name("Throughput/Reference/CancelHeavy")
    ->Unit(benchmark::kMillisecond);

// Optimized Engine Throughput
BENCHMARK_TEMPLATE(BM_Throughput, OptimizedMatchingEngine, WorkloadType::Balanced)
    ->Name("Throughput/Optimized/Balanced")
    ->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_Throughput, OptimizedMatchingEngine, WorkloadType::AddHeavy)
    ->Name("Throughput/Optimized/AddHeavy")
    ->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_Throughput, OptimizedMatchingEngine, WorkloadType::CrossingHeavy)
    ->Name("Throughput/Optimized/CrossingHeavy")
    ->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_Throughput, OptimizedMatchingEngine, WorkloadType::CancelHeavy)
    ->Name("Throughput/Optimized/CancelHeavy")
    ->Unit(benchmark::kMillisecond);

// Reference Engine Latency
BENCHMARK_TEMPLATE(BM_Latency, ReferenceMatchingEngine, WorkloadType::Balanced)
    ->Name("Latency/Reference/Balanced")
    ->Unit(benchmark::kMillisecond)
    ->Iterations(3);
BENCHMARK_TEMPLATE(BM_Latency, ReferenceMatchingEngine, WorkloadType::AddHeavy)
    ->Name("Latency/Reference/AddHeavy")
    ->Unit(benchmark::kMillisecond)
    ->Iterations(3);
BENCHMARK_TEMPLATE(BM_Latency, ReferenceMatchingEngine, WorkloadType::CrossingHeavy)
    ->Name("Latency/Reference/CrossingHeavy")
    ->Unit(benchmark::kMillisecond)
    ->Iterations(3);
BENCHMARK_TEMPLATE(BM_Latency, ReferenceMatchingEngine, WorkloadType::CancelHeavy)
    ->Name("Latency/Reference/CancelHeavy")
    ->Unit(benchmark::kMillisecond)
    ->Iterations(3);

// Optimized Engine Latency
BENCHMARK_TEMPLATE(BM_Latency, OptimizedMatchingEngine, WorkloadType::Balanced)
    ->Name("Latency/Optimized/Balanced")
    ->Unit(benchmark::kMillisecond)
    ->Iterations(3);
BENCHMARK_TEMPLATE(BM_Latency, OptimizedMatchingEngine, WorkloadType::AddHeavy)
    ->Name("Latency/Optimized/AddHeavy")
    ->Unit(benchmark::kMillisecond)
    ->Iterations(3);
BENCHMARK_TEMPLATE(BM_Latency, OptimizedMatchingEngine, WorkloadType::CrossingHeavy)
    ->Name("Latency/Optimized/CrossingHeavy")
    ->Unit(benchmark::kMillisecond)
    ->Iterations(3);
BENCHMARK_TEMPLATE(BM_Latency, OptimizedMatchingEngine, WorkloadType::CancelHeavy)
    ->Name("Latency/Optimized/CancelHeavy")
    ->Unit(benchmark::kMillisecond)
    ->Iterations(3);

// ---------------------------------------------------------------------------
// Standalone Calibrated Reporting Table Generator
// ---------------------------------------------------------------------------

struct CalibratedResult {
    std::string engine_name;
    std::string workload_name;
    double throughput_mops{0.0};
    double mean_latency_ns{0.0};
    double p50_ns{0.0};
    double p90_ns{0.0};
    double p99_ns{0.0};
    double p99_9_ns{0.0};
    double max_ns{0.0};
};

template <typename EngineType>
static CalibratedResult run_calibrated_pass(const std::string& eng_name, WorkloadType wtype,
                                            std::size_t op_count = 100'000) {
    const auto commands = WorkloadGenerator::generate(wtype, op_count);
    EngineType engine;
    if constexpr (std::is_same_v<EngineType, OptimizedMatchingEngine>) {
        engine = OptimizedMatchingEngine(optimized::OptimizedOrderBook(131072));
    }

    // Warm up
    for (const auto& cmd : commands) {
        auto rep = engine.process_command(cmd);
        benchmark::DoNotOptimize(rep);
    }

    engine.reset();
    std::vector<std::uint64_t> latencies;
    latencies.reserve(commands.size());

    const auto t_start = std::chrono::steady_clock::now();
    for (const auto& cmd : commands) {
        const auto t0 = std::chrono::steady_clock::now();
        auto rep = engine.process_command(cmd);
        benchmark::DoNotOptimize(rep);
        const auto t1 = std::chrono::steady_clock::now();
        latencies.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
    }
    const auto t_end = std::chrono::steady_clock::now();

    const auto elapsed_sec =
        std::chrono::duration_cast<std::chrono::duration<double>>(t_end - t_start).count();
    const double mops = (static_cast<double>(op_count) / elapsed_sec) / 1e6;

    std::sort(latencies.begin(), latencies.end());
    const auto n = latencies.size();
    const double sum = std::accumulate(
        latencies.begin(), latencies.end(), 0.0,
        [](double acc, std::uint64_t val) { return acc + static_cast<double>(val); });

    return CalibratedResult{
        .engine_name = eng_name,
        .workload_name = workload_name(wtype),
        .throughput_mops = mops,
        .mean_latency_ns = sum / static_cast<double>(n),
        .p50_ns = static_cast<double>(latencies[n * 50 / 100]),
        .p90_ns = static_cast<double>(latencies[n * 90 / 100]),
        .p99_ns = static_cast<double>(latencies[n * 99 / 100]),
        .p99_9_ns = static_cast<double>(latencies[n * 999 / 1000]),
        .max_ns = static_cast<double>(latencies.back()),
    };
}

static void print_markdown_summary() {
    std::cout
        << "\n================================================================================"
        << std::endl;
    std::cout << "               QuantEngine Calibrated Performance Summary                       "
              << std::endl;
    std::cout
        << "================================================================================\n"
        << std::endl;

    const std::vector<WorkloadType> workloads = {
        WorkloadType::Balanced,
        WorkloadType::AddHeavy,
        WorkloadType::CrossingHeavy,
        WorkloadType::CancelHeavy,
    };

    std::cout << "| Workload | Engine | Throughput (Mops/s) | Speedup | Mean (ns) | p50 (ns) | p90 "
                 "(ns) | p99 (ns) | p99.9 (ns) | Max (ns) |\n";
    std::cout << "|:---------|:-------|--------------------:|:-------:|----------:|---------:|-----"
                 "----:|---------:|-----------:|---------:|\n";

    for (auto w : workloads) {
        auto ref = run_calibrated_pass<ReferenceMatchingEngine>("Reference", w, 100'000);
        auto opt = run_calibrated_pass<OptimizedMatchingEngine>("Optimized", w, 100'000);

        const double speedup = opt.throughput_mops / ref.throughput_mops;

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "| " << workload_name(w) << " | " << ref.engine_name << " | "
                  << ref.throughput_mops << " | 1.00x | " << ref.mean_latency_ns << " | "
                  << ref.p50_ns << " | " << ref.p90_ns << " | " << ref.p99_ns << " | "
                  << ref.p99_9_ns << " | " << ref.max_ns << " |\n";

        std::cout << "| " << workload_name(w) << " | **" << opt.engine_name << "** | **"
                  << opt.throughput_mops << "** | **" << speedup << "x** | **"
                  << opt.mean_latency_ns << "** | **" << opt.p50_ns << "** | **" << opt.p90_ns
                  << "** | **" << opt.p99_ns << "** | **" << opt.p99_9_ns << "** | **" << opt.max_ns
                  << "** |\n";
    }
    std::cout
        << "\n================================================================================\n"
        << std::endl;
}

int main(int argc, char** argv) {
    ::benchmark::Initialize(&argc, argv);
    if (::benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }
    ::benchmark::RunSpecifiedBenchmarks();
    ::benchmark::Shutdown();

    // Generate executive summary comparison table
    print_markdown_summary();

    return 0;
}
