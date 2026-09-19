import quantengine as qe
import pytest


def test_side_enum():
    assert qe.Side.Buy != qe.Side.Sell
    assert qe.Buy == qe.Side.Buy
    assert qe.Sell == qe.Side.Sell


def test_order_status_enum():
    assert qe.OrderStatus.New != qe.OrderStatus.Filled
    assert qe.OrderStatus.Resting != qe.OrderStatus.Cancelled
    assert qe.OrderStatus.PartiallyFilled != qe.OrderStatus.Rejected


def test_reject_reason_enum():
    assert qe.RejectReason.NoReject == getattr(qe.RejectReason, "None")
    assert qe.RejectReason.DuplicateOrderId != qe.RejectReason.UnknownOrder
    assert qe.RejectReason.InvalidPrice != qe.RejectReason.InvalidQuantity


def test_order_class():
    order = qe.Order(
        order_id=101,
        side=qe.Side.Buy,
        price=10000,
        quantity=50,
        sequence_number=1,
    )
    assert order.order_id == 101
    assert order.side == qe.Side.Buy
    assert order.price == 10000
    assert order.initial_quantity == 50
    assert order.remaining_quantity == 50
    assert order.filled_quantity == 0
    assert not order.is_filled()
    assert "<Order id=101" in repr(order)


def test_trade_class():
    trade = qe.Trade(
        trade_id=1,
        maker_order_id=10,
        taker_order_id=20,
        maker_side=qe.Side.Sell,
        price=9950,
        quantity=30,
        sequence_number=5,
    )
    assert trade.trade_id == 1
    assert trade.maker_order_id == 10
    assert trade.taker_order_id == 20
    assert trade.maker_side == qe.Side.Sell
    assert trade.price == 9950
    assert trade.quantity == 30
    assert trade.sequence_number == 5
    assert "<Trade id=1" in repr(trade)


def test_level_info_class():
    level = qe.LevelInfo()
    level.price = 10050
    level.total_quantity = 500
    level.order_count = 5
    assert level.price == 10050
    assert level.total_quantity == 500
    assert level.order_count == 5
    assert "<LevelInfo price=10050" in repr(level)


def test_batch_order_class():
    bo = qe.BatchOrder(order_id=42, side=qe.Side.Buy, price=10100, quantity=250)
    assert bo.order_id == 42
    assert bo.side == qe.Side.Buy
    assert bo.price == 10100
    assert bo.quantity == 250
    assert "<BatchOrder id=42" in repr(bo)


def test_command_classes():
    create = qe.CreateOrderCommand(order_id=1, side=qe.Side.Buy, price=10000, quantity=10)
    assert create.order_id == 1
    assert create.side == qe.Side.Buy
    assert create.price == 10000
    assert create.quantity == 10

    cancel = qe.CancelOrderCommand(order_id=1)
    assert cancel.order_id == 1

    modify = qe.ModifyOrderCommand(order_id=1, new_price=10050, new_quantity=20)
    assert modify.order_id == 1
    assert modify.new_price == 10050
    assert modify.new_quantity == 20

    cmd = qe.OrderCommand(sequence_number=99, payload=create)
    assert cmd.sequence_number == 99
