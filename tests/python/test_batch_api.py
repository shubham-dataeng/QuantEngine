import concurrent.futures
import time
import quantengine as qe
import pytest


def test_submit_batch_with_batch_order_objects():
    engine = qe.OptimizedMatchingEngine()

    batch = [
        qe.BatchOrder(order_id=1, side=qe.Side.Buy, price=9900, quantity=100),
        qe.BatchOrder(order_id=2, side=qe.Side.Buy, price=9950, quantity=50),
        qe.BatchOrder(order_id=3, side=qe.Side.Sell, price=10050, quantity=200),
        # Crossing order
        qe.BatchOrder(order_id=4, side=qe.Side.Buy, price=10050, quantity=50),
    ]

    reports = engine.submit_batch(batch)
    assert len(reports) == 4
    assert reports[0].status == qe.OrderStatus.Resting
    assert reports[1].status == qe.OrderStatus.Resting
    assert reports[2].status == qe.OrderStatus.Resting
    assert reports[3].status == qe.OrderStatus.Filled
    assert len(reports[3].trades) == 1
    assert reports[3].trades[0].quantity == 50

    assert engine.best_ask_price() == 10050
    assert engine.best_ask_quantity() == 150
    assert engine.best_bid_price() == 9950


def test_submit_batch_with_tuples():
    engine = qe.OptimizedMatchingEngine()

    batch = [
        (1, qe.Side.Buy, 9900, 100),
        (2, qe.Side.Buy, 9950, 50),
        (3, qe.Side.Sell, 10000, 100),
        (4, qe.Side.Buy, 10000, 100),  # Full fill against Order 3
    ]

    reports = engine.submit_batch(batch)
    assert len(reports) == 4
    assert reports[3].status == qe.OrderStatus.Filled
    assert len(reports[3].trades) == 1
    assert reports[3].trades[0].maker_order_id == 3


def test_process_commands_batch():
    engine = qe.OptimizedMatchingEngine()

    c1 = qe.OrderCommand(payload=qe.CreateOrderCommand(order_id=1, side=qe.Side.Buy, price=9900, quantity=100))
    c2 = qe.OrderCommand(payload=qe.CreateOrderCommand(order_id=2, side=qe.Side.Sell, price=10100, quantity=100))
    c3 = qe.OrderCommand(payload=qe.CancelOrderCommand(order_id=1))

    reports = engine.process_commands_batch([c1, c2, c3])
    assert len(reports) == 3
    assert reports[0].status == qe.OrderStatus.Resting
    assert reports[1].status == qe.OrderStatus.Resting
    assert reports[2].status == qe.OrderStatus.Cancelled

    assert engine.total_orders() == 1
    assert engine.best_bid_price() is None
    assert engine.best_ask_price() == 10100


def test_concurrent_batch_execution_gil_release():
    # Verify that multiple engines can process batches simultaneously on separate threads
    engines = [qe.OptimizedMatchingEngine() for _ in range(4)]
    batch_size = 5000

    def run_worker(eng, offset):
        batch = [
            (offset + i, qe.Side.Buy if i % 2 == 0 else qe.Side.Sell,
             9900 + (i % 50) if i % 2 == 0 else 10000 + (i % 50), 10)
            for i in range(batch_size)
        ]
        return eng.submit_batch(batch)

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        futures = [
            executor.submit(run_worker, engines[i], (i + 1) * 100000)
            for i in range(4)
        ]
        results = [f.result() for f in futures]

    assert len(results) == 4
    for res in results:
        assert len(res) == batch_size


def test_high_throughput_batch():
    engine = qe.OptimizedMatchingEngine(initial_capacity=131072)
    n = 50000

    # Alternating resting bids and asks
    batch = [
        (i + 1, qe.Side.Buy if i % 2 == 0 else qe.Side.Sell,
         9500 + (i % 400) if i % 2 == 0 else 10100 + (i % 400), 10)
        for i in range(n)
    ]

    t0 = time.perf_counter()
    reports = engine.submit_batch(batch)
    t1 = time.perf_counter()

    elapsed = t1 - t0
    mops = (n / elapsed) / 1e6

    assert len(reports) == n
    # Verify reasonable batch throughput across both debug and release builds
    assert mops > 0.1, f"Throughput was {mops:.2f} Mops/s, expected > 0.1 Mops/s"
    print(f"\n[Batch Benchmark] Processed {n} orders in {elapsed*1000:.2f} ms ({mops:.2f} Mops/sec)")
