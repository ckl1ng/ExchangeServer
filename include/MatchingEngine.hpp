#ifndef MATCHING_ENGINE_H
#define MATCHING_ENGINE_H

#include "Repositories.h"
#include "RedisPool.h"
#include "Logger.hpp"
#include "OrderBookProcessor.hpp"
#include "MarketDataPublisher.hpp"
#include "SessionManager.hpp"
#include <mutex>
#include <memory>
#include <string>
#include <algorithm>

class MatchingEngine {
private:
    std::unordered_map<uint32_t, std::unique_ptr<OrderBookProcessor>> order_books_;
    std::shared_ptr<SettlementProcessor> settleProcessor_;

public:
    MatchingEngine(std::shared_ptr<SettlementProcessor> sp) : settleProcessor_(sp) {}

    void init(uint32_t ticker_id) {
        if (order_books_.find(ticker_id) == order_books_.end()) {
            order_books_[ticker_id] = std::make_unique<OrderBookProcessor>(ticker_id, settleProcessor_);
        }
    }

    bool processOrder(std::shared_ptr<Order> order) {
        auto it = order_books_.find(order->ticker_id);

        if (it == order_books_.end()) {
            // LOG_INFO("未找到股票id为 " + std::to_string(order->ticker_id) +  " 的股票");
            return false;
        }   
        EngineEvent ev;
        ev.type = EventType::NEW_ORDER;
        ev.order = order;
        it->second->enqueue(ev);

        return true;
    }

    void getBestBidOffer(uint64_t ticker_id, int64_t& best_bid, int64_t& best_ask) {
        auto it = order_books_.find(ticker_id);
        if (it != order_books_.end()) it->second->getBestBidOffer(best_bid, best_ask);
    }

    void CancelOrder(uint32_t ticker_id, uint64_t order_id, uint64_t user_id) {
        auto it = order_books_.find(ticker_id);
        if (it != order_books_.end()) {
            EngineEvent ev;
            ev.type = EventType::CANCEL_ORDER;
            ev.cancel_order_id = order_id;
            ev.cancel_user_id = user_id;
            it->second->enqueue(ev);
        }
    }

    std::vector<std::shared_ptr<Order>> getUserActiveOrder(uint64_t user_id) {
        std::vector<std::shared_ptr<Order>> all_orders;
        for (const auto& [ticker_id, order_book] : order_books_) {
            std::vector<std::shared_ptr<Order>> my_order = order_book->getUserOrder(user_id);
            all_orders.insert(all_orders.end(), my_order.begin(), my_order.end());
        }
        return all_orders;
    }
};

#endif