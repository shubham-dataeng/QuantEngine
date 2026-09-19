import quantengine as qe
import pytest


@pytest.mark.parametrize("engine_cls", [qe.ReferenceMatchingEngine, qe.OptimizedMatchingEngine])
def test_passive_resting_orders(engine_cls):
    engine = engine_cls()
    assert engine.is_empty()

    r1 = engine.submit_order(order_id=1, side=qe.Side.Buy, price=9950, quantity=100)
    assert r1.order_id == 1
    assert r1.status == qe.OrderStatus.Resting
    assert r1.remaining_quantity == 100
    assert r1.filled_quantity == 0
    assert len(r1.trades) == 0

    r2 = engine.submit_order(order_id=2, side=qe.Side.Sell, price=10050, quantity=200)
    assert r2.order_id == 2
    assert r2.status == qe.OrderStatus.Resting

    assert engine.best_bid_price() == 9950
    assert engine.best_ask_price() == 10050
    assert engine.best_bid_quantity() == 100
    assert engine.best_ask_quantity() == 200
    assert engine.total_bid_volume() == 100
    assert engine.total_ask_volume() == 200
    assert engine.total_orders() == 2
    assert not engine.is_empty()

    bids = engine.get_bids()
    assert len(bids) == 1
    assert bids[0].price == 9950
    assert bids[0].total_quantity == 100
    assert bids[0].order_count == 1

    asks = engine.get_asks()
    assert len(asks) == 1
    assert asks[0].price == 10050
    assert asks[0].total_quantity == 200


@pytest.mark.parametrize("engine_cls", [qe.ReferenceMatchingEngine, qe.OptimizedMatchingEngine])
def test_full_crossing_fill_and_maker_price_rule(engine_cls):
    engine = engine_cls()

    # Maker rests sell order at 10000
    engine.submit_order(order_id=1, side=qe.Side.Sell, price=10000, quantity=100)

    # Taker aggressively buys at 10050
    report = engine.submit_order(order_id=2, side=qe.Side.Buy, price=10050, quantity=100)

    assert report.status == qe.OrderStatus.Filled
    assert report.remaining_quantity == 0
    assert report.filled_quantity == 100
    assert len(report.trades) == 1

    trade = report.trades[0]
    assert trade.maker_order_id == 1
    assert trade.taker_order_id == 2
    assert trade.maker_side == qe.Side.Sell
    # Maker price rule: trade executes at maker's resting price (10000), not taker's price (10050)
    assert trade.price == 10000
    assert trade.quantity == 100

    assert engine.is_empty()
    assert engine.best_ask_price() is None
    assert engine.best_bid_price() is None


@pytest.mark.parametrize("engine_cls", [qe.ReferenceMatchingEngine, qe.OptimizedMatchingEngine])
def test_partial_fill_taker_rests_remaining(engine_cls):
    engine = engine_cls()

    # Maker sells 40 @ 10000
    engine.submit_order(order_id=1, side=qe.Side.Sell, price=10000, quantity=40)

    # Taker buys 100 @ 10000
    report = engine.submit_order(order_id=2, side=qe.Side.Buy, price=10000, quantity=100)

    assert report.status == qe.OrderStatus.PartiallyFilled
    assert report.remaining_quantity == 60
    assert report.filled_quantity == 40
    assert len(report.trades) == 1
    assert report.trades[0].quantity == 40

    # Remaining 60 shares should now rest on the bid side
    assert engine.best_bid_price() == 10000
    assert engine.best_bid_quantity() == 60
    assert engine.best_ask_price() is None


@pytest.mark.parametrize("engine_cls", [qe.ReferenceMatchingEngine, qe.OptimizedMatchingEngine])
def test_fifo_priority_at_same_price(engine_cls):
    engine = engine_cls()

    # Two makers sell at 10000
    engine.submit_order(order_id=1, side=qe.Side.Sell, price=10000, quantity=50)
    engine.submit_order(order_id=2, side=qe.Side.Sell, price=10000, quantity=50)

    # Taker buys 60 @ 10000 -> Should completely fill Order 1 (50) and partially fill Order 2 (10)
    report = engine.submit_order(order_id=3, side=qe.Side.Buy, price=10000, quantity=60)

    assert report.status == qe.OrderStatus.Filled
    assert len(report.trades) == 2
    assert report.trades[0].maker_order_id == 1
    assert report.trades[0].quantity == 50
    assert report.trades[1].maker_order_id == 2
    assert report.trades[1].quantity == 10

    # Order 2 has 40 remaining
    assert engine.best_ask_price() == 10000
    assert engine.best_ask_quantity() == 40


@pytest.mark.parametrize("engine_cls", [qe.ReferenceMatchingEngine, qe.OptimizedMatchingEngine])
def test_cancellation(engine_cls):
    engine = engine_cls()

    engine.submit_order(order_id=1, side=qe.Side.Buy, price=9900, quantity=100)
    assert engine.total_orders() == 1

    cancel_rep = engine.cancel_order(order_id=1)
    assert cancel_rep.status == qe.OrderStatus.Cancelled
    assert cancel_rep.order_id == 1
    assert engine.is_empty()

    # Cancelling unknown order
    unknown_cancel = engine.cancel_order(order_id=999)
    assert unknown_cancel.status == qe.OrderStatus.Rejected
    assert unknown_cancel.reject_reason == qe.RejectReason.UnknownOrder


@pytest.mark.parametrize("engine_cls", [qe.ReferenceMatchingEngine, qe.OptimizedMatchingEngine])
def test_order_modification(engine_cls):
    engine = engine_cls()

    engine.submit_order(order_id=1, side=qe.Side.Buy, price=9900, quantity=100)
    assert engine.best_bid_price() == 9900

    mod_rep = engine.modify_order(order_id=1, new_price=9950, new_quantity=150)
    assert mod_rep.status == qe.OrderStatus.Resting
    assert engine.best_bid_price() == 9950
    assert engine.best_bid_quantity() == 150


@pytest.mark.parametrize("engine_cls", [qe.ReferenceMatchingEngine, qe.OptimizedMatchingEngine])
def test_rejections(engine_cls):
    engine = engine_cls()

    # Duplicate order id
    r1 = engine.submit_order(order_id=1, side=qe.Side.Buy, price=10000, quantity=10)
    assert r1.status == qe.OrderStatus.Resting

    r_dup = engine.submit_order(order_id=1, side=qe.Side.Sell, price=10050, quantity=10)
    assert r_dup.status == qe.OrderStatus.Rejected
    assert r_dup.reject_reason == qe.RejectReason.DuplicateOrderId

    # Invalid price
    r_bad_px = engine.submit_order(order_id=2, side=qe.Side.Buy, price=0, quantity=10)
    assert r_bad_px.status == qe.OrderStatus.Rejected
    assert r_bad_px.reject_reason == qe.RejectReason.InvalidPrice

    # Invalid quantity
    r_bad_qty = engine.submit_order(order_id=3, side=qe.Side.Buy, price=10000, quantity=0)
    assert r_bad_qty.status == qe.OrderStatus.Rejected
    assert r_bad_qty.reject_reason == qe.RejectReason.InvalidQuantity


@pytest.mark.parametrize("engine_cls", [qe.ReferenceMatchingEngine, qe.OptimizedMatchingEngine])
def test_reset_and_auditor(engine_cls):
    engine = engine_cls()
    engine.submit_order(order_id=1, side=qe.Side.Buy, price=9900, quantity=100)
    engine.submit_order(order_id=2, side=qe.Side.Sell, price=10100, quantity=100)

    ok, msg = engine.audit()
    assert ok
    assert msg == ""

    engine.reset()
    assert engine.is_empty()
    assert engine.best_bid_price() is None
    assert engine.best_ask_price() is None
    assert engine.current_sequence() == 0
