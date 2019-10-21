#include <cstdint>
#include <random>
#include <string>
#include <sstream>
#include <thread>
#include <vector>
#include <mutex>

typedef uint8_t update_type_t;
typedef int64_t quantity_t;
typedef uint64_t order_id_t;
typedef uint64_t trader_id_t;
typedef double price_t;
typedef uint8_t ticker_t;
const int MAX_NUM_TICKERS = 256;
const uint64_t MAX_MESSAGES = UINT64_MAX;


namespace Common {

  static inline price_t round_price(price_t price) {
    return round(price * 100.0) / 100.0;
  }


  struct Order {
    // TODO sort these fields by size for struct packing
    // (make sure nothing in the code depends on this ordering)

    ticker_t ticker;
    price_t price;
    quantity_t quantity;
    bool buy;
    bool ioc;
    order_id_t order_id;
    trader_id_t trader_id;
  };

  struct Cancel {
    ticker_t ticker;
    order_id_t order_id;
    trader_id_t trader_id;
  };

  enum UpdateType {
    TRADE, ORDER, CANCEL, REJECT_ORDER, REJECT_CANCEL
  };
  enum RejectReason {
    NO_REASON, INVALID_PARAMETERS, INVALID_TRADER_ID, INVALID_TICKER, INVALID_ORDER_ID, RATE_LIMIT_EXCEEDED, OPEN_ORDERS_EXCEEDED, POSITION_LIMIT_EXCEEDED, PNL_LIMIT_EXCEEDED
  };

    struct TradeUpdate{
      ticker_t ticker;
      price_t price;
      quantity_t quantity;
      order_id_t resting_order_id;
      order_id_t aggressing_order_id;
      bool buy; // direction of aggressing order

      std::string getMsg(){

        std::stringstream ss;
        ss << "Trade update: ticker=" << (int)ticker;
        ss << ", quantity="<< quantity  << ", price=" << price
          << ", resting_order_id=" << resting_order_id <<
        ", aggressing_order_id=" << aggressing_order_id
          << ", buy=" << buy;
        return ss.str();
      }
  };

  struct OrderUpdate {
    ticker_t ticker;
    price_t price;
    quantity_t quantity;
    order_id_t order_id;
    bool buy;

    std::string getMsg(){
        std::stringstream ss;
        ss << "Order update: ticker=" << (int)ticker << ", quantity=" << this->quantity
           << ", price=" << this->price << ", buy=" << buy << ", order_id=" << order_id;
        return ss.str();
    }
  };

  struct CancelUpdate {
    ticker_t ticker;
    order_id_t order_id;
    std::string getMsg(){
        std::stringstream ss;
        ss << "Cancel update: ticker=" << (int) ticker
           << ", order_id=" << order_id;
        return ss.str();
    }
  };
  struct RejectOrderUpdate{
    ticker_t ticker;
    order_id_t order_id;
    RejectReason reason;
    std::string pretty_reason(RejectReason x){
      switch(x){
        case NO_REASON: return "No reason";
        case INVALID_PARAMETERS: return "INVALID_PARAMETERS";
        case INVALID_TRADER_ID: return "INVALID_TRADER_ID";
        case INVALID_TICKER: return "INVALID_TICKER";
        case INVALID_ORDER_ID: return "INVALID_ORDER_ID";
        case RATE_LIMIT_EXCEEDED: return "RATE_LIMIT_EXCEEDED";
        case OPEN_ORDERS_EXCEEDED: return "OPEN_ORDERS_EXCEEDED";
        case POSITION_LIMIT_EXCEEDED: return "POSITION_LIMIT_EXCEEDED";
        case PNL_LIMIT_EXCEEDED: return "PNL_LIMIT_EXCEEDED";
        default: return "Unknown";
      }
    }
    std::string getMsg(){
        std::stringstream ss;
        ss << "Reject order update: ticker=" << (int) ticker
           << ", order_id=" << order_id
           << ", reason: " << pretty_reason(reason);
        return ss.str();
    }
  };
  struct RejectCancelUpdate{
    ticker_t ticker;
    order_id_t order_id;
    RejectReason reason;
    std::string pretty_reason(RejectReason x){
      switch(x){
        case NO_REASON: return "No reason";
        case INVALID_PARAMETERS: return "INVALID_PARAMETERS";
        case INVALID_TRADER_ID: return "INVALID_TRADER_ID";
        case INVALID_TICKER: return "INVALID_TICKER";
        case INVALID_ORDER_ID: return "INVALID_ORDER_ID";
        case RATE_LIMIT_EXCEEDED: return "RATE_LIMIT_EXCEEDED";
        case OPEN_ORDERS_EXCEEDED: return "OPEN_ORDERS_EXCEEDED";
        case POSITION_LIMIT_EXCEEDED: return "POSITION_LIMIT_EXCEEDED";
        case PNL_LIMIT_EXCEEDED: return "PNL_LIMIT_EXCEEDED";
        default: return "Unknown";
      }
    }
    std::string getMsg(){
        std::stringstream ss;
        ss << "Reject cancel update: ticker=" << (int) ticker
           << ", order_id=" << order_id
           << ", reason: " << pretty_reason(reason);
        return ss.str();
    }
  };
};


namespace Router {
  class Sender;
  class Receiver;
};


namespace Bot {

  class AbstractBot;

  class Communicator {
  public:
    Communicator(AbstractBot & bot, Router::Sender & sender, Router::Receiver & receiver);
    void communicate_in_thread();
    void communicate();
    order_id_t place_order(const Common::Order& order);
    void place_cancel(const Common::Cancel& cancel);
  private:
    order_id_t random_order_id();

    AbstractBot & bot_;
    Router::Sender & sender_;
    Router::Receiver & receiver_;
    std::thread t_;
    std::mt19937_64 mersenne;
    std::mutex mu;
  };

  class AbstractBot {
  public:
    AbstractBot(trader_id_t trader_id);
    virtual void init(Communicator& com) {};
    virtual void on_trade_update(Common::TradeUpdate& update, Communicator& com) = 0;
    virtual void on_order_update(Common::OrderUpdate& update, Communicator& com) = 0;
    virtual void on_cancel_update(Common::CancelUpdate& update, Communicator& com) = 0;

    // TODO make pure virtual
    virtual void on_reject_order_update(Common::RejectOrderUpdate& update, Communicator& com) {};
    virtual void on_reject_cancel_update(Common::RejectCancelUpdate& update, Communicator& com) {};
    virtual void on_packet_start(Communicator& com) {};
    virtual void on_packet_end(Communicator& com) {};

    trader_id_t getTraderId() {
      return trader_id;
    }

  protected:
    trader_id_t trader_id;
  };

};


namespace Manager {

  class Manager {
  public:
    void register_bot(Bot::AbstractBot* bot);
    void run();
  private:
    std::vector<Bot::AbstractBot*> bots;
    std::vector<Bot::Communicator*> coms;
  };
};
