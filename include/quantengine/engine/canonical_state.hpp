#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "quantengine/engine/matching_engine.hpp"

namespace quantengine::engine {

class CanonicalState {
public:
    using Hash = std::uint64_t;

    // Deterministic little-endian binary serialization of the entire engine state
    [[nodiscard]] static std::vector<std::uint8_t> serialize(const MatchingEngine& engine);

    // Compute canonical 64-bit FNV-1a hash over the serialized binary state
    [[nodiscard]] static Hash compute_hash(const MatchingEngine& engine);

    // Human-readable canonical state representation for debugging & inspection
    [[nodiscard]] static std::string to_string(const MatchingEngine& engine);

    // 64-bit FNV-1a algorithm
    [[nodiscard]] static Hash fnv1a_64(const std::uint8_t* data, std::size_t size) noexcept;
};

}  // namespace quantengine::engine
