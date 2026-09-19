#pragma once

#include <iosfwd>

#include "quantengine/core/types.hpp"

namespace quantengine::core {

class Order {
public:
    constexpr Order() noexcept = default;

    constexpr Order(OrderId id, Side side, PriceTicks price, Quantity quantity,
                    SeqNum seq = 0) noexcept
        : order_id_(id),
          side_(side),
          price_(price),
          initial_quantity_(quantity),
          remaining_quantity_(quantity),
          sequence_number_(seq),
          status_(OrderStatus::New) {}

    [[nodiscard]] constexpr OrderId order_id() const noexcept { return order_id_; }
    [[nodiscard]] constexpr Side side() const noexcept { return side_; }
    [[nodiscard]] constexpr PriceTicks price() const noexcept { return price_; }
    [[nodiscard]] constexpr Quantity initial_quantity() const noexcept { return initial_quantity_; }
    [[nodiscard]] constexpr Quantity remaining_quantity() const noexcept {
        return remaining_quantity_;
    }
    [[nodiscard]] constexpr Quantity filled_quantity() const noexcept {
        return initial_quantity_ - remaining_quantity_;
    }
    [[nodiscard]] constexpr SeqNum sequence_number() const noexcept { return sequence_number_; }
    [[nodiscard]] constexpr OrderStatus status() const noexcept { return status_; }

    [[nodiscard]] constexpr bool is_active() const noexcept {
        return status_ == OrderStatus::Resting || status_ == OrderStatus::PartiallyFilled;
    }

    [[nodiscard]] constexpr bool is_terminal() const noexcept {
        return status_ == OrderStatus::Filled || status_ == OrderStatus::Cancelled ||
               status_ == OrderStatus::Rejected;
    }

    // State machine transitions
    [[nodiscard]] bool mark_resting() noexcept;
    [[nodiscard]] bool apply_fill(Quantity fill_qty) noexcept;
    [[nodiscard]] bool mark_cancelled() noexcept;
    [[nodiscard]] bool mark_rejected() noexcept;

    // Invariants
    [[nodiscard]] bool check_invariants() const noexcept;

    // Modification helper: updates remaining quantity and priority sequence
    void update_priority_sequence(SeqNum new_seq) noexcept { sequence_number_ = new_seq; }
    void update_remaining_quantity(Quantity new_qty) noexcept {
        remaining_quantity_ = new_qty;
        if (new_qty > initial_quantity_) {
            initial_quantity_ = new_qty;
        }
    }

    [[nodiscard]] constexpr bool operator==(const Order& other) const noexcept = default;

private:
    OrderId order_id_{0};
    Side side_{Side::Buy};
    PriceTicks price_{0};
    Quantity initial_quantity_{0};
    Quantity remaining_quantity_{0};
    SeqNum sequence_number_{0};
    OrderStatus status_{OrderStatus::New};
};

std::ostream& operator<<(std::ostream& os, const Order& order);

}  // namespace quantengine::core
