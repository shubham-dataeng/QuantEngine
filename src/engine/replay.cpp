#include "quantengine/engine/replay.hpp"

namespace quantengine::engine {

using namespace quantengine::core;

ReplayResult ReplayEngine::replay(const std::vector<OrderCommand>& commands) {
    MatchingEngine engine;
    ReplayResult result;
    result.execution_reports.reserve(commands.size());

    for (const auto& cmd : commands) {
        auto report = engine.process_command(cmd);
        for (const auto& trade : report.trades) {
            result.all_trades.push_back(trade);
        }
        result.execution_reports.push_back(std::move(report));

        // Audit invariants after every single event
        auto audit = InvariantAuditor::audit(engine);
        if (!audit.ok) {
            result.invariants_satisfied = false;
            result.failure_reason = audit.error_message;
            result.final_hash = CanonicalState::compute_hash(engine);
            return result;
        }
    }

    result.final_hash = CanonicalState::compute_hash(engine);
    result.invariants_satisfied = true;
    return result;
}

bool ReplayEngine::verify_determinism(const std::vector<OrderCommand>& commands) {
    const auto run1 = replay(commands);
    const auto run2 = replay(commands);

    if (!run1.invariants_satisfied || !run2.invariants_satisfied) {
        return false;
    }

    if (run1.final_hash != run2.final_hash) {
        return false;
    }

    if (run1.execution_reports.size() != run2.execution_reports.size()) {
        return false;
    }

    for (std::size_t i = 0; i < run1.execution_reports.size(); ++i) {
        const auto& r1 = run1.execution_reports[i];
        const auto& r2 = run2.execution_reports[i];
        if (r1 != r2) {
            return false;
        }
    }

    if (run1.all_trades.size() != run2.all_trades.size()) {
        return false;
    }

    for (std::size_t i = 0; i < run1.all_trades.size(); ++i) {
        if (run1.all_trades[i] != run2.all_trades[i]) {
            return false;
        }
    }

    return true;
}

}  // namespace quantengine::engine
