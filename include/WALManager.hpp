#ifndef WAL_MANAGER_HPP
#define WAL_MANAGER_HPP

#include "Common.hpp"
#include "EngineEvent.hpp"
#include "MatchingEngine.hpp"
#include "blockingconcurrentqueue.h"
#include "Logger.hpp"
#include <thread>
#include <atomic>
#include <vector>
#include <cstdio>
#include <unistd.h>

#pragma pack(push, 1)
struct WALRecord {
    uint8_t type;
    uint64_t timestamp; 
    uint64_t order_id;
    uint32_t ticker_id;
    uint64_t user_id;
    uint8_t side;
    uint64_t price;
    uint64_t quantity;
};
#pragma pack(pop)

class WALManager {
private:
    moodycamel::BlockingConcurrentQueue<EngineEvent> queue_; // 替换为 Blocking 队列
    std::thread worker_;
    std::atomic<bool> running_ {true};
    std::shared_ptr<MatchingEngine> engine_;
    FILE* wal_file_;

    void processerLoop() {
        const size_t batch_size = 1000;
        EngineEvent buffer[batch_size];
        std::vector<WALRecord> records;
        records.reserve(batch_size);

        while (running_) {
            size_t count = queue_.wait_dequeue_bulk_timed(buffer, batch_size, std::chrono::milliseconds(1));
            if (count > 0) {
                records.clear();

                for (size_t i = 0; i < count; i++) {
                    WALRecord rec;
                    rec.type = (buffer[i].type == EventType::NEW_ORDER) ? 0 : 1;
                    if (rec.type == 0) {
                        rec.timestamp = buffer[i].order->timestamp;
                        rec.order_id  = buffer[i].order->order_id;
                        rec.ticker_id = buffer[i].order->ticker_id;
                        rec.user_id   = buffer[i].order->user_id;
                        rec.side      = (buffer[i].order->side == OrderSide::BUY) ? 0 : 1;
                        rec.price     = buffer[i].order->price;
                        rec.quantity  = buffer[i].order->quantity;
                    } else {
                        rec.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
                        rec.order_id  = buffer[i].cancel_order_id;
                        rec.ticker_id = buffer[i].ticker_id;
                        rec.user_id   = buffer[i].cancel_user_id;
                        rec.side = 0; rec.price = 0; rec.quantity = 0;
                    }
                    records.push_back(rec);
                }

                // 修复：将记录写入 WAL 文件，交由 OS 缓冲以提升性能
                if (wal_file_ && !records.empty()) {
                    fwrite(records.data(), sizeof(WALRecord), records.size(), wal_file_);
                }

                for (size_t i = 0; i < count; i++) {
                    if (buffer[i].type == EventType::NEW_ORDER) {
                        engine_->processOrder(buffer[i].order);
                    } else {
                        engine_->CancelOrder(buffer[i].ticker_id, buffer[i].cancel_order_id, buffer[i].cancel_user_id);
                    }
                }
            }
        }
    }

public:
    WALManager(std::shared_ptr<MatchingEngine> engine) : engine_(engine) {
        wal_file_ = fopen("wal.bin", "ab");
        if (!wal_file_) LOG_ERROR("无法打开WAL日志文件");
        worker_ = std::thread(&WALManager::processerLoop, this);
    }

    ~WALManager() {
        running_ = false;
        if (worker_.joinable())worker_.join();
        if (wal_file_) fclose(wal_file_);
    }

    void appendOrder(std::shared_ptr<Order> order) {
        EngineEvent ev;
        ev.type = EventType::NEW_ORDER;
        ev.order = order;
        queue_.enqueue(ev);
    }

    void appendCancel(uint32_t ticker_id, uint64_t order_id, uint64_t user_id) {
        EngineEvent ev;
        ev.type = EventType::CANCEL_ORDER;
        ev.ticker_id = ticker_id;
        ev.cancel_order_id = order_id;
        ev.cancel_user_id = user_id;
        queue_.enqueue(ev);
    }
};

#endif