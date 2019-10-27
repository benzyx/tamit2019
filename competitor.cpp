// TODO: move ClientState and its OrderBook code into this file

#include "kirin.hpp"
#include <cassert>
#include <iostream>
#include <iomanip>
#include <fstream>

#include <algorithm>
#include <chrono>
#include <set>
#include <unordered_map>
#include <unordered_set>

#define DEBUG 0
#define INFO 1

#define TIME_INFO 1


int64_t time_ns() {
  using namespace std::chrono;
  return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

// PRICING TYPE 1
price_t cube_root_price(price_t best_bid, price_t best_offer, quantity_t bid_quote_size, quantity_t offer_quote_size) {
  const quantity_t bid_weight = std::cbrt(offer_quote_size);
  const quantity_t offer_weight = std::cbrt(bid_quote_size);
  return (bid_weight * best_bid + offer_weight * best_offer) / (bid_weight + offer_weight);
}
// PRICING TYPE 2
price_t square_root_price(price_t best_bid, price_t best_offer, quantity_t bid_quote_size, quantity_t offer_quote_size) {
  const quantity_t bid_weight = std::sqrt(offer_quote_size);
  const quantity_t offer_weight = std::sqrt(bid_quote_size);
  return (bid_weight * best_bid + offer_weight * best_offer) / (bid_weight + offer_weight);
}

// PRICING TYPE 3
price_t weighted_price(price_t best_bid, price_t best_offer, quantity_t bid_quote_size, quantity_t offer_quote_size) {
  const quantity_t bid_weight = offer_quote_size;
  const quantity_t offer_weight = bid_quote_size;
  return (bid_weight * best_bid + offer_weight * best_offer) / (bid_weight + offer_weight);
}

// PRICING TYPE 4
price_t square_price(price_t best_bid, price_t best_offer, quantity_t bid_quote_size, quantity_t offer_quote_size) {
  const quantity_t bid_weight = offer_quote_size * offer_quote_size;
  const quantity_t offer_weight = bid_quote_size * bid_quote_size;
  return (bid_weight * best_bid + offer_weight * best_offer) / (bid_weight + offer_weight);
}

// Limit Orders
//
struct LimitOrder {
  price_t price;
  mutable quantity_t quantity; // mutable so set doesn't complain
  order_id_t order_id;
  long long time;
  trader_id_t trader_id;
  bool buy;

  bool operator <(const LimitOrder& other) const {
    // < means more aggressive
    if (buy) {
      return price > other.price || (price == other.price && time < other.time);
    } else {
      return price < other.price || (price == other.price && time < other.time);
    }
  }

  bool trades_with(const LimitOrder& other) const {
    return ((buy && !other.buy && price >= other.price) ||
            (!buy && other.buy && price <= other.price));
  }
};

// MyBook
//
struct MyBook {
public:

  MyBook() {}

  price_t get_bbo(bool buy) const {
    const std::set<LimitOrder>& side = sides[buy];

    if (side.empty()) {
      return 0.0;
    }
    return side.begin()->price;
  }

  price_t get_mid_price(price_t default_to) const {
    price_t best_bid = get_bbo(true);
    price_t best_offer = get_bbo(false);

    if (best_bid == 0.0 || best_offer == 0.0) {
      return default_to;
    }
    return 0.5 * (best_bid + best_offer);
  }


  void insert(Common::Order order_to_insert) {

    LimitOrder order_left = {
      .price = order_to_insert.price,
      .quantity = order_to_insert.quantity,
      .order_id = order_to_insert.order_id,
      .time = std::chrono::steady_clock::now().time_since_epoch().count(),
      .trader_id = order_to_insert.trader_id,
      .buy = order_to_insert.buy
    };

    auto& side = sides[(size_t)order_left.buy];

    auto it_new = side.insert(order_left);
    assert(it_new.second);
    order_map[order_left.order_id] = it_new.first;

  }

  void cancel(trader_id_t trader_id, order_id_t order_id) {

    auto it = order_map[order_id];

    order_map.erase(order_id);

    auto& side = sides[(size_t)it->buy];
    side.erase(it);
  }

  quantity_t decrease_qty(order_id_t order_id, quantity_t decrease_by) {

    if (!order_map.count(order_id)) {
      return -1;
    }

    std::set<LimitOrder>::iterator it = order_map[order_id];

    if (decrease_by >= it->quantity) {
      order_map.erase(order_id);
      std::set<LimitOrder>& side = sides[(size_t)it->buy];
      side.erase(it);
      return 0;

    } else {

      it->quantity -= decrease_by;
      return it->quantity;
    }

  }

  void print_book(std::string fp, const std::unordered_map<order_id_t, Common::Order>& mine={}) {
    if (fp == "") {
      return;
    }

    std::ofstream fout(fp, std::ios::app);

    fout << time_ns() << std::endl;
    fout << "offers\n";
    for (auto rit = sides[0].rbegin(); rit != sides[0].rend(); rit++) {
      auto x = *rit;
      fout << x.price << ' ' << x.quantity;
      if (mine.count(x.order_id)) {
        fout << " (mine)";
      }
      fout << '\n';
    }

    fout << "\nbids\n";

    for (auto& x : sides[1]) {
      fout << x.price << ' ' << x.quantity;
      if (mine.count(x.order_id)) {
        fout << " (mine)";
      }
      fout << '\n';
    }

    fout << "EOF" << std::endl;


    fout.close();
  }


  /// Really fucking slow. Need to improve this.
  quantity_t quote_size(bool buy) const {
    price_t p = get_bbo(buy);
    if (p == 0.0) {
      return 0;
    }
    quantity_t ans = 0;
    for (auto& x : sides[buy]) {
      if (x.price != p) {
        break;
      }
      ans += x.quantity;
    }
    return ans;
  }

  price_t spread() const {

    price_t best_bid = get_bbo(true);
    price_t best_offer = get_bbo(false);

    if (best_bid == 0.0 || best_offer == 0.0) {
      return 0.0;
    }

    return best_offer - best_bid;
  }

private:
  std::set<LimitOrder> sides[2];
  std::unordered_map<order_id_t, std::set<LimitOrder>::iterator> order_map;
};

// My State
//
//
//
//
//
//
//
//
struct MyState {
  MyState(trader_id_t trader_id) :
    trader_id(trader_id), books(), submitted(), open_orders(),
    cash(), positions(), volume_traded(), last_trade_price(100.0),
    log_path("") {}

  MyState() : MyState(0) {}

  void on_trade_update(const Common::TradeUpdate& update) {
    last_trade_price = update.price;
    books[update.ticker].decrease_qty(update.resting_order_id, update.quantity);

    if (submitted.count(update.resting_order_id)) {

      if (!submitted.count(update.aggressing_order_id)) {
        volume_traded += update.quantity;
        // not a self-trade
        update_position(update.ticker, update.price,
                        update.buy ? -update.quantity : update.quantity); // opposite, since resting
      }

      open_orders[update.resting_order_id].quantity -= update.quantity;
      if (open_orders[update.resting_order_id].quantity <= 0) {
        open_orders.erase(update.resting_order_id);
      }

    } else if (submitted.count(update.aggressing_order_id)) {
      volume_traded += update.quantity;

      update_position(update.ticker, update.price,
                      update.buy ? update.quantity : -update.quantity);
    }
  }

  void update_position(ticker_t ticker, price_t price, quantity_t delta_quantity) {
    cash -= price * delta_quantity;
    positions[ticker] += delta_quantity;
  }

  void on_order_update(const Common::OrderUpdate& update) {

    const Common::Order order{
      .ticker = update.ticker,
      .price = update.price,
      .quantity = update.quantity,
      .buy = update.buy,
      .ioc = false,
      .order_id = update.order_id,
      .trader_id = trader_id
    };
    books[update.ticker].insert(order);

    if (submitted.count(update.order_id)) {
      open_orders[update.order_id] = order;
    }
  }

  void on_cancel_update(const Common::CancelUpdate& update) {
    books[update.ticker].cancel(trader_id, update.order_id);

    if (open_orders.count(update.order_id)) {
      open_orders.erase(update.order_id);

    }

    submitted.erase(update.order_id);
  }

  void on_place_order(const Common::Order& order) {
    submitted.insert(order.order_id);
  }


  std::unordered_map<price_t, std::vector<Common::Order>> levels() const {
    std::unordered_map<price_t, std::vector<Common::Order>> levels;
    for (const auto& p : open_orders) {
      const Common::Order& order = p.second;
      levels[order.price].push_back(order);
    }
    return levels;
  }

  price_t get_pnl() const {
    price_t pnl = cash;

    for (int i = 0; i < MAX_NUM_TICKERS; i++) {
      pnl += positions[i] * books[i].get_mid_price(last_trade_price);
    }

    return pnl;
  }

  price_t get_bbo(ticker_t ticker, bool buy) const {
    return books[ticker].get_bbo(buy);
  }

  price_t get_mid_price(ticker_t ticker, bool buy) const {
    return books[ticker].get_mid_price(buy);
  }

  price_t get_quote_size(ticker_t ticker, bool buy) const {
    return books[ticker].quote_size(buy);
  }

  price_t get_spread(ticker_t ticker) const {
    return books[ticker].spread();
  }

  void log_book() {
    books[0].print_book(log_path, open_orders);
  }

  trader_id_t trader_id;
  MyBook books[MAX_NUM_TICKERS];
  std::unordered_set<order_id_t> submitted;
  std::unordered_map<order_id_t, Common::Order> open_orders;
  price_t cash;
  quantity_t positions[MAX_NUM_TICKERS];
  quantity_t volume_traded;
  price_t last_trade_price;
  std::string log_path;

};

// Momentum Bot
//
// - Part 1. Liquidity Taker: takes all orders that cross the weighted spread with IOC.
// - Part 2. Market Maker: frontruns the leading spread if the size is greater than 0.1
//
//
class MomentumBot : public Bot::AbstractBot {

public:

  MyState state;

  using Bot::AbstractBot::AbstractBot;

  int64_t last = 0, start_time;
  uint64_t last_order_id = 0;

  bool trade_with_me_in_this_packet = false;

  // (maybe) EDIT THIS METHOD
  void init(Bot::Communicator& com) {
    state.trader_id = trader_id;
    state.log_path = "book.log";
    start_time = time_ns();
  }


  // EDIT THIS METHOD
  void on_trade_update(Common::TradeUpdate& update, Bot::Communicator& com){

    if (DEBUG) {
      std::cout << "Called on TRADE update: " << update.getMsg() << std::endl;
    }

    state.on_trade_update(update);

    if (state.submitted.count(update.resting_order_id) ||
        state.submitted.count(update.aggressing_order_id)) {
      trade_with_me_in_this_packet = true;
      if (INFO) {
        std::cout << "WE WERE TRADED WITH!" << std::endl;
        std::cout << update.getMsg() << std::endl;
      }
    }
  }


  quantity_t last_large_order_qty = 0;
  price_t last_large_order_price = 0;
  bool last_large_order_side = false;

  // EDIT THIS METHOD
  void on_order_update(Common::OrderUpdate & update, Bot::Communicator& com){

    if (DEBUG) {
      std::cout << "Called on ORDER update: " << update.getMsg() << std::endl;
    }


    ///
    /// Get the previous prices.
    ///

    // a way to put in a bid of quantity 1 at the current best bid
    price_t best_bid = state.get_bbo(0, true);
    price_t best_offer = state.get_bbo(0, false);
    price_t spread_size = state.get_spread(0);
    quantity_t bid_quote_size = state.get_quote_size(0, true);
    quantity_t offer_quote_size = state.get_quote_size(0, false);
    price_t fair_price = weighted_price(best_bid, best_offer, bid_quote_size, offer_quote_size);

    state.on_order_update(update);

    // This order is our order -- do not respond.
    if (state.submitted.count(update.order_id)) {
      return;
    }


    constexpr quantity_t position_limit = 1000;

    // Momentum on top of book: Taking side on large orders
    // Other competitors are literally incapable of placing orders larger than 2000 -- due to their position limits.
    if (update.quantity > 2000) {
      // BUY side large order
      if (update.buy && update.price > best_bid && state.positions[0] < position_limit) {
        // Make a large IOC order to take best offers.
        order_id_t order_id = place_order(com, Common::Order{
          .ticker = 0,
          .price = best_offer + 0.02,
          .quantity = position_limit - state.positions[0],
          .buy = true,
          .ioc = true,
          .order_id = 0,
          .trader_id = trader_id
        });

        if (INFO) {
          std::cout << "BIG BUY DETECTED!" << std::endl;
          std::cout << " -- best_bid         : " << best_bid << std::endl;
          std::cout << " -- bid_quote_size   : " << bid_quote_size << std::endl;
          std::cout << " -- best_offer       : " << best_offer << std::endl;
          std::cout << " -- offer_quote_size : " << offer_quote_size << std::endl;
          std::cout << " -- spread_size      : " << spread_size << std::endl;
          std::cout << " -- fair_price       : " << fair_price << std::endl;
          std::cout << " ----------------------------------------" << std::endl;
          std::cout << " -- THEIR PRICE      : " << update.price << std::endl;
          std::cout << " -- THEIR QTY        : " << update.quantity << std::endl;
          std::cout << " -- OUR QTY          : " << position_limit - state.positions[0] << std::endl;
          std::cout << " -- ORDER ID         : " << order_id << std::endl;
          std::cout << " -- THEIR ORDER      : " << update.order_id << std::endl;
          std::cout << " ----------------------------------------" << std::endl;
        }
      }

      if (!update.buy && update.price < best_offer && state.positions[0] > -position_limit) {
        // Make a large IOC order to take best bids.
        order_id_t order_id = place_order(com, Common::Order{
          .ticker = 0,
          .price = best_bid - 0.02,
          .quantity = state.positions[0] + position_limit,
          .buy = false,
          .ioc = true,
          .order_id = 0,
          .trader_id = trader_id
        });

        if (INFO) {
          std::cout << "BIG SELL DETECTED!" << std::endl;
          std::cout << " -- best_bid         : " << best_bid << std::endl;
          std::cout << " -- bid_quote_size   : " << bid_quote_size << std::endl;
          std::cout << " -- best_offer       : " << best_offer << std::endl;
          std::cout << " -- offer_quote_size : " << offer_quote_size << std::endl;
          std::cout << " -- spread_size      : " << spread_size << std::endl;
          std::cout << " -- fair_price       : " << fair_price << std::endl;
          std::cout << " ----------------------------------------" << std::endl;
          std::cout << " -- THEIR PRICE      : " << update.price << std::endl;
          std::cout << " -- THEIR QTY        : " << update.quantity << std::endl;
          std::cout << " -- OUR QTY          : " << position_limit + state.positions[0] << std::endl;
          std::cout << " -- ORDER ID         : " << order_id << std::endl;
          std::cout << " -- THEIR ORDER      : " << update.order_id << std::endl;
          std::cout << " ----------------------------------------" << std::endl;
        }
      }
    }

    //// STRATEGY 1:
    //// MARKET TAKING:
    // got the true weighted price.
    // price_t weighted_price = (best_bid * bid_quote_size + best_offer * offer_quote_size) / (bid_quote_size + offer_quote_size);
    
    /*
    if (spread_size < 2 && spread_size > 0.2 && update.quantity <= 100){

      quantity_t order_size = update.quantity > 100 ? 100 : update.quantity;

      // if the order crosses the weighted spread, send an IOC to hit it.
      if (update.buy && update.price > fair_price + 0.02) {
        order_id_t order_id = place_order(com, Common::Order{
          .ticker = 0,
          .price = update.price,
          .quantity = order_size,
          .buy = false,
          .ioc = true,
          .order_id = 0,
          .trader_id = trader_id
        });

        if (INFO) {
          std::cout << "SHIT BUY DETECTED!" << std::endl;
          std::cout << " -- best_bid         : " << best_bid << std::endl;
          std::cout << " -- bid_quote_size   : " << bid_quote_size << std::endl;
          std::cout << " -- best_offer       : " << best_offer << std::endl;
          std::cout << " -- offer_quote_size : " << offer_quote_size << std::endl;
          std::cout << " -- spread_size      : " << spread_size << std::endl;
          std::cout << " -- fair_price       : " << fair_price << std::endl;
          std::cout << " ----------------------------------------" << std::endl;
          std::cout << " -- THEIR PRICE      : " << update.price << std::endl;
          std::cout << " -- THEIR QTY        : " << update.quantity << std::endl;
          std::cout << " -- OUR QTY          : " << order_size << std::endl;
          std::cout << " -- ORDER ID         : " << order_id << std::endl;
          std::cout << " -- THEIR ORDER      : " << update.order_id << std::endl;
          std::cout << " ----------------------------------------" << std::endl;
        }
      }
      else if (!update.buy && update.price < fair_price - 0.02) {
        order_id_t order_id = place_order(com, Common::Order{
          .ticker = 0,
          .price = update.price,
          .quantity = order_size,
          .buy = true,
          .ioc = true,
          .order_id = 0, // this order ID will be chosen randomly by com
          .trader_id = trader_id
        });

        if (INFO) {
          std::cout << "SHIT SELL DETECTED!" << std::endl;
          std::cout << " -- best_bid         : " << best_bid << std::endl;
          std::cout << " -- bid_quote_size   : " << bid_quote_size << std::endl;
          std::cout << " -- best_offer       : " << best_offer << std::endl;
          std::cout << " -- offer_quote_size : " << offer_quote_size << std::endl;
          std::cout << " -- spread_size      : " << spread_size << std::endl;
          std::cout << " -- fair_price       : " << fair_price << std::endl;
          std::cout << " ----------------------------------------" << std::endl;
          std::cout << " -- THEIR PRICE      : " << update.price << std::endl;
          std::cout << " -- THEIR QTY        : " << update.quantity << std::endl;
          std::cout << " -- OUR QTY          : " << order_size << std::endl;
          std::cout << " -- ORDER ID         : " << order_id << std::endl;
          std::cout << " -- THEIR ORDER      : " << update.order_id << std::endl;
          std::cout << " ----------------------------------------" << std::endl;
        }
      }
    }
    */

    // Timer dump
    int64_t now = time_ns();
    if (TIME_INFO && (now - last > 10e9)) { // 10ms
      state.log_book();

      std::cout << now << ": " << std::endl;
      std::cout << " -- best_bid : " << best_bid << std::endl;
      std::cout << " -- bid_quote_size : " << bid_quote_size << std::endl;
      std::cout << " -- best_offer : " << best_offer << std::endl;
      std::cout << " -- offer_quote_size : " << offer_quote_size << std::endl;
      std::cout << " -- spread_size : " << spread_size << std::endl;
      last = now;
    }


    // a way to cancel all your open orders
    // for (const auto& x : state.open_orders) {
    //   place_cancel(com, Common::Cancel{
    //     .ticker = 0,
    //     .order_id = x.first,
    //     .trader_id = trader_id
    //   });
    // }


    // a way to get your current position
    // quantity_t position = state.positions[0];


    // MARKET MAKING CODE
    // if (best_bid != 0.0 && best_offer != 0.0 && spread_size >= 0.1 && std::abs(position) < 20) { // 0.0 denotes no bid

    //   place_order(com, Common::Order{
    //     .ticker = 0,
    //     .price = best_bid + 0.01,
    //     .quantity = 1,
    //     .buy = true,
    //     .ioc = false,
    //     .order_id = last_order_id, // this order ID will be chosen randomly by com
    //     .trader_id = trader_id
    //   });
    //   ++last_order_id;

    //   place_order(com, Common::Order{
    //     .ticker = 0,
    //     .price = best_offer - 0.01,
    //     .quantity = 1,
    //     .buy = false,
    //     .ioc = false,
    //     .order_id = last_order_id, // this order ID will be chosen randomly by com
    //     .trader_id = trader_id
    //   });
    //   ++last_order_id;

    //   if (INFO) {
    //     std::cout << "Placing spread " << best_bid+0.01 << ", " << best_offer-0.01 << std::endl;
    //   }
    // }

  }

  // EDIT THIS METHOD
  void on_cancel_update(Common::CancelUpdate & update, Bot::Communicator& com){
    if (DEBUG) {
      std::cout << "Called on CANCEL update: " << update.getMsg() << std::endl;
    }
    state.on_cancel_update(update);
  }

  // (maybe) EDIT THIS METHOD
  void on_reject_order_update(Common::RejectOrderUpdate& update, Bot::Communicator& com) {
    if (DEBUG) {
      std::cout << "Called on REJECT ORDER update: " << update.getMsg() << std::endl;
    }
    std::cout << update.getMsg() << std::endl;
  }

  // (maybe) EDIT THIS METHOD
  void on_reject_cancel_update(Common::RejectCancelUpdate& update, Bot::Communicator& com) {
    if (DEBUG) {
      std::cout << "Called on REJECT CANCEL update: " << update.getMsg() << std::endl;
    }

    if (update.reason != Common::INVALID_ORDER_ID) {
      std::cout << update.getMsg() << std::endl;
    }
  }

  // (maybe) EDIT THIS METHOD
  void on_packet_start(Bot::Communicator& com) {
    trade_with_me_in_this_packet = false;
  }

  // (maybe) EDIT THIS METHOD
  void on_packet_end(Bot::Communicator& com) {
    if (trade_with_me_in_this_packet) {

      price_t pnl = state.get_pnl();

      std::cout << "got trade with me; pnl = "
                << std::setw(15) << std::left << pnl
                << " ; position = "
                << std::setw(5) << std::left << state.positions[0]
                << " ; pnl/s = "
                << std::setw(15) << std::left << (pnl/((time_ns() - start_time)/1e9))
                << " ; pnl/volume = "
                << std::setw(15) << std::left << (state.volume_traded ? pnl/state.volume_traded : 0.0)
                << std::endl;
    }
  }

  order_id_t place_order(Bot::Communicator& com, const Common::Order& order) {
    Common::Order copy = order;

    copy.order_id = com.place_order(order);

    state.on_place_order(copy);

    return copy.order_id;
  }

  void place_cancel(Bot::Communicator& com, const Common::Cancel& cancel) {
    com.place_cancel(cancel);
  }

};

// LogBot
//
// - Does nothing except log the top of book stats for each tick.
//
//
class LogBot: public Bot::AbstractBot {

public:

  MyState state;

  using Bot::AbstractBot::AbstractBot;

  std::ofstream prices_file;

  int64_t last = 0, start_time;
  uint64_t last_order_id = 0;

  bool trade_with_me_in_this_packet = false;

  // (maybe) EDIT THIS METHOD
  void init(Bot::Communicator& com) {
    state.trader_id = trader_id;
    start_time = time_ns();
    prices_file.open("prices.csv");
    prices_file << "time,best_bid,best_offer,bid_quote_size,offer_quote_size,cbrt_price,sqrt_price,weighted_price,square_price,midpoint_price,update_type,order_id,side,update_price,quantity,trader_id" << std::endl;
  }

  void write_to_csv(std::string type, order_id_t order_id, bool buy, price_t price, quantity_t qty) {
    int64_t curr = time_ns() - start_time;
    price_t best_bid = state.get_bbo(0, true);
    price_t best_offer = state.get_bbo(0, false);
    quantity_t bid_quote_size = state.get_quote_size(0, true);
    quantity_t offer_quote_size = state.get_quote_size(0, false);

    prices_file << curr << ",";
    prices_file << best_bid << ",";
    prices_file << best_offer << ",";
    prices_file << bid_quote_size << ",";
    prices_file << offer_quote_size << ",";
    prices_file << cube_root_price(best_bid, best_offer, bid_quote_size, offer_quote_size) << ",";
    prices_file << square_root_price(best_bid, best_offer, bid_quote_size, offer_quote_size) << ",";
    prices_file << weighted_price(best_bid, best_offer, bid_quote_size, offer_quote_size) << ",";
    prices_file << square_price(best_bid, best_offer, bid_quote_size, offer_quote_size) << ",";
    prices_file << (best_bid + best_offer) / 2 << ",";
    prices_file << type << ",";
    prices_file << order_id << ",";
    prices_file << (buy ? 1 : 0) << ",";
    prices_file << price << ",";
    prices_file << qty << std::endl;
  }

  // EDIT THIS METHOD
  void on_trade_update(Common::TradeUpdate& update, Bot::Communicator& com){

    write_to_csv("TRADE", update.aggressing_order_id, update.buy, update.price, update.quantity);
    state.on_trade_update(update);

    if (state.submitted.count(update.resting_order_id) ||
        state.submitted.count(update.aggressing_order_id)) {
      trade_with_me_in_this_packet = true;
    }
  }

  // EDIT THIS METHOD
  void on_order_update(Common::OrderUpdate & update, Bot::Communicator& com){
    
    write_to_csv("ORDER", update.order_id, update.buy, update.price, update.quantity);

    state.on_order_update(update);

    // This order is our order -- do not respond.
    if (state.submitted.count(update.order_id)) {
      return;
    }
  }

  // EDIT THIS METHOD
  void on_cancel_update(Common::CancelUpdate & update, Bot::Communicator& com){
    write_to_csv("CANCEL", update.order_id, 0, 0, 0);
    state.on_cancel_update(update);
  }

  // (maybe) EDIT THIS METHOD
  void on_reject_order_update(Common::RejectOrderUpdate& update, Bot::Communicator& com) {
  }

  // (maybe) EDIT THIS METHOD
  void on_reject_cancel_update(Common::RejectCancelUpdate& update, Bot::Communicator& com) {
  }

  // (maybe) EDIT THIS METHOD
  void on_packet_start(Bot::Communicator& com) {
    trade_with_me_in_this_packet = false;
  }

  // (maybe) EDIT THIS METHOD
  void on_packet_end(Bot::Communicator& com) {
  }

  order_id_t place_order(Bot::Communicator& com, const Common::Order& order) {
    Common::Order copy = order;

    copy.order_id = com.place_order(order);

    state.on_place_order(copy);

    return copy.order_id;
  }

  void place_cancel(Bot::Communicator& com, const Common::Cancel& cancel) {
    com.place_cancel(cancel);
  }

};

int main() {

  MomentumBot* m = new MomentumBot(1001);

  assert(m != NULL);

  Manager::Manager manager;

  manager.register_bot(m);

  manager.run();

  return 0;
}
