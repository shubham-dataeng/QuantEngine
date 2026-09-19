#pragma once

#include <optional>
#include <string>

#include "quantengine/engine/matching_engine.hpp"

namespace quantengine::engine {

class InvariantAuditor {
public:
    struct AuditResult {
        bool ok{true};
        std::string error_message{};

        [[nodiscard]] constexpr explicit operator bool() const noexcept { return ok; }
    };

    // Comprehensive audit of all engine invariants
    [[nodiscard]] static AuditResult audit(const MatchingEngine& engine);

    // Specific invariant checks
    [[nodiscard]] static bool is_book_uncrossed(const reference::ReferenceOrderBook& book) noexcept;
    [[nodiscard]] static bool are_volumes_consistent(
        const reference::ReferenceOrderBook& book) noexcept;
};

}  // namespace quantengine::engine
