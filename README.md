# QuantEngine

> Deterministic C++20 Limit Order Matching Engine with Python Bindings

QuantEngine is a high-integrity, deterministic limit-order matching engine and order-book simulator written in modern C++20, featuring fixed-point arithmetic, canonical state hashing, differential fuzz testing, and zero-overhead batch bindings via Pybind11.

---

## Key Design Principles

1. **Correctness First**: All matching behaviors are verified against a reference model through differential testing.
2. **Deterministic Replay**: No wall-clock dependencies in core matching logic. Every event stream reproduces identical state hashes.
3. **Fixed-Point Price Representation**: Core order book operates purely in integer ticks (`PriceTicks` / `std::int64_t`), preventing IEEE 754 floating-point inaccuracies.
4. **Measured Performance**: Optimizations are accepted only when backed by reproducible microbenchmarks and profiling.

---

## Quickstart

### Prerequisites
- C++20 compliant compiler (GCC 13+ or Clang 16+)
- CMake 3.28+
- Python 3.10+ (for Python bindings)

### Build with CMake Presets
```bash
# Configure debug build
cmake --preset dev-debug

# Build targets
cmake --build --preset dev-debug

# Run test suite
ctest --preset dev-debug
```

### Sanitizer Build
```bash
cmake --preset dev-sanitizer
cmake --build --preset dev-sanitizer
ctest --preset dev-sanitizer
```

---

## Architecture & Documentation
- [Architecture Overview](docs/ARCHITECTURE.md)
- [Design Decisions](docs/DESIGN_DECISIONS.md)
- [Benchmark Methodology & Results](docs/BENCHMARKS.md)
