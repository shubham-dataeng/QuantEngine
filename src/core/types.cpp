#include "quantengine/core/types.hpp"

#include <ostream>

namespace quantengine::core {

std::ostream& operator<<(std::ostream& os, Side side) {
    return os << to_string(side);
}

std::ostream& operator<<(std::ostream& os, OrderStatus status) {
    return os << to_string(status);
}

std::ostream& operator<<(std::ostream& os, OrderType type) {
    return os << to_string(type);
}

std::ostream& operator<<(std::ostream& os, RejectReason reason) {
    return os << to_string(reason);
}

}  // namespace quantengine::core
