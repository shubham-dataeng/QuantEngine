# QuantEngine

[![CI](https://github.com/shubham-dataeng/QuantEngine/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/shubham-dataeng/QuantEngine/actions/workflows/ci.yml)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Python](https://img.shields.io/badge/Python-3.10%2B-blue.svg)](https://www.python.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

> **Deterministic, High-Throughput C++20 Limit Order Matching Engine & Order-Book Simulator with Python Bindings**

QuantEngine is a high-integrity, cache-conscious financial matching engine and simulation framework written in modern C++20. Designed from first principles for quantitative finance systems, high-frequency backtesting, and market microstructure simulation, QuantEngine couples bit-level determinism with zero-allocation data structures delivering **over 11.7 million operations per second** with sub-100 nanosecond execution latency.

---

## Key Highlights

* **Bit-for-Bit Determinism**: Eliminates wall-clock time in the hot path. Every event stream reproduces identical 64-bit FNV-1a canonical state hashes across machines.
* **Dual-Engine Architecture with Differential Fuzzing**: Features both a ground-truth `ReferenceOrderBook` (STL) and a high-performance `OptimizedOrderBook` (zero-allocation memory pool) verified under millions of randomized fuzzing operations.
* **Cache-Aligned Zero-Allocation Core**: Pre-allocated contiguous arena memory pool (`OrderPool`) with 32-bit intrusive index-linked price queues. No runtime heap allocations (`malloc`/`new`) on the critical trading path.
* **Fixed-Point Tick Arithmetic**: Prevents IEEE 754 floating-point inaccuracies by strictly representing prices as integer multiples of minimum tick increments (`PriceTicks` / `std::int64_t`).
* **High-Throughput Python Bindings (Pybind11)**: Native Python bindings with GIL release support processing **1.84+ million orders per second** in batch mode.
* **Interactive CLI Tools**: Rich command-line interface featuring real-time visual ASCII depth ladders, journal replay, and calibrated latency profiling.

---

## Performance Benchmarks

Measured on an **AMD Ryzen 7 7840HS** (Zen 4, 8C/16T @ 5.14 GHz, Linux 6.8, GCC 13.3.0 `-O3`) via **Google Benchmark v1.8.3**:

| Workload | Engine Variant | Peak Throughput | Mean Latency | Median (p50) | 99th Percentile (p99) | Tail Reduction |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| **Balanced (50% Add, 25% Cxl, 25% Trd)** | Reference | 10.65 Mops/s | 93.9 ns | 141 ns | 854 ns | — |
| **Balanced** | **Optimized** | **11.79 Mops/s** | **84.8 ns** | **90 ns** | **281 ns** | **3.0x lower p99** |
| **Add-Heavy (90% Add, 10% Cxl)** | Reference | 6.55 Mops/s | 152.7 ns | 101 ns | 2,038 ns | — |
| **Add-Heavy** | **Optimized** | **10.91 Mops/s** | **91.7 ns** | **81 ns** | **331 ns** | **6.1x lower p99** |
| **Crossing-Heavy (Sweep / Match)** | Reference | 14.02 Mops/s | 71.3 ns | 70 ns | 401 ns | — |
| **Crossing-Heavy** | **Optimized** | **15.14 Mops/s** | **66.0 ns** | **70 ns** | **392 ns** | **1.02x lower p99** |
| **Cancel-Heavy (50% Add, 50% Cxl)** | Reference | 10.96 Mops/s | 91.2 ns | 100 ns | 191 ns | — |
| **Cancel-Heavy** | **Optimized** | **11.33 Mops/s** | **88.2 ns** | **100 ns** | **171 ns** | **1.12x lower p99** |

*For complete methodology and Google Benchmark profiles, see [docs/BENCHMARKS.md](docs/BENCHMARKS.md).*

---

## Architecture at a Glance

```
                                  +---------------------------------------+
                                  |            Client Ingestion           |
                                  |   (CLI / C++ Core / Python Pybind11)  |
                                  +-------------------+-------------------+
                                                      |
                                                      v
                                  +---------------------------------------+
                                  |     GenericMatchingEngine<BookType>   |
                                  |  Continuous Price-Time Matching Loop  |
                                  +-------------------+-------------------+
                                                      |
                        +-----------------------------+-----------------------------+
                        |                                                           |
                        v                                                           v
      +-----------------------------------+               +-----------------------------------+
      |        ReferenceOrderBook         |               |        OptimizedOrderBook         |
      |-----------------------------------|               |-----------------------------------|
      | - std::map<PriceTicks, ...>       |               | - FlatHashMap<Price, Level>       |
      | - std::list<core::Order>          |               | - Contiguous OrderPool (48B/node) |
      | - std::unordered_map<Id, ...>     |               | - Intrusive Doubly-Linked Lists   |
      | - Baseline ground truth           |               | - Zero runtime heap allocations   |
      +-----------------+-----------------+               +-----------------+-----------------+
                        |                                                           |
                        +-----------------------------+-----------------------------+
                                                      |
                                                      v
                                  +---------------------------------------+
                                  |            Audit & Replay             |
                                  |---------------------------------------|
                                  | - InvariantAuditor (Crossed/Volume)   |
                                  | - CanonicalState (FNV-1a 64-bit Hash) |
                                  | - Bit-level Differential Equivalence  |
                                  +---------------------------------------+
```

*For detailed architectural specifications, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [docs/DESIGN_DECISIONS.md](docs/DESIGN_DECISIONS.md).*

---

## Quickstart

### Prerequisites
* C++20 compliant compiler (`g++-13` or `clang++-16`+)
* CMake 3.28+
* Python 3.10+ (for Python bindings and pytest test suite)

### 1. Build and Run via CMake Presets

```bash
# Clone repository
git clone https://github.com/shubham-dataeng/QuantEngine.git
cd QuantEngine

# Build & run tests in Release mode
cmake --preset dev-release
cmake --build --preset dev-release
ctest --preset dev-release --output-on-failure

# Build with AddressSanitizer & UndefinedBehaviorSanitizer
cmake --preset dev-sanitizer
cmake --build --preset dev-sanitizer
ctest --preset dev-sanitizer --output-on-failure
```

### 2. Interactive CLI

The CLI provides ready-to-run tools for visual matching simulations, microbenchmarks, and journal replays:

```bash
# Visual order book ladder simulation with aggressive sweeping orders
./build/dev-release/quantengine_cli demo

# Microbenchmark throughput and tail latency percentiles
./build/dev-release/quantengine_cli benchmark --workload balanced --ops 100000

# Replay an event journal and verify bit-for-bit state invariants & canonical hash
./build/dev-release/quantengine_cli replay examples/sample_journal.csv
```

### 3. Python Installation & Usage

Install the package directly via `pip`:

```bash
pip install .
```

Quick Python example:

```python
import quantengine as qe

# 1. Instantiate the high-performance matching engine
engine = qe.OptimizedMatchingEngine()

# 2. Place resting limit orders on the book
engine.submit_order(order_id=1, side=qe.Side.Buy, price=10000, quantity=50)
engine.submit_order(order_id=2, side=qe.Side.Buy, price=9950, quantity=100)
engine.submit_order(order_id=3, side=qe.Side.Sell, price=10050, quantity=75)

# 3. Submit an aggressive crossing order that sweeps liquidity
report = engine.submit_order(order_id=4, side=qe.Side.Sell, price=10000, quantity=50)

print(f"Status: {report.status}")
print(f"Filled Qty: {report.filled_qty}")
for trade in report.trades:
    print(f"Trade Executed: Maker={trade.maker_order_id} Taker={trade.taker_order_id} "
          f"Price={trade.price} Qty={trade.quantity}")

# 4. Compute deterministic 64-bit canonical state hash
state_hash = engine.canonical_hash()
print(f"Canonical State Hash: {hex(state_hash)}")
```

---

## High-Throughput Batch API

For quantitative strategies and backtesting pipelines, QuantEngine provides GIL-released batch processing:

```python
import quantengine as qe

engine = qe.OptimizedMatchingEngine()

# Create a batch of market orders
commands = [
    qe.OrderCommand(seq=i, payload=qe.CreateOrderCommand(order_id=i, side=qe.Side.Buy, price=10000, quantity=10))
    for i in range(1, 100000)
]

# Process entire batch in C++ with Python GIL released
reports = engine.submit_batch(commands)
print(f"Processed {len(reports)} orders at native C++ speeds!")
```

---

## Test Suite & Verification

QuantEngine maintains 100% automated test coverage across both C++ and Python:

```bash
# Run all 52 unit tests, fuzz tests, and pytest suites
ctest --preset dev-release --output-on-failure
```

* **Unit Tests**: Full coverage of order lifecycle, execution reports, FIFO queues, maker-price matching, and level sweeps.
* **Differential Fuzz Testing**: Continuous fuzz tests generating millions of randomized commands verifying bit-for-bit equivalence between `ReferenceOrderBook` and `OptimizedOrderBook`.
* **Invariant Auditing**: Strict checks validating uncrossed books, volume parity, and slot recycling across every state transition.
* **Memory & Thread Safety**: Zero leaks or memory errors under AddressSanitizer and UndefinedBehaviorSanitizer.

---

## Documentation

* [Systems Architecture Specification](docs/ARCHITECTURE.md)
* [Design Decisions & Trade-offs](docs/DESIGN_DECISIONS.md)
* [Empirical Benchmarking Report](docs/BENCHMARKS.md)

---

## License

QuantEngine is licensed under the [MIT License](LICENSE).
