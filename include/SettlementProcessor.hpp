#ifndef SETTLEMENT_PROCESSOR_HPP
#define SETTLEMENT_PROCESSOR_HPP

#include "Common.hpp"
#include "Repositories.h"
#include "concurrentqueue.h"
#include "UserManager.hpp"
#include "TickerManager.hpp"
#include <thread>
#include <atomic>
#include <vector>
#include <memory>

// 负责处理撮合结果的清算逻辑，更新用户余额和持仓
class SettlementProcessor {
private:
    moodycamel::ConcurrentQueue<MatchRecord> queue_;
    moodycamel::ConcurrentQueue<CancelRecord> cancel_queue_;
    std::thread worker_thread_;
    std::atomic<bool> running_{true};

    std::shared_ptr<IUserRepository> userRepo_;
    std::shared_ptr<IStockRepository> stockRepo_;

    // 后台线程函数：持续处理撮合结果和撤单记录，更新用户账户信息
    void processLoop() {
        const size_t batch_size = 1000;
        MatchRecord buffer[batch_size];
        CancelRecord cancel_buffer[batch_size];

        while (running_ || queue_.size_approx() > 0 || cancel_queue_.size_approx() > 0) {
            size_t count = queue_.try_dequeue_bulk(buffer, batch_size);

            if (count > 0) {
                for (size_t i = 0; i < count; i++) {
                    const MatchRecord& rec = buffer[i];

                    std::string buyer_name = UserManager::getInstance().getName(rec.buyer_user_id);
                    std::string seller_name = UserManager::getInstance().getName(rec.sell_user_id);
                    std::string symbol = TickerManager::getInstance().getSymbol(rec.ticker_id);

                    int64_t sell_money = rec.match_price * rec.match_quantity;

                    int64_t refund_money = (rec.buyer_expected_price - rec.match_price) * rec.match_quantity;

                    userRepo_->deductBalance(buyer_name, sell_money);
                    stockRepo_->updateStockHolding(buyer_name, symbol, rec.match_quantity);
                    userRepo_->updateBalance(seller_name, sell_money);
                    stockRepo_->deductStockHolding(seller_name, symbol, rec.match_quantity);

                    UserManager::getInstance().commitBuy(rec.buyer_user_id, rec.ticker_id, sell_money, rec.match_quantity, refund_money);
                    UserManager::getInstance().commitSell(rec.sell_user_id, rec.ticker_id, sell_money, rec.match_quantity);
                }
            }


            size_t cancel_count = cancel_queue_.try_dequeue_bulk(cancel_buffer, batch_size);
            for (size_t i = 0; i < cancel_count; i++) {
                const CancelRecord& rec = cancel_buffer[i];

                if (rec.side == OrderSide::BUY) {
                    int64_t refund_money = rec.price * rec.quantity;
                    UserManager::getInstance().unfreezeBalance(rec.user_id, refund_money);
                } else {
                    UserManager::getInstance().unfreezeStock(rec.user_id, rec.ticker_id, rec.quantity);
                }
            }

            // 如果都没有任务，让出 CPU
            if (count == 0 && cancel_count == 0) {
                std::this_thread::yield();
            }
        }
    }

public:
    SettlementProcessor(std::shared_ptr<IUserRepository> userRepo, std::shared_ptr<IStockRepository> stockRepo) : userRepo_(userRepo), stockRepo_(stockRepo) {
        worker_thread_ = std::thread(&SettlementProcessor::processLoop, this);
        // LOG_INFO("清算线程开始");
    }

    ~SettlementProcessor() {
        running_ = false;
        if (worker_thread_.joinable())worker_thread_.join();
    }

    void pushRecords(const std::vector<MatchRecord>& records) {
        if (!records.empty()) {
            queue_.enqueue_bulk(records.begin(), records.size());
        }
    }

    void pushCancelRecord(const CancelRecord& record) {
        cancel_queue_.enqueue(record);
    }
};

#endif