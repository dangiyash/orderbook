# orderbook

A limit order book and matching engine in C++17. Price-time priority, header-only, no dependencies.

## What it does

- Limit orders with three time-in-force modes: **GTC** (rest the remainder), **IOC** (fill what crosses, drop the rest), **FOK** (fill everything or reject).
- **Cancel** and **modify** (cancel/replace, which loses time priority, as on real exchanges).
- Trades execute at the **resting order's price**. Prices are integer ticks, never floats.
- Rejects zero-quantity orders, duplicate order ids, and unfillable FOK orders.

## Design

| Piece | Choice | Why |
|---|---|---|
| Each side | `std::map<Price, std::list<Order>>` | Sorted price levels, FIFO queue per level gives time priority |
| Order lookup | `unordered_map<OrderId, {side, price, list iterator}>` | Cancel does not scan the book |
| Bids ordering | `std::greater<Price>` | Best bid is `begin()`, so best bid/ask are O(1) |

Complexity: add without a match is O(log L), cancel is O(log L), best bid/ask is O(1), where L is the number of price levels.

## Build and run

```
make test       # unit tests (48 checks)
make sanitize   # same tests under AddressSanitizer + UBSan
make bench      # throughput and latency
```

## Results

Measured on a 2-core cloud sandbox, g++ 13.3, `-O2`, 2,000,000 operations (70% limit adds around a random-walking mid price, 30% cancels of random earlier orders; about 1.26M trades):

```
throughput:  3.73 M ops/s (includes timing overhead)
latency ns:  p50=152  p90=451  p99=895  p99.9=3183  max=10686478
```

Read these numbers carefully:
- Each latency sample includes roughly 20-40 ns of `steady_clock` overhead.
- The 10 ms max is an OS-level outlier (page faults, scheduling), not the engine. The p99.9 is the more honest tail figure here.
- Some cancels target orders that were already filled, so they are cheap no-ops. The workload is a rough synthetic mix, not a replay of real market data.
- Run it on your own machine and put your numbers here.

## Tests

`tests/test_orderbook.cpp` covers price priority, time priority, maker-price execution, partial fills, IOC, FOK (both reject and success across two levels), cancel, cancelling filled orders, modify losing priority, modify crossing the spread, input rejection, and a 20k-operation randomized check that the book is never crossed.

## Known limits (deliberate, and where to go next)

1. **Cache behavior.** `std::map` and `std::list` allocate a node per level and per order. Replace them with a contiguous array of levels around the mid price plus an object pool for orders, then benchmark the difference.
2. **Single threaded.** Add a lock-free SPSC ring buffer between a gateway thread and the matching thread.
3. **No market orders, no stop orders, no self-trade prevention.**
4. **Synthetic benchmark only.** Replay a real feed format (for example Nasdaq ITCH samples) through it.
5. **Stronger invariants.** Track cancelled and IOC-dropped quantity so the test can assert exact conservation.
6. **Profile it.** Use `perf stat` / `perf record` and add the cache-miss numbers to this README.

## Development notes

The first version of this code was written with help from Claude (Anthropic) as a starting point. Everything after the first commit is my own work. <!-- Edit this line to describe what you actually did. -->
