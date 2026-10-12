// Limit order book with price-time priority matching.
//
// Data structures
//   - Each side is a std::map<Price, std::list<Order>>: price levels sorted
//     best-first, and a FIFO queue per level (time priority).
//   - index_ maps OrderId -> (side, price, list iterator) so cancel is
//     O(log L) for the level lookup plus O(1) for the list erase. std::list and
//     std::map iterators stay valid when other elements change, which is what
//     makes this safe.
//
// Complexity (L = number of price levels, M = orders matched)
//   add (no match)  O(log L)
//   add (matching)  O(M + levels crossed * log L)
//   cancel          O(log L)
//   best bid/ask    O(1)
//
// Known limits (good extension work, see README): single threaded, node-based
// containers (cache unfriendly), no market orders, no self-trade prevention.
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ob {

using OrderId = std::uint64_t;
using Price = std::int64_t;  // integer ticks, never floating point
using Qty = std::uint32_t;

enum class Side : std::uint8_t { Buy, Sell };

// GTC: rest the remainder on the book.
// IOC: execute what crosses now, cancel the remainder.
// FOK: execute the whole quantity now or do nothing.
enum class TimeInForce : std::uint8_t { GTC, IOC, FOK };

struct Order {
  OrderId id;
  Side side;
  Price price;
  Qty qty;  // remaining quantity
};

struct Trade {
  OrderId taker_id;
  OrderId maker_id;
  Price price;  // always the resting (maker) order's price
  Qty qty;
};

class OrderBook {
 public:
  // Returns false if the order is rejected: zero quantity, duplicate id, or an
  // FOK order that cannot be filled in full. Trades are appended to `trades`.
  bool add(OrderId id, Side side, Price price, Qty qty, TimeInForce tif,
           std::vector<Trade>& trades) {
    if (qty == 0 || index_.count(id) != 0) return false;

    if (side == Side::Buy) {
      if (tif == TimeInForce::FOK && available(asks_, Side::Buy, price) < qty) return false;
      Qty left = match(asks_, id, Side::Buy, price, qty, trades);
      if (left > 0 && tif == TimeInForce::GTC) rest(bids_, id, Side::Buy, price, left);
    } else {
      if (tif == TimeInForce::FOK && available(bids_, Side::Sell, price) < qty) return false;
      Qty left = match(bids_, id, Side::Sell, price, qty, trades);
      if (left > 0 && tif == TimeInForce::GTC) rest(asks_, id, Side::Sell, price, left);
    }
    return true;
  }

  // Removes a resting order. Returns false if the id is unknown (never rested,
  // already filled, or already cancelled).
  bool cancel(OrderId id) {
    auto it = index_.find(id);
    if (it == index_.end()) return false;
    const Loc loc = it->second;
    if (loc.side == Side::Buy) erase_from(bids_, loc);
    else erase_from(asks_, loc);
    index_.erase(it);
    return true;
  }

  // Cancel/replace: the order loses time priority and may match immediately.
  // Returns false if the id is unknown or the new quantity is zero.
  bool modify(OrderId id, Price new_price, Qty new_qty, std::vector<Trade>& trades) {
    auto it = index_.find(id);
    if (it == index_.end() || new_qty == 0) return false;
    const Side side = it->second.side;
    cancel(id);
    return add(id, side, new_price, new_qty, TimeInForce::GTC, trades);
  }

  std::optional<Price> best_bid() const {
    return bids_.empty() ? std::nullopt : std::optional<Price>(bids_.begin()->first);
  }
  std::optional<Price> best_ask() const {
    return asks_.empty() ? std::nullopt : std::optional<Price>(asks_.begin()->first);
  }

  // Total resting quantity at an exact price on one side.
  Qty depth_at(Side side, Price price) const {
    return side == Side::Buy ? level_qty(bids_, price) : level_qty(asks_, price);
  }

  std::size_t open_orders() const { return index_.size(); }

 private:
  using Level = std::list<Order>;
  using Bids = std::map<Price, Level, std::greater<Price>>;  // best = highest
  using Asks = std::map<Price, Level>;                       // best = lowest

  struct Loc {
    Side side;
    Price price;
    Level::iterator it;
  };

  static bool crosses(Side taker, Price limit, Price level) {
    return taker == Side::Buy ? level <= limit : level >= limit;
  }

  // Quantity the taker could execute right now (used for FOK).
  template <class Book>
  static Qty available(const Book& book, Side taker, Price limit) {
    std::uint64_t total = 0;
    for (const auto& [price, level] : book) {
      if (!crosses(taker, limit, price)) break;
      for (const Order& o : level) total += o.qty;
    }
    return total > UINT32_MAX ? UINT32_MAX : static_cast<Qty>(total);
  }

  // Match the taker against the opposite book, best price first, FIFO within
  // a price. Returns the unfilled remainder.
  template <class Book>
  Qty match(Book& book, OrderId taker, Side side, Price limit, Qty qty,
            std::vector<Trade>& trades) {
    while (qty > 0 && !book.empty()) {
      auto lvl = book.begin();
      if (!crosses(side, limit, lvl->first)) break;
      Level& q = lvl->second;
      while (qty > 0 && !q.empty()) {
        Order& maker = q.front();
        const Qty fill = std::min(qty, maker.qty);
        trades.push_back({taker, maker.id, lvl->first, fill});
        maker.qty -= fill;
        qty -= fill;
        if (maker.qty == 0) {
          index_.erase(maker.id);
          q.pop_front();
        }
      }
      if (q.empty()) book.erase(lvl);
    }
    return qty;
  }

  template <class Book>
  void rest(Book& book, OrderId id, Side side, Price price, Qty qty) {
    Level& q = book[price];
    q.push_back({id, side, price, qty});
    index_[id] = Loc{side, price, std::prev(q.end())};
  }

  template <class Book>
  static void erase_from(Book& book, const Loc& loc) {
    auto lvl = book.find(loc.price);
    lvl->second.erase(loc.it);
    if (lvl->second.empty()) book.erase(lvl);
  }

  template <class Book>
  static Qty level_qty(const Book& book, Price price) {
    auto lvl = book.find(price);
    if (lvl == book.end()) return 0;
    std::uint64_t total = 0;
    for (const Order& o : lvl->second) total += o.qty;
    return static_cast<Qty>(total);
  }

  Bids bids_;
  Asks asks_;
  std::unordered_map<OrderId, Loc> index_;
};

}  // namespace ob
