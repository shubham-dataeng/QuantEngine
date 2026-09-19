#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include "quantengine/core/events.hpp"
#include "quantengine/core/types.hpp"

namespace quantengine::test {

class FuzzGenerator {
public:
    explicit FuzzGenerator(std::uint64_t seed = 42) : rng_(seed) {}

    std::vector<core::OrderCommand> generate_commands(std::size_t count) {
        std::vector<core::OrderCommand> commands;
        commands.reserve(count);

        std::vector<core::OrderId> active_order_ids;
        core::OrderId next_order_id = 1;
        core::SeqNum seq = 1;

        std::uniform_int_distribution<int> action_dist(0, 9);
        std::uniform_int_distribution<int> side_dist(0, 1);
        std::uniform_int_distribution<int64_t> price_dist(9900, 10100);
        std::uniform_int_distribution<uint64_t> qty_dist(5, 100);

        for (std::size_t i = 0; i < count; ++i) {
            const int action = action_dist(rng_);

            if (action < 6 || active_order_ids.empty()) {
                // 60%: Create Order
                const auto id = next_order_id++;
                const auto side = (side_dist(rng_) == 0) ? core::Side::Buy : core::Side::Sell;
                const auto price = price_dist(rng_);
                const auto qty = qty_dist(rng_);

                active_order_ids.push_back(id);
                commands.push_back(
                    core::OrderCommand{.sequence_number = seq++,
                                       .payload = core::CreateOrderCommand{id, side, price, qty}});
            } else if (action < 8) {
                // 20%: Cancel Order
                std::uniform_int_distribution<std::size_t> idx_dist(0, active_order_ids.size() - 1);
                const auto idx = idx_dist(rng_);
                const auto id = active_order_ids[idx];

                // Remove from local tracked pool
                active_order_ids[idx] = active_order_ids.back();
                active_order_ids.pop_back();

                commands.push_back(core::OrderCommand{.sequence_number = seq++,
                                                      .payload = core::CancelOrderCommand{id}});
            } else {
                // 20%: Modify Order
                std::uniform_int_distribution<std::size_t> idx_dist(0, active_order_ids.size() - 1);
                const auto id = active_order_ids[idx_dist(rng_)];
                const auto new_price = price_dist(rng_);
                const auto new_qty = qty_dist(rng_);

                commands.push_back(core::OrderCommand{
                    .sequence_number = seq++,
                    .payload = core::ModifyOrderCommand{id, new_price, new_qty}});
            }
        }

        return commands;
    }

private:
    std::mt19937_64 rng_;
};

}  // namespace quantengine::test
