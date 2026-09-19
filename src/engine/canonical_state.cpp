#include "quantengine/engine/canonical_state.hpp"

#include <cstring>
#include <iomanip>
#include <sstream>

namespace quantengine::engine {

namespace {

inline void append_u32(std::vector<std::uint8_t>& buffer, std::uint32_t val) {
    buffer.push_back(static_cast<std::uint8_t>(val & 0xFF));
    buffer.push_back(static_cast<std::uint8_t>((val >> 8) & 0xFF));
    buffer.push_back(static_cast<std::uint8_t>((val >> 16) & 0xFF));
    buffer.push_back(static_cast<std::uint8_t>((val >> 24) & 0xFF));
}

inline void append_u64(std::vector<std::uint8_t>& buffer, std::uint64_t val) {
    for (int i = 0; i < 8; ++i) {
        buffer.push_back(static_cast<std::uint8_t>((val >> (i * 8)) & 0xFF));
    }
}

inline void append_i64(std::vector<std::uint8_t>& buffer, std::int64_t val) {
    std::uint64_t uval;
    std::memcpy(&uval, &val, sizeof(uval));
    append_u64(buffer, uval);
}

}  // namespace

CanonicalState::Hash CanonicalState::fnv1a_64(const std::uint8_t* data, std::size_t size) noexcept {
    Hash hash = 14695981039346656037ULL;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::vector<std::uint8_t> CanonicalState::serialize(const MatchingEngine& engine) {
    std::vector<std::uint8_t> buffer;
    buffer.reserve(1024);

    // Header
    append_u64(buffer, engine.current_sequence());
    append_u64(buffer, engine.current_trade_id());

    const auto& book = engine.book();

    // Bids: price levels ordered descending (highest bid first)
    const auto bids = book.get_bids();
    append_u32(buffer, static_cast<std::uint32_t>(bids.size()));
    for (const auto& level : bids) {
        append_i64(buffer, level.price);
        append_u64(buffer, level.total_quantity);
        append_u32(buffer, static_cast<std::uint32_t>(level.order_count));
    }

    // Asks: price levels ordered ascending (lowest ask first)
    const auto asks = book.get_asks();
    append_u32(buffer, static_cast<std::uint32_t>(asks.size()));
    for (const auto& level : asks) {
        append_i64(buffer, level.price);
        append_u64(buffer, level.total_quantity);
        append_u32(buffer, static_cast<std::uint32_t>(level.order_count));
    }

    return buffer;
}

CanonicalState::Hash CanonicalState::compute_hash(const MatchingEngine& engine) {
    const auto serialized = serialize(engine);
    return fnv1a_64(serialized.data(), serialized.size());
}

std::string CanonicalState::to_string(const MatchingEngine& engine) {
    std::ostringstream oss;
    oss << "CanonicalState{\n"
        << "  seq=" << engine.current_sequence() << "\n"
        << "  trade_id=" << engine.current_trade_id() << "\n";

    const auto& book = engine.book();
    oss << "  bids=[\n";
    for (const auto& level : book.get_bids()) {
        oss << "    Level{px=" << level.price << ", qty=" << level.total_quantity
            << ", orders=" << level.order_count << "}\n";
    }
    oss << "  ]\n";

    oss << "  asks=[\n";
    for (const auto& level : book.get_asks()) {
        oss << "    Level{px=" << level.price << ", qty=" << level.total_quantity
            << ", orders=" << level.order_count << "}\n";
    }
    oss << "  ]\n"
        << "  hash=0x" << std::hex << std::setfill('0') << std::setw(16) << compute_hash(engine)
        << "\n"
        << "}";
    return oss.str();
}

}  // namespace quantengine::engine
