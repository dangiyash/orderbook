// Throughput and per-operation latency benchmark.
// Workload (fixed seed, so runs are comparable): 70% limit adds around a
// drifting mid price (about half of them cross the spread), 30% cancels of a
// random earlier order.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "orderbook.hpp"

using namespace ob;
using Clock = std::chrono::steady_clock;

int main(int argc, char** argv) {
  const std::size_t n_ops = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 2'000'000;

  std::uint64_t x = 88172645463325252ULL;
  auto rnd = [&x]() { x ^= x << 13; x ^= x >> 7; x ^= x << 17; return x; };

  OrderBook book;
  std::vector<Trade> trades;
  trades.reserve(64);
  std::vector<OrderId> issued;
  issued.reserve(n_ops);
  std::vector<std::uint32_t> lat;
  lat.reserve(n_ops);

  Price mid = 100000;
  OrderId next_id = 1;
  std::uint64_t n_trades = 0;

  const auto t_start = Clock::now();
  for (std::size_t i = 0; i < n_ops; ++i) {
    if ((i & 1023) == 0) mid += static_cast<Price>(rnd() % 5) - 2;  // random walk

    const bool do_cancel = !issued.empty() && (rnd() % 10) < 3;
    const auto t0 = Clock::now();
    if (do_cancel) {
      book.cancel(issued[rnd() % issued.size()]);
    } else {
      const Side s = (rnd() & 1) ? Side::Buy : Side::Sell;
      const Price offset = static_cast<Price>(rnd() % 20);  // 0..19 ticks
      const Price p = s == Side::Buy ? mid - 9 + offset : mid + 9 - offset;
      const Qty q = 1 + static_cast<Qty>(rnd() % 100);
      trades.clear();
      book.add(next_id, s, p, q, TimeInForce::GTC, trades);
      issued.push_back(next_id++);
      n_trades += trades.size();
    }
    const auto t1 = Clock::now();
    lat.push_back(static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
  }
  const double secs = std::chrono::duration<double>(Clock::now() - t_start).count();

  std::sort(lat.begin(), lat.end());
  auto pct = [&](double p) { return lat[static_cast<std::size_t>(p * (lat.size() - 1))]; };

  std::printf("ops:            %zu\n", n_ops);
  std::printf("trades:         %llu\n", static_cast<unsigned long long>(n_trades));
  std::printf("open orders:    %zu\n", book.open_orders());
  std::printf("throughput:     %.2f M ops/s (includes timing overhead)\n", n_ops / secs / 1e6);
  std::printf("latency ns:     p50=%u  p90=%u  p99=%u  p99.9=%u  max=%u\n",
              pct(0.50), pct(0.90), pct(0.99), pct(0.999), lat.back());
  std::printf("note: each sample includes ~20-40 ns of steady_clock overhead\n");
  return 0;
}
