#pragma once

#include <iosfwd>

#include "quantengine/core/types.hpp"

namespace quantengine::core {

struct Trade {
    TradeId trade_id{0};
    OrderId maker_order_id{0};
    OrderId taker_order_id{0};
    Side maker_side{Side::Sell};
    PriceTicks price{0};
    Quantity quantity{0};
    SeqNum sequence_number{0};

    [[nodiscard]] constexpr bool operator==(const Trade& other) const noexcept = default;
};

std::ostream& operator<<(std::ostream& os, const Trade& trade);

}  // namespace quantengine::core
