#pragma once

// quantengine/replay/FifoQueueModel.hpp
//
// Conservative deterministic FIFO queue model.
//
// PRINCIPLES:
//   1. Initial queue ahead equals the exact resting volume at that price level
//      when the simulated order was placed.
//   2. Cancellations of orders placed BEFORE the simulated order (lower sequence_number)
//      reduce queue_ahead by min(queue_ahead, cancelled_qty).
//   3. Cancellations of orders placed AFTER the simulated order do NOT reduce queue_ahead.
//   4. Executions at the same price level deplete queue_ahead first. Once queue_ahead reaches
//      zero, subsequent executed quantity fills the simulated order.
//   5. Executions strictly through the simulated order's price (e.g. trades printed below
//      our bid or above our ask) fill remaining quantity immediately.

#include "quantengine/replay/IQueueModel.hpp"

namespace quantengine::replay {

class FifoQueueModel final : public IQueueModel {
public:
    FifoQueueModel() = default;
    ~FifoQueueModel() override = default;

    [[nodiscard]] auto initial_queue_ahead(const HistoricalL3Book& book, core::PriceTicks price,
                                           core::Side side, core::Quantity quantity) noexcept
        -> core::Quantity override;

    auto on_historical_execute(
        core::Quantity& queue_ahead, core::Quantity& order_remaining_qty,
        core::PriceTicks order_price, core::Side order_side, const OrderExecuted& exec,
        const HistoricalOrder* executed_historical_order) noexcept -> FillResult override;

    void on_historical_cancel(core::Quantity& queue_ahead, core::PriceTicks order_price,
                              core::Side order_side, std::uint64_t order_join_seq,
                              const OrderCancelled& cancel,
                              const HistoricalOrder* cancelled_historical_order) noexcept override;

    [[nodiscard]] auto model_name() const noexcept -> std::string_view override {
        return "ConservativeFIFO";
    }
};

}  // namespace quantengine::replay
