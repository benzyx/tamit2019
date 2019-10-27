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
price_t cube_root_price(price_t best_bid, price_t best_offer, quantity_t bid_quote_size, quantity_t offer_quote_size) const {
  const bid_weight = std::cbrt(bid_quote_size);
  const offer_weight = std::cbrt(offer_quote_size);
  return (bid_weight * best_bid + offer_weight * best_offer) / (bid_weight + offer_weight);
}
// PRICING TYPE 2
price_t square_root_price(price_t best_bid, price_t best_offer, quantity_t bid_quote_size, quantity_t offer_quote_size) const {
  const bid_weight = std::sqrt(bid_quote_size);
  const offer_weight = std::sqrt(offer_quote_size);
  return (bid_weight * best_bid + offer_weight * best_offer) / (bid_weight + offer_weight);
}

// PRICING TYPE 3
price_t weighted_price(price_t best_bid, price_t best_offer, quantity_t bid_quote_size, quantity_t offer_quote_size) const {
  const bid_weight = bid_quote_size;
  const offer_weight = offer_quote_size;
  return (bid_weight * best_bid + offer_weight * best_offer) / (bid_weight + offer_weight);
}

// PRICING TYPE 4
price_t square_price(price_t best_bid, price_t best_offer, quantity_t bid_quote_size, quantity_t offer_quote_size) const {
  const bid_weight = bid_quote_size * bid_quote_size;
  const offer_weight = offer_quote_size * offer_quote_size;
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


/// BETTERBOOK
///

struct BookV2 {
public:

  BookV2() {}

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

// LogBot
//
// - Does nothing except log the top of book stats for each tick.
//
//
class LogBot: public Bot::AbstractBot {

public:

  MyState state;

  using Bot::AbstractBot::AbstractBot;

  std::ofstream("prices.csv") prices_file;

  int64_t last = 0, start_time;
  uint64_t last_order_id = 0;

  bool trade_with_me_in_this_packet = false;

  // (maybe) EDIT THIS METHOD
  void init(Bot::Communicator& com) {
    state.trader_id = trader_id;
    start_time = time_ns();

    prices_file << "time,best_bid,best_offer,bid_quote_size,offer_quote_size,cbrt_price,sqrt_price,weighted_price,square_price,midpoint_price,update_type,order_id,side,update_price,quantity,trader_id" << std::endl;
  }

  void write_to_csv(std::string type, order_id_t order_id, bool buy, price_t price, quantity_t qty, trader_id_t trader_id) {
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
    prices_file << buy ? 1 : 0 << ",";
    prices_file << price << ",";
    prices_file << qty << ",";
    prices_file << trader_id << std::endl;
  }

  // EDIT THIS METHOD
  void on_trade_update(Common::TradeUpdate& update, Bot::Communicator& com){

    write_to_csv("TRADE", update.aggressing_order_id, update.buy, update.price, update.quantity, update.trader_id);
    state.on_trade_update(update);

    if (state.submitted.count(update.resting_order_id) ||
        state.submitted.count(update.aggressing_order_id)) {
      trade_with_me_in_this_packet = true;
    }
  }

  // EDIT THIS METHOD
  void on_order_update(Common::OrderUpdate & update, Bot::Communicator& com){
    
    write_to_csv("ORDER", update.order_id, update.buy, update.price, update.quantity, update.trader_id);

    state.on_order_update(update);

    // This order is our order -- do not respond.
    if (state.submitted.count(update.order_id)) {
      return;
    }
  }

  // EDIT THIS METHOD
  void on_cancel_update(Common::CancelUpdate & update, Bot::Communicator& com){
    write_to_csv("CANCEL", update.order_id, update.buy, update.price, update.quantity, update.trader_id);
    state.on_cancel_update(update);
  }

  // (maybe) EDIT THIS METHOD
  void on_reject_order_update(Common::RejectOrderUpdate& update, Bot::Communicator& com) {
    write_to_csv("REJECT ORDER", update.order_id, update.buy, update.price, update.quantity, update.trader_id);
  }

  // (maybe) EDIT THIS METHOD
  void on_reject_cancel_update(Common::RejectCancelUpdate& update, Bot::Communicator& com) {
    write_to_csv("REJECT CANCEL", update.order_id, update.buy, update.price, update.quantity, update.trader_id);
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
