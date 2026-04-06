#ifndef ORDER_BOOK_PROCESSOR_HPP
#define ORDER_BOOK_PROCESSOR_HPP

#include "OrderBook.hpp"
#include "blockingconcurrentqueue.h"
#include "EngineEvent.hpp"
#include "MarketDataPublisher.hpp"
#include "SettlementProcessor.hpp"
#include <thread>
#include <atomic>
#include <vector>

class OrderBookProcessor {
private:
    uint32_t ticker_id_;
    OrderBook book_;
    moodycamel::BlockingConcurrentQueue<EngineEvent> queue_; // 替换为 Blocking 队列

    std::thread worker_thread_;
    std::atomic<bool> running_{true};

    std::shared_ptr<SettlementProcessor> settlementProcessor_;

    void processLoop() {
        const size_t batch_size = 1000;
        EngineEvent buffer[batch_size];
        std::vector<MatchRecord> results;
        results.reserve(batch_size);

        while (running_) {
            // 使用 wait_dequeue_bulk_timed 避免空转和 sleep_for 带来的延迟
            size_t count = queue_.wait_dequeue_bulk_timed(buffer, batch_size, std::chrono::milliseconds(1));
            if (count > 0) {
                results.clear();

                for (size_t i = 0; i < count; i++) {
                    auto& event = buffer[i];
                    if (event.type == EventType::NEW_ORDER) {
                        book_.processOrder(event.order, results);
                    }
                    else if (event.type == EventType::CANCEL_ORDER) {
                        auto canceled_order = book_.cancelOrder(event.cancel_order_id, event.cancel_user_id);
                        if (canceled_order != nullptr && settlementProcessor_) {
                            CancelRecord cr;
                            cr.ticker_id = canceled_order->ticker_id;
                            cr.order_id  = canceled_order->order_id;
                            cr.user_id   = canceled_order->user_id;
                            cr.side      = canceled_order->side;
                            cr.price     = canceled_order->price;
                            cr.quantity  = canceled_order->quantity;
                            cr.timestamp = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
                            settlementProcessor_->pushCancelRecord(cr);
                        }
                    }
                }

                if (!results.empty()) {
                    MarketDataPublisher::getInstance().pushRecords(results);
                    if (settlementProcessor_) {
                        settlementProcessor_->pushRecords(results);
                    }
                }
            }
        }
    }

public:
    OrderBookProcessor(uint32_t ticker_id, std::shared_ptr<SettlementProcessor> sp) : ticker_id_(ticker_id), book_(ticker_id), settlementProcessor_(sp) {
        worker_thread_ = std::thread(&OrderBookProcessor::processLoop, this);
    }

    ~OrderBookProcessor() {
        running_ = false;
        if (worker_thread_.joinable()) worker_thread_.join();
    }

    void enqueue(const EngineEvent& ev) {
        queue_.enqueue(ev);
    }

    void getBestBidOffer(int64_t& bid, int64_t& ask) {
        book_.getBestBidOffer(bid, ask);
    }

    std::vector<std::shared_ptr<Order>> getUserOrder(uint64_t uid) {
        return book_.getUserActiveOrder(uid);
    }
};

#endif