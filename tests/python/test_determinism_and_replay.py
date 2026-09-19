import random
import quantengine as qe
import pytest


def test_differential_parity_reference_vs_optimized():
    ref_engine = qe.ReferenceMatchingEngine()
    opt_engine = qe.OptimizedMatchingEngine()

    random.seed(42)
    order_id = 1
    active_ids = []

    for _ in range(2000):
        action = random.random()

        if action < 0.6 or not active_ids:
            # 60%: Add Order
            oid = order_id
            order_id += 1
            side = qe.Side.Buy if random.random() < 0.5 else qe.Side.Sell
            price = random.randint(9900, 10100)
            qty = random.randint(5, 100)

            r_ref = ref_engine.submit_order(oid, side, price, qty)
            r_opt = opt_engine.submit_order(oid, side, price, qty)

            if r_ref.status == qe.OrderStatus.Resting or r_ref.status == qe.OrderStatus.PartiallyFilled:
                active_ids.append(oid)

        elif action < 0.8:
            # 20%: Cancel Order
            idx = random.randint(0, len(active_ids) - 1)
            oid = active_ids.pop(idx)

            r_ref = ref_engine.cancel_order(oid)
            r_opt = opt_engine.cancel_order(oid)

        else:
            # 20%: Modify Order
            oid = random.choice(active_ids)
            new_price = random.randint(9900, 10100)
            new_qty = random.randint(5, 100)

            r_ref = ref_engine.modify_order(oid, new_price, new_qty)
            r_opt = opt_engine.modify_order(oid, new_price, new_qty)

        # Assert every single report is identical
        assert r_ref.order_id == r_opt.order_id
        assert r_ref.status == r_opt.status
        assert r_ref.reject_reason == r_opt.reject_reason
        assert r_ref.remaining_quantity == r_opt.remaining_quantity
        assert r_ref.filled_quantity == r_opt.filled_quantity
        assert len(r_ref.trades) == len(r_opt.trades)

        for t1, t2 in zip(r_ref.trades, r_opt.trades):
            assert t1.trade_id == t2.trade_id
            assert t1.maker_order_id == t2.maker_order_id
            assert t1.taker_order_id == t2.taker_order_id
            assert t1.price == t2.price
            assert t1.quantity == t2.quantity

    # Invariants audit
    ok_ref, msg_ref = ref_engine.audit()
    ok_opt, msg_opt = opt_engine.audit()
    assert ok_ref, msg_ref
    assert ok_opt, msg_opt

    # Canonical State Hash bit-for-bit equivalence
    hash_ref = ref_engine.canonical_hash()
    hash_opt = opt_engine.canonical_hash()
    assert hash_ref == hash_opt
    assert hash_ref != 0

    # Binary serialized state bit-for-bit equivalence
    bytes_ref = ref_engine.serialize_state()
    bytes_opt = opt_engine.serialize_state()
    assert bytes_ref == bytes_opt
    assert len(bytes_ref) > 0


def test_replay_engine_determinism():
    commands = [
        qe.OrderCommand(payload=qe.CreateOrderCommand(order_id=1, side=qe.Side.Buy, price=9900, quantity=100)),
        qe.OrderCommand(payload=qe.CreateOrderCommand(order_id=2, side=qe.Side.Sell, price=10000, quantity=100)),
        qe.OrderCommand(payload=qe.CreateOrderCommand(order_id=3, side=qe.Side.Buy, price=10000, quantity=50)),
        qe.OrderCommand(payload=qe.CancelOrderCommand(order_id=1)),
    ]

    res_ref = qe.ReplayEngine.replay_reference(commands)
    res_opt = qe.ReplayEngine.replay_optimized(commands)

    assert res_ref.invariants_satisfied
    assert res_opt.invariants_satisfied
    assert res_ref.final_hash == res_opt.final_hash
    assert len(res_ref.execution_reports) == 4
    assert len(res_opt.execution_reports) == 4
    assert len(res_ref.all_trades) == 1
    assert len(res_opt.all_trades) == 1

    # Verify determinism helper
    assert qe.ReplayEngine.verify_determinism(commands)
