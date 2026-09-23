// benchmarks/bench_l3_replay.cpp
//
// Phase 5 Benchmarks: L3 parsing, Historical book reconstruction, and full replay pipeline.
// Realistic workloads at 100k and 1M event scales.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "quantengine/portfolio/Portfolio.hpp"
#include "quantengine/replay/EventClock.hpp"
#include "quantengine/replay/FifoQueueModel.hpp"
#include "quantengine/replay/HistoricalL3Book.hpp"
#include "quantengine/replay/L3CsvParser.hpp"
#include "quantengine/replay/LatencyModel.hpp"
#include "quantengine/replay/QueuePositionTracker.hpp"
#include "quantengine/replay/ReplayGateway.hpp"

using namespace quantengine;
using namespace quantengine::core;
using namespace quantengine::replay;
using namespace quantengine::portfolio;

// ---------------------------------------------------------------------------
// Workload synthesis for realistic benchmark event streams
// ---------------------------------------------------------------------------
static auto generate_synthetic_l3_events(std::size_t count) -> std::vector<L3Message> {
    std::vector<L3Message> events;
    events.reserve(count);

    std::mt19937_64 rng(1337);
    std::uniform_int_distribution<std::int64_t> price_dist(14500, 15500);
    std::uniform_int_distribution<std::uint64_t> qty_dist(10, 100);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> action_dist(0, 99);

    std::vector<VenueOrderId> resting_ids;
    resting_ids.reserve(count / 2);

    VenueOrderId next_id = 1;
    market::NanoTs ts = 1'000'000'000LL;

    for (std::size_t i = 0; i < count; ++i) {
        ts += 100;
        int action = action_dist(rng);

        if (resting_ids.empty() || action < 60) {
            // 60% OrderAdded
            VenueOrderId id = next_id++;
            Side s = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            PriceTicks p = price_dist(rng);
            Quantity q = qty_dist(rng);

            events.push_back(OrderAdded{
                .exchange_ts = ts,
                .venue_order_id = id,
                .symbol = market::make_symbol("AAPL"),
                .price = p,
                .quantity = q,
                .side = s,
            });
            resting_ids.push_back(id);
        } else if (action < 85) {
            // 25% OrderExecuted
            std::uniform_int_distribution<std::size_t> idx_dist(0, resting_ids.size() - 1);
            std::size_t idx = idx_dist(rng);
            VenueOrderId id = resting_ids[idx];

            events.push_back(OrderExecuted{
                .exchange_ts = ts,
                .venue_order_id = id,
                .symbol = market::make_symbol("AAPL"),
                .executed_qty = 25,
                .match_number = i + 1,
            });
        } else {
            // 15% OrderCancelled
            std::uniform_int_distribution<std::size_t> idx_dist(0, resting_ids.size() - 1);
            std::size_t idx = idx_dist(rng);
            VenueOrderId id = resting_ids[idx];
            resting_ids.erase(resting_ids.begin() + static_cast<std::ptrdiff_t>(idx));

            events.push_back(OrderCancelled{
                .exchange_ts = ts,
                .venue_order_id = id,
                .symbol = market::make_symbol("AAPL"),
                .cancelled_qty = 50,
            });
        }
    }

    return events;
}

// ---------------------------------------------------------------------------
// Benchmark: L3 CSV Parser
// ---------------------------------------------------------------------------
static void BM_L3CsvParser_ParseLine(benchmark::State& state) {
    const std::string line = "A,1000000000,1001,AAPL,15000,100,B\n";
    std::string buffer;
    buffer.reserve(line.size() * 10'000);
    for (int i = 0; i < 10'000; ++i) {
        buffer += line;
    }

    L3Message out;
    for (auto _ : state) {
        std::istringstream ss{buffer};
        L3CsvParser parser{ss, "bench.csv"};
        while (parser.next(out)) {
            benchmark::DoNotOptimize(out);
        }
    }

    const int64_t total_lines = state.iterations() * 10'000;
    state.SetItemsProcessed(total_lines);
    state.counters["lines/sec"] =
        benchmark::Counter(static_cast<double>(total_lines), benchmark::Counter::kIsRate);
    state.counters["Mlines/sec"] =
        benchmark::Counter(static_cast<double>(total_lines) / 1e6, benchmark::Counter::kIsRate);
}
BENCHMARK(BM_L3CsvParser_ParseLine)->Unit(benchmark::kMillisecond);

// ---------------------------------------------------------------------------
// Benchmark: Historical L3 Book Reconstruction (100k events)
// ---------------------------------------------------------------------------
static void BM_HistoricalL3Book_Reconstruction_100k(benchmark::State& state) {
    const auto events = generate_synthetic_l3_events(100'000);
    HistoricalL3Book book;

    for (auto _ : state) {
        state.PauseTiming();
        book.clear();
        state.ResumeTiming();

        for (const auto& msg : events) {
            book.apply(msg);
        }
        benchmark::DoNotOptimize(book);
    }

    const int64_t total_ops = state.iterations() * 100'000;
    state.SetItemsProcessed(total_ops);
    state.counters["events/sec"] =
        benchmark::Counter(static_cast<double>(total_ops), benchmark::Counter::kIsRate);
    state.counters["Mevents/sec"] =
        benchmark::Counter(static_cast<double>(total_ops) / 1e6, benchmark::Counter::kIsRate);
}
BENCHMARK(BM_HistoricalL3Book_Reconstruction_100k)->Unit(benchmark::kMillisecond);

// ---------------------------------------------------------------------------
// Benchmark: Historical L3 Book Reconstruction (1M events)
// ---------------------------------------------------------------------------
static void BM_HistoricalL3Book_Reconstruction_1M(benchmark::State& state) {
    const auto events = generate_synthetic_l3_events(1'000'000);
    HistoricalL3Book book;

    for (auto _ : state) {
        state.PauseTiming();
        book.clear();
        state.ResumeTiming();

        for (const auto& msg : events) {
            book.apply(msg);
        }
        benchmark::DoNotOptimize(book);
    }

    const int64_t total_ops = state.iterations() * 1'000'000;
    state.SetItemsProcessed(total_ops);
    state.counters["events/sec"] =
        benchmark::Counter(static_cast<double>(total_ops), benchmark::Counter::kIsRate);
    state.counters["Mevents/sec"] =
        benchmark::Counter(static_cast<double>(total_ops) / 1e6, benchmark::Counter::kIsRate);
}
BENCHMARK(BM_HistoricalL3Book_Reconstruction_1M)->Unit(benchmark::kMillisecond);

// ---------------------------------------------------------------------------
// Benchmark: Full Replay Pipeline with Strategy Fills & Portfolio (100k events)
// ---------------------------------------------------------------------------
static void BM_FullReplay_Pipeline_100k(benchmark::State& state) {
    const auto events = generate_synthetic_l3_events(100'000);

    for (auto _ : state) {
        HistoricalL3Book book;
        QueuePositionTracker tracker;
        EventClock clock(0);
        LatencyConfig latency = LatencyConfig::symmetric_network(10'000);

        ReplayGateway gateway(book, tracker, clock, latency);
        Portfolio portfolio;
        (void)gateway.connect(portfolio);

        const auto sym = market::make_symbol("AAPL");

        for (std::size_t i = 0; i < events.size(); ++i) {
            gateway.process_historical_message(events[i]);

            // Periodic participant quote injection every 1,000 events
            if (i % 1000 == 0 && book.best_bid_price().has_value() &&
                book.best_ask_price().has_value() &&
                *book.best_bid_price() < *book.best_ask_price()) {
                (void)gateway.submit_order(execution::OrderRequest{
                    .client_order_id = static_cast<OrderId>(10000 + i),
                    .side = Side::Buy,
                    .type = OrderType::Limit,
                    .time_in_force = execution::TimeInForce::Day,
                    .symbol = sym,
                    .price = *book.best_bid_price(),
                    .quantity = 10,
                });
            }
        }

        benchmark::DoNotOptimize(portfolio.total_realized_pnl());
    }

    const int64_t total_ops = state.iterations() * 100'000;
    state.SetItemsProcessed(total_ops);
    state.counters["events/sec"] =
        benchmark::Counter(static_cast<double>(total_ops), benchmark::Counter::kIsRate);
    state.counters["Mevents/sec"] =
        benchmark::Counter(static_cast<double>(total_ops) / 1e6, benchmark::Counter::kIsRate);
}
BENCHMARK(BM_FullReplay_Pipeline_100k)->Unit(benchmark::kMillisecond);

// ---------------------------------------------------------------------------
// Benchmark: Full Replay Pipeline with Strategy Fills & Portfolio (1M events)
// ---------------------------------------------------------------------------
static void BM_FullReplay_Pipeline_1M(benchmark::State& state) {
    const auto events = generate_synthetic_l3_events(1'000'000);

    for (auto _ : state) {
        HistoricalL3Book book;
        QueuePositionTracker tracker;
        EventClock clock(0);
        LatencyConfig latency = LatencyConfig::symmetric_network(10'000);

        ReplayGateway gateway(book, tracker, clock, latency);
        Portfolio portfolio;
        (void)gateway.connect(portfolio);

        const auto sym = market::make_symbol("AAPL");

        for (std::size_t i = 0; i < events.size(); ++i) {
            gateway.process_historical_message(events[i]);

            if (i % 1000 == 0 && book.best_bid_price().has_value() &&
                book.best_ask_price().has_value() &&
                *book.best_bid_price() < *book.best_ask_price()) {
                (void)gateway.submit_order(execution::OrderRequest{
                    .client_order_id = static_cast<OrderId>(10000 + i),
                    .side = Side::Buy,
                    .type = OrderType::Limit,
                    .time_in_force = execution::TimeInForce::Day,
                    .symbol = sym,
                    .price = *book.best_bid_price(),
                    .quantity = 10,
                });
            }
        }

        benchmark::DoNotOptimize(portfolio.total_realized_pnl());
    }

    const int64_t total_ops = state.iterations() * 1'000'000;
    state.SetItemsProcessed(total_ops);
    state.counters["events/sec"] =
        benchmark::Counter(static_cast<double>(total_ops), benchmark::Counter::kIsRate);
    state.counters["Mevents/sec"] =
        benchmark::Counter(static_cast<double>(total_ops) / 1e6, benchmark::Counter::kIsRate);
}
BENCHMARK(BM_FullReplay_Pipeline_1M)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
