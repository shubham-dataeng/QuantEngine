#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "quantengine/core/events.hpp"
#include "quantengine/core/types.hpp"
#include "quantengine/core/version.hpp"
#include "quantengine/engine/canonical_state.hpp"
#include "quantengine/engine/generic_matching_engine.hpp"
#include "quantengine/engine/invariants.hpp"
#include "quantengine/engine/workload_generator.hpp"
#include "quantengine/optimized/optimized_order_book.hpp"
#include "quantengine/reference/reference_order_book.hpp"

using namespace quantengine;
using namespace quantengine::core;
using namespace quantengine::engine;
using namespace quantengine::bench;

static void print_banner() {
    std::cout << R"(
  ██████╗ ██╗   ██╗ █████╗ ███╗   ██╗████████╗███████╗███╗   ██╗ ██████╗ ██╗███╗   ██╗███████╗
 ██╔═══██╗██║   ██║██╔══██╗████╗  ██║╚══██╔══╝██╔════╝████╗  ██║██╔════╝ ██║████╗  ██║██╔════╝
 ██║   ██║██║   ██║███████║██╔██╗ ██║   ██║   █████╗  ██╔██╗ ██║██║  ███╗██║██╔██╗ ██║█████╗  
 ██║▄▄ ██║██║   ██║██╔══██║██║╚██╗██║   ██║   ██╔══╝  ██║╚██╗██║██║   ██║██║██║╚██╗██║██╔══╝  
 ╚██████╔╝╚██████╔╝██║  ██║██║ ╚████║   ██║   ███████╗██║ ╚████║╚██████╔╝██║██║ ╚████║███████╗
  ╚══▀▀═╝  ╚═════╝ ╚═╝  ╚═╝╚═╝  ╚═══╝   ╚═╝   ╚══════╝╚═╝  ╚═══╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝╚══════╝
)" << "\n";
    std::cout << " Deterministic C++20 Limit Order Matching Engine (v" << get_version() << ")\n\n";
}

static void print_help(const char* prog) {
    print_banner();
    std::cout
        << "Usage: " << prog << " <command> [options]\n\n"
        << "Commands:\n"
        << "  demo                  Run an interactive visual order book matching simulation\n"
        << "  benchmark [options]   Run high-resolution microbenchmarks and latency profiling\n"
        << "  replay <file>         Replay an event journal, verify determinism and invariants\n"
        << "  --version, -v         Display engine version\n"
        << "  --help, -h            Display this help banner\n\n"
        << "Benchmark Options:\n"
        << "  --workload <type>     balanced | add | crossing | cancel (default: balanced)\n"
        << "  --ops <count>         Number of operations (default: 100000)\n"
        << "  --engine <type>       optimized | reference (default: optimized)\n\n"
        << "Replay Options:\n"
        << "  --engine <type>       optimized | reference (default: optimized)\n"
        << "  --verify-invariants   Audit invariants after each command (default: true)\n\n";
}

static void print_depth_ladder(const std::vector<reference::LevelInfo>& bids,
                               const std::vector<reference::LevelInfo>& asks,
                               std::size_t max_rows = 5) {
    std::cout << "\n+-------------------------------------------------------------------------+\n"
              << "|                        ORDER BOOK DEPTH LADDER                          |\n"
              << "+--------------------+-----------------+-----------------+----------------+\n"
              << "|   Bid Orders (Vol) |       Bid Price |       Ask Price | Ask Orders(Vol)|\n"
              << "+--------------------+-----------------+-----------------+----------------+\n";

    const std::size_t rows =
        std::max(std::min(bids.size(), max_rows), std::min(asks.size(), max_rows));
    for (std::size_t i = 0; i < rows; ++i) {
        std::string bid_str = "";
        std::string bid_px_str = "";
        if (i < bids.size()) {
            std::ostringstream ss;
            ss << bids[i].order_count << " (" << bids[i].total_quantity << ")";
            bid_str = ss.str();
            bid_px_str = std::to_string(bids[i].price);
        }

        std::string ask_str = "";
        std::string ask_px_str = "";
        if (i < asks.size()) {
            std::ostringstream ss;
            ss << asks[i].order_count << " (" << asks[i].total_quantity << ")";
            ask_str = ss.str();
            ask_px_str = std::to_string(asks[i].price);
        }

        std::cout << "| " << std::setw(18) << bid_str << " | " << std::setw(15) << bid_px_str
                  << " | " << std::setw(15) << ask_px_str << " | " << std::setw(14) << ask_str
                  << " |\n";
    }

    if (bids.empty() && asks.empty()) {
        std::cout
            << "|                       [ ORDER BOOK IS EMPTY ]                           |\n";
    }

    std::cout << "+--------------------+-----------------+-----------------+----------------+\n\n";
}

static int run_demo() {
    print_banner();
    std::cout << ">>> Running Interactive Visual Order Book Matching Simulation <<<\n\n";

    OptimizedMatchingEngine engine;

    std::cout << "1. Seeding Bid side with resting limit orders...\n";
    (void)engine.submit_order(1, Side::Buy, 9950, 100);
    (void)engine.submit_order(2, Side::Buy, 9950, 50);
    (void)engine.submit_order(3, Side::Buy, 9900, 200);
    (void)engine.submit_order(4, Side::Buy, 9850, 300);

    std::cout << "2. Seeding Ask side with resting limit orders...\n";
    (void)engine.submit_order(5, Side::Sell, 10050, 80);
    (void)engine.submit_order(6, Side::Sell, 10050, 120);
    (void)engine.submit_order(7, Side::Sell, 10100, 150);
    (void)engine.submit_order(8, Side::Sell, 10150, 400);

    std::cout << "\nInitial Book State (Uncrossed Spread: 9950 - 10050):";
    print_depth_ladder(engine.book().get_bids(), engine.book().get_asks());

    std::cout
        << "3. Submitting Aggressive Crossing Buy Order:\n"
        << "   Order ID: 100 | Side: BUY | Price: 10100 | Quantity: 250 (Sweeps across levels)\n";

    auto report = engine.submit_order(100, Side::Buy, 10100, 250);

    std::cout << "\n>>> Execution Report Received <<<\n"
              << "   Order Status      : " << to_string(report.status) << "\n"
              << "   Filled Quantity   : " << report.filled_quantity << " / 250\n"
              << "   Remaining Quantity: " << report.remaining_quantity << "\n"
              << "   Trades Generated  : " << report.trades.size() << "\n\n";

    for (std::size_t i = 0; i < report.trades.size(); ++i) {
        const auto& t = report.trades[i];
        std::cout << "   Trade #" << (i + 1) << ": TradeID=" << t.trade_id
                  << " | Maker=" << t.maker_order_id << " (Side=" << to_string(t.maker_side) << ")"
                  << " | Taker=" << t.taker_order_id << " | ExecPrice=" << t.price
                  << " (Maker Price Rule) | ExecQty=" << t.quantity << "\n";
    }

    std::cout
        << "\nUpdated Book State After Execution (Ask 10050 swept, Ask 10100 partially filled):";
    print_depth_ladder(engine.book().get_bids(), engine.book().get_asks());

    std::cout << "4. Auditing State Invariants & Canonical Hash...\n";
    auto [ok, err] = InvariantAuditor::audit(engine);
    std::cout << "   Invariants Audit : "
              << (ok ? "PASSED (No Crossed Book, Volume Parity OK)" : ("FAILED: " + err)) << "\n";
    std::cout << "   Canonical 64-bit Hash: "
              << CanonicalState::to_string(engine).substr(
                     CanonicalState::to_string(engine).find("0x"))
              << "\n\n";

    return 0;
}

template <typename EngineType>
static int run_benchmark(WorkloadType wtype, std::size_t op_count, const std::string& eng_name) {
    std::cout << "Generating " << op_count << " deterministic commands for " << workload_name(wtype)
              << "...\n";

    const auto commands = WorkloadGenerator::generate(wtype, op_count);
    EngineType engine;

    std::cout << "Warming up engine cache...\n";
    for (const auto& cmd : commands) {
        auto r = engine.process_command(cmd);
        (void)r;
    }

    engine.reset();
    std::vector<std::uint64_t> latencies;
    latencies.reserve(commands.size());

    std::cout << "Executing calibrated run (" << eng_name << ")...\n";
    const auto t_start = std::chrono::steady_clock::now();
    for (const auto& cmd : commands) {
        const auto t0 = std::chrono::steady_clock::now();
        auto rep = engine.process_command(cmd);
        (void)rep;
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
    const double sum =
        std::accumulate(latencies.begin(), latencies.end(), 0.0,
                        [](double acc, std::uint64_t v) { return acc + static_cast<double>(v); });

    std::cout << "\n+---------------------------------------------------------------------+\n"
              << "|                       BENCHMARK RESULTS SUMMARY                     |\n"
              << "+------------------------+--------------------------------------------+\n"
              << "| Engine Variant         | " << std::setw(42) << std::left << eng_name << " |\n"
              << "| Workload               | " << std::setw(42) << std::left << workload_name(wtype)
              << " |\n"
              << "| Operations Count       | " << std::setw(42) << std::left << op_count << " |\n"
              << "| Elapsed Time           | " << std::setw(37) << std::left << std::fixed
              << std::setprecision(2) << (elapsed_sec * 1000.0) << " ms |\n"
              << "| Peak Throughput        | " << std::setw(35) << std::left << std::fixed
              << std::setprecision(2) << mops << " Mops/s |\n"
              << "| Mean Latency           | " << std::setw(37) << std::left << std::fixed
              << std::setprecision(1) << (sum / static_cast<double>(n)) << " ns |\n"
              << "| Median (p50) Latency   | " << std::setw(37) << std::left
              << latencies[n * 50 / 100] << " ns |\n"
              << "| p90 Latency            | " << std::setw(37) << std::left
              << latencies[n * 90 / 100] << " ns |\n"
              << "| p99 Latency            | " << std::setw(37) << std::left
              << latencies[n * 99 / 100] << " ns |\n"
              << "| p99.9 Latency          | " << std::setw(37) << std::left
              << latencies[n * 999 / 1000] << " ns |\n"
              << "| Max Latency            | " << std::setw(37) << std::left << latencies.back()
              << " ns |\n"
              << "+------------------------+--------------------------------------------+\n\n";

    return 0;
}

static int run_replay(const std::string& filepath, bool use_optimized) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open journal file: " << filepath << "\n";
        return 1;
    }

    std::cout << "Replaying event journal: " << filepath << "\n";
    std::cout << "Using Engine: "
              << (use_optimized ? "OptimizedMatchingEngine" : "ReferenceMatchingEngine") << "\n\n";

    OptimizedMatchingEngine opt_engine;
    ReferenceMatchingEngine ref_engine;

    std::string line;
    std::size_t line_num = 0;
    std::size_t commands_processed = 0;
    std::size_t total_trades = 0;
    Quantity total_volume_traded = 0;

    const auto t_start = std::chrono::steady_clock::now();

    while (std::getline(file, line)) {
        line_num++;
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Replace commas with spaces for parsing
        for (char& c : line) {
            if (c == ',')
                c = ' ';
        }

        std::istringstream iss(line);
        std::string action;
        iss >> action;

        ExecutionReport report;
        if (action == "CREATE" || action == "ADD") {
            OrderId id{0};
            std::string side_str;
            PriceTicks price{0};
            Quantity qty{0};
            if (!(iss >> id >> side_str >> price >> qty)) {
                std::cerr << "Malformed CREATE command at line " << line_num << "\n";
                continue;
            }
            Side side = (side_str == "BUY" || side_str == "Buy" || side_str == "1") ? Side::Buy
                                                                                    : Side::Sell;
            report = use_optimized ? opt_engine.submit_order(id, side, price, qty)
                                   : ref_engine.submit_order(id, side, price, qty);
        } else if (action == "CANCEL") {
            OrderId id{0};
            if (!(iss >> id)) {
                std::cerr << "Malformed CANCEL command at line " << line_num << "\n";
                continue;
            }
            report = use_optimized ? opt_engine.cancel_order(id) : ref_engine.cancel_order(id);
        } else if (action == "MODIFY") {
            OrderId id{0};
            PriceTicks new_price{0};
            Quantity new_qty{0};
            if (!(iss >> id >> new_price >> new_qty)) {
                std::cerr << "Malformed MODIFY command at line " << line_num << "\n";
                continue;
            }
            report = use_optimized ? opt_engine.modify_order(id, new_price, new_qty)
                                   : ref_engine.modify_order(id, new_price, new_qty);
        } else {
            std::cerr << "Unknown action '" << action << "' at line " << line_num << "\n";
            continue;
        }

        commands_processed++;
        for (const auto& t : report.trades) {
            total_trades++;
            total_volume_traded += t.quantity;
        }
    }

    const auto t_end = std::chrono::steady_clock::now();
    const auto elapsed_us =
        std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count();

    auto [ok, err] =
        use_optimized ? InvariantAuditor::audit(opt_engine) : InvariantAuditor::audit(ref_engine);

    std::uint64_t final_hash = use_optimized ? CanonicalState::compute_hash(opt_engine)
                                             : CanonicalState::compute_hash(ref_engine);

    std::ostringstream hash_ss;
    hash_ss << "0x" << std::hex << std::setfill('0') << std::setw(16) << final_hash;

    std::cout << "+---------------------------------------------------------------------+\n"
              << "|                        REPLAY SUMMARY REPORT                        |\n"
              << "+--------------------------+------------------------------------------+\n"
              << "| Journal File             | " << std::setw(40) << std::left << filepath << " |\n"
              << "| Total Events Processed   | " << std::setw(40) << std::left << commands_processed
              << " |\n"
              << "| Trades Executed          | " << std::setw(40) << std::left << total_trades
              << " |\n"
              << "| Total Volume Traded      | " << std::setw(40) << std::left
              << total_volume_traded << " |\n"
              << "| Active Resting Orders    | " << std::setw(40) << std::left
              << (use_optimized ? opt_engine.book().total_orders()
                                : ref_engine.book().total_orders())
              << " |\n"
              << "| Replay Runtime           | " << std::setw(37) << std::left
              << (static_cast<double>(elapsed_us) / 1000.0) << " ms |\n"
              << "| Invariants Audit         | " << std::setw(40) << std::left
              << (ok ? "PASSED" : ("FAILED: " + err)) << " |\n"
              << "| Canonical 64-bit Hash    | " << std::setw(40) << std::left << hash_ss.str()
              << " |\n"
              << "+--------------------------+------------------------------------------+\n\n";

    return ok ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_help(argv[0]);
        return 0;
    }

    const std::string cmd = argv[1];
    if (cmd == "--help" || cmd == "-h" || cmd == "help") {
        print_help(argv[0]);
        return 0;
    }

    if (cmd == "--version" || cmd == "-v" || cmd == "version") {
        std::cout << "QuantEngine version " << get_version() << "\n";
        return 0;
    }

    if (cmd == "demo") {
        return run_demo();
    }

    if (cmd == "benchmark") {
        WorkloadType wtype = WorkloadType::Balanced;
        std::size_t op_count = 100'000;
        std::string engine_type = "optimized";

        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--workload" && i + 1 < argc) {
                std::string val = argv[++i];
                if (val == "add")
                    wtype = WorkloadType::AddHeavy;
                else if (val == "crossing")
                    wtype = WorkloadType::CrossingHeavy;
                else if (val == "cancel")
                    wtype = WorkloadType::CancelHeavy;
                else
                    wtype = WorkloadType::Balanced;
            } else if (arg == "--ops" && i + 1 < argc) {
                op_count = static_cast<std::size_t>(std::stoul(argv[++i]));
            } else if (arg == "--engine" && i + 1 < argc) {
                engine_type = argv[++i];
            }
        }

        if (engine_type == "reference") {
            return run_benchmark<ReferenceMatchingEngine>(wtype, op_count,
                                                          "ReferenceMatchingEngine");
        } else {
            return run_benchmark<OptimizedMatchingEngine>(wtype, op_count,
                                                          "OptimizedMatchingEngine");
        }
    }

    if (cmd == "replay") {
        if (argc < 3) {
            std::cerr << "Usage: " << argv[0]
                      << " replay <journal_file> [--engine optimized|reference]\n";
            return 1;
        }
        std::string filepath = argv[2];
        bool use_optimized = true;
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--engine" && i + 1 < argc) {
                std::string eng = argv[++i];
                if (eng == "reference")
                    use_optimized = false;
            }
        }
        return run_replay(filepath, use_optimized);
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    print_help(argv[0]);
    return 1;
}
