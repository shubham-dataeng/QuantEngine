#include "quantengine/engine/invariants.hpp"

#include <sstream>

namespace quantengine::engine {

bool InvariantAuditor::is_book_uncrossed(const reference::ReferenceOrderBook& book) noexcept {
    const auto best_bid = book.best_bid_price();
    const auto best_ask = book.best_ask_price();

    if (best_bid && best_ask) {
        return *best_bid < *best_ask;
    }
    return true;
}

bool InvariantAuditor::are_volumes_consistent(const reference::ReferenceOrderBook& book) noexcept {
    core::Quantity sum_bids = 0;
    for (const auto& level : book.get_bids()) {
        sum_bids += level.total_quantity;
    }
    if (sum_bids != book.total_bid_volume()) {
        return false;
    }

    core::Quantity sum_asks = 0;
    for (const auto& level : book.get_asks()) {
        sum_asks += level.total_quantity;
    }
    if (sum_asks != book.total_ask_volume()) {
        return false;
    }

    return true;
}

InvariantAuditor::AuditResult InvariantAuditor::audit(const MatchingEngine& engine) {
    const auto& book = engine.book();

    if (!book.check_invariants()) {
        return AuditResult{false, "Book internal invariant check failed"};
    }

    if (!is_book_uncrossed(book)) {
        std::ostringstream oss;
        oss << "Book crossed invariant violated: best_bid=" << *book.best_bid_price()
            << " >= best_ask=" << *book.best_ask_price();
        return AuditResult{false, oss.str()};
    }

    if (!are_volumes_consistent(book)) {
        return AuditResult{false, "Book volume consistency invariant violated"};
    }

    return AuditResult{true, ""};
}

}  // namespace quantengine::engine
