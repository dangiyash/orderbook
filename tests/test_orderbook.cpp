// Minimal dependency-free tests. Build and run with `make test`.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "orderbook.hpp"

using namespace ob;

static int g_checks = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    ++g_checks;                                                              \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

static void resting_orders_do_not_trade_when_not_crossing() {
  OrderBook b;
  std::vector<Trade> t;
  CHECK(b.add(1, Side::Buy, 100, 10, TimeInForce::GTC, t));
  CHECK(b.add(2, Side::Sell, 101, 10, TimeInForce::GTC, t));
  CHECK(t.empty());
  CHECK(*b.best_bid() == 100);
  CHECK(*b.best_ask() == 101);
  CHECK(b.open_orders() == 2);
}

static void price_priority_then_time_priority() {
  OrderBook b;
  std::vector<Trade> t;
  b.add(1, Side::Sell, 101, 5, TimeInForce::GTC, t);  // worse price
  b.add(2, Side::Sell, 100, 5, TimeInForce::GTC, t);  // best price, first in time
  b.add(3, Side::Sell, 100, 5, TimeInForce::GTC, t);  // best price, second in time
  b.add(4, Side::Buy, 101, 12, TimeInForce::GTC, t);
  CHECK(t.size() == 3);
  CHECK(t[0].maker_id == 2 && t[0].price == 100 && t[0].qty == 5);
  CHECK(t[1].maker_id == 3 && t[1].price == 100 && t[1].qty == 5);
  CHECK(t[2].maker_id == 1 && t[2].price == 101 && t[2].qty == 2);
  CHECK(b.depth_at(Side::Sell, 101) == 3);
  CHECK(b.open_orders() == 1);
}

static void trade_price_is_the_makers_price() {
  OrderBook b;
  std::vector<Trade> t;
  b.add(1, Side::Sell, 100, 10, TimeInForce::GTC, t);
  b.add(2, Side::Buy, 105, 10, TimeInForce::GTC, t);  // willing to pay 105
  CHECK(t.size() == 1 && t[0].price == 100);
  CHECK(b.open_orders() == 0);
}

static void partial_fill_rests_remainder_for_gtc() {
  OrderBook b;
  std::vector<Trade> t;
  b.add(1, Side::Sell, 100, 4, TimeInForce::GTC, t);
  b.add(2, Side::Buy, 100, 10, TimeInForce::GTC, t);
  CHECK(t.size() == 1 && t[0].qty == 4);
  CHECK(*b.best_bid() == 100);
  CHECK(b.depth_at(Side::Buy, 100) == 6);
  CHECK(!b.best_ask().has_value());
}

static void ioc_never_rests() {
  OrderBook b;
  std::vector<Trade> t;
  b.add(1, Side::Sell, 100, 4, TimeInForce::GTC, t);
  CHECK(b.add(2, Side::Buy, 100, 10, TimeInForce::IOC, t));
  CHECK(t.size() == 1 && t[0].qty == 4);
  CHECK(!b.best_bid().has_value());
  CHECK(b.open_orders() == 0);
}

static void fok_all_or_nothing() {
  OrderBook b;
  std::vector<Trade> t;
  b.add(1, Side::Sell, 100, 4, TimeInForce::GTC, t);
  b.add(2, Side::Sell, 101, 4, TimeInForce::GTC, t);
  // Only 4 available at <= 100, so FOK for 6 at 100 must be rejected untouched.
  CHECK(!b.add(3, Side::Buy, 100, 6, TimeInForce::FOK, t));
  CHECK(t.empty());
  CHECK(b.depth_at(Side::Sell, 100) == 4);
  // 8 available at <= 101, so FOK for 8 fills in full across two levels.
  CHECK(b.add(4, Side::Buy, 101, 8, TimeInForce::FOK, t));
  CHECK(t.size() == 2);
  CHECK(b.open_orders() == 0);
}

static void cancel_removes_order_and_empty_levels() {
  OrderBook b;
  std::vector<Trade> t;
  b.add(1, Side::Buy, 100, 5, TimeInForce::GTC, t);
  b.add(2, Side::Buy, 99, 5, TimeInForce::GTC, t);
  CHECK(b.cancel(1));
  CHECK(*b.best_bid() == 99);
  CHECK(!b.cancel(1));    // already gone
  CHECK(!b.cancel(999));  // never existed
  CHECK(b.open_orders() == 1);
}

static void filled_orders_cannot_be_cancelled() {
  OrderBook b;
  std::vector<Trade> t;
  b.add(1, Side::Sell, 100, 5, TimeInForce::GTC, t);
  b.add(2, Side::Buy, 100, 5, TimeInForce::GTC, t);
  CHECK(!b.cancel(1));
  CHECK(!b.cancel(2));
}

static void modify_loses_time_priority() {
  OrderBook b;
  std::vector<Trade> t;
  b.add(1, Side::Sell, 100, 5, TimeInForce::GTC, t);
  b.add(2, Side::Sell, 100, 5, TimeInForce::GTC, t);
  CHECK(b.modify(1, 100, 5, t));  // same price, but goes to the back of the queue
  CHECK(t.empty());
  b.add(3, Side::Buy, 100, 5, TimeInForce::GTC, t);
  CHECK(t.size() == 1 && t[0].maker_id == 2);
}

static void modify_can_cross_and_trade() {
  OrderBook b;
  std::vector<Trade> t;
  b.add(1, Side::Sell, 101, 5, TimeInForce::GTC, t);
  b.add(2, Side::Buy, 99, 5, TimeInForce::GTC, t);
  CHECK(b.modify(2, 101, 5, t));
  CHECK(t.size() == 1 && t[0].taker_id == 2 && t[0].maker_id == 1);
  CHECK(b.open_orders() == 0);
}

static void rejects_bad_input() {
  OrderBook b;
  std::vector<Trade> t;
  CHECK(!b.add(1, Side::Buy, 100, 0, TimeInForce::GTC, t));   // zero qty
  CHECK(b.add(2, Side::Buy, 100, 5, TimeInForce::GTC, t));
  CHECK(!b.add(2, Side::Buy, 101, 5, TimeInForce::GTC, t));   // duplicate id
  CHECK(!b.modify(77, 100, 5, t));                            // unknown id
  CHECK(!b.modify(2, 100, 0, t));                             // zero qty
}

// Randomized invariants over 20k operations: the book is never crossed after
// any operation sequence, and traded quantity can never exceed what was
// submitted on either side. (Not a full conservation proof; a stricter
// version that also tracks cancelled quantity is a good extension.)
static void quantity_is_conserved_over_random_flow() {
  OrderBook b;
  std::vector<Trade> t;
  std::uint64_t x = 88172645463325252ULL;
  auto rnd = [&x]() { x ^= x << 13; x ^= x >> 7; x ^= x << 17; return x; };

  std::uint64_t submitted_buy = 0, submitted_sell = 0, traded = 0;
  for (OrderId id = 1; id <= 20000; ++id) {
    Side s = (rnd() & 1) ? Side::Buy : Side::Sell;
    Price p = 1000 + static_cast<Price>(rnd() % 21) - 10;
    Qty q = 1 + static_cast<Qty>(rnd() % 50);
    t.clear();
    b.add(id, s, p, q, TimeInForce::GTC, t);
    (s == Side::Buy ? submitted_buy : submitted_sell) += q;
    for (const Trade& tr : t) traded += tr.qty;
    if (id % 7 == 0) b.cancel(id - 3);  // occasional cancels; result is ignored
  }
  // Book must never be crossed.
  if (b.best_bid() && b.best_ask()) CHECK(*b.best_bid() < *b.best_ask());
  CHECK(traded <= submitted_buy && traded <= submitted_sell);
}

int main() {
  resting_orders_do_not_trade_when_not_crossing();
  price_priority_then_time_priority();
  trade_price_is_the_makers_price();
  partial_fill_rests_remainder_for_gtc();
  ioc_never_rests();
  fok_all_or_nothing();
  cancel_removes_order_and_empty_levels();
  filled_orders_cannot_be_cancelled();
  modify_loses_time_priority();
  modify_can_cross_and_trade();
  rejects_bad_input();
  quantity_is_conserved_over_random_flow();
  std::printf("all tests passed (%d checks)\n", g_checks);
  return 0;
}
