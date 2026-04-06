#ifndef ORDERBOOK_HPP
#define ORDERBOOK_HPP

#include "Common.hpp"
#include <iostream>
#include <map>
#include <list>
#include <vector>
#include <memory>
#include <unordered_map>

class OrderBook {
private:
    uint32_t ticker_id_;
    using OrderList = std::list<std::shared_ptr<Order>>;
    std::map<uint64_t, OrderList, std::greater<uint64_t>> bids_;
    std::map<uint64_t, OrderList, std::less<uint64_t>> asks_;

    std::unordered_map<uint64_t, std::unordered_map<uint64_t, std::shared_ptr<Order>>> user_active_orders_;

    void matchBuyOrder(std::shared_ptr<Order> buy_order, std::vector<MatchRecord>& results) {
        auto it = asks_.begin();
        while (it != asks_.end() && buy_order->quantity > 0) {
            uint64_t ask_price = it->first;
            if (ask_price > buy_order->price) break;

            OrderList& ask_list = it->second;
            auto list_it = ask_list.begin();

            while (list_it != ask_list.end() && buy_order->quantity > 0) {
                auto ask_order = *list_it;
                uint64_t trade_qty = std::min(buy_order->quantity, ask_order->quantity);

                results.push_back({
                    ticker_id_, 
                    ask_order->order_id, 
                    buy_order->order_id, 
                    buy_order->user_id,
                    ask_order->user_id,
                    buy_order->price,
                    ask_price, 
                    trade_qty,
                    static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count())
                });
                buy_order->quantity -= trade_qty;
                ask_order->quantity -= trade_qty;

                if (ask_order->quantity == 0) {
                    list_it = ask_list.erase(list_it);
                    user_active_orders_[ask_order->user_id].erase(ask_order->order_id);
                }
                else ++list_it;
            }
            if (ask_list.empty()) it = asks_.erase(it);
            else it++;
        }
        if (buy_order->quantity > 0) bids_[buy_order->price].push_back(buy_order);
    }

    void matchSellOrder(std::shared_ptr<Order> sell_order, std::vector<MatchRecord>& results) {
        auto it = bids_.begin();
        while (it != bids_.end() && sell_order->quantity > 0) {
            uint64_t bids_price = it->first;

            if (bids_price < sell_order->price) break;

            OrderList& bid_list = it->second;
            auto list_it = bid_list.begin();

            while (list_it != bid_list.end() && sell_order->quantity > 0) {
                auto bid_order = *list_it;
                uint64_t trade_qty = std::min(sell_order->quantity, bid_order->quantity);

                results.push_back({
                    ticker_id_, 
                    bid_order->order_id, 
                    sell_order->order_id, 
                    bid_order->user_id,
                    sell_order->user_id,
                    bid_order->price,
                    bids_price, 
                    trade_qty,
                    static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count())
                });
                
                sell_order->quantity -= trade_qty;
                bid_order->quantity -= trade_qty;

                if (bid_order->quantity == 0) {
                    list_it = bid_list.erase(list_it);
                    user_active_orders_[bid_order->user_id].erase(bid_order->order_id);
                }
                else ++list_it;
            }
            if (bid_list.empty()) it = bids_.erase(it);
            else it++;
        }
        if (sell_order->quantity > 0) asks_[sell_order->price].push_back(sell_order);
    }

public:
    explicit OrderBook(uint32_t ticker_id) : ticker_id_(ticker_id) {}

    void processOrder(std::shared_ptr<Order> new_order, std::vector<MatchRecord>& results) {
        if (new_order->side == OrderSide::BUY) {
            matchBuyOrder(new_order, results);
        }
        else {
            matchSellOrder(new_order, results);
        }

        if (new_order->quantity > 0) {
            user_active_orders_[new_order->user_id][new_order->order_id] = new_order;
        }
    }

    void getBestBidOffer(int64_t& best_bid, int64_t& best_ask) {
        best_bid = bids_.empty() ? 0 : (*bids_.begin()).first;
        best_ask = asks_.empty() ? 0 : (*asks_.begin()).first;
    }

    std::shared_ptr<Order> cancelOrder(const uint32_t& order_id, uint64_t user_id) {
        auto user_it = user_active_orders_.find(user_id);
        if (user_it == user_active_orders_.end()) return nullptr;

        auto order_it = user_it->second.find(order_id);
        if(order_it == user_it->second.end()) return nullptr;

        std::shared_ptr<Order> order_to_cancel = order_it->second;

        if (order_to_cancel->side == OrderSide::BUY) {
            auto& list = bids_[order_to_cancel->price];
            list.remove_if([order_id](const std::shared_ptr<Order>& o) { return o->order_id == order_id; });

            if (list.empty()) bids_.erase(order_to_cancel->price);
        } 
        else {
            auto& list = asks_[order_to_cancel->price];
            list.remove_if([order_id](const std::shared_ptr<Order>& o) { return o->order_id == order_id; });
            if (list.empty()) asks_.erase(order_to_cancel->price);
        }

        user_it->second.erase(order_it);
        return order_to_cancel;
    }

    std::vector<std::shared_ptr<Order>> getUserActiveOrder(uint64_t user_id) {
        std::vector<std::shared_ptr<Order>> active_orders;
        auto user_it = user_active_orders_.find(user_id);
        if (user_it != user_active_orders_.end()) {
            for (auto [id, order] : user_it->second) {
                active_orders.push_back(order);
            }
        }
        return active_orders;
    }
};

#endif