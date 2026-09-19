#pragma once

#include <vector>

#include "quantengine/core/events.hpp"
#include "quantengine/engine/canonical_state.hpp"
#include "quantengine/engine/invariants.hpp"
#include "quantengine/engine/matching_engine.hpp"

namespace quantengine::engine {

struct ReplayResult {
    CanonicalState::Hash final_hash{0};
    std::vector<core::ExecutionReport> execution_reports{};
    std::vector<core::Trade> all_trades{};
    bool invariants_satisfied{true};
    std::string failure_reason{};
};

class ReplayEngine {
public:
    // Replay a sequence of order commands from a clean engine state
    [[nodiscard]] static ReplayResult replay(const std::vector<core::OrderCommand>& commands);

    // Verify determinism: runs two completely separate engine instances over the exact same
    // event sequence and verifies bit-for-bit equivalence of reports, trades, and final state
    // hashes
    [[nodiscard]] static bool verify_determinism(const std::vector<core::OrderCommand>& commands);
};

}  // namespace quantengine::engine
