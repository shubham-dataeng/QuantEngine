#include "quantengine/core/trade.hpp"

#include <ostream>

namespace quantengine::core {

std::ostream& operator<<(std::ostream& os, const Trade& trade) {
    return os << "Trade{id=" << trade.trade_id << ", maker=" << trade.maker_order_id
              << ", taker=" << trade.taker_order_id << ", side=" << trade.maker_side
              << ", px=" << trade.price << ", qty=" << trade.quantity
              << ", seq=" << trade.sequence_number << "}";
}

}  // namespace quantengine::core
