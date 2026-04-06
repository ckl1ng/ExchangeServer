#ifndef MARKETDATAPUBLISHER_HPP
#define MARKETDATAPUBLISHER_HPP

#pragma once
#include "RedisPool.h"
#include "Common.hpp"
#include "blockingconcurrentqueue.h"
#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <atomic>
#include <string>

class MarketDataPublisher {
private:
    moodycamel::BlockingConcurrentQueue<MatchRecord> queue_;
    std::atomic<bool> running_;
    std::thread worker_thread_;

    MarketDataPublisher() : running_(true) {
        worker_thread_ = std::thread(&MarketDataPublisher::processQueue, this);
    }

    void processQueue() {
        const size_t batch_size = 1000;
        MatchRecord buffer[batch_size];

        while (running_) {
            size_t count = queue_.wait_dequeue_bulk_timed(buffer, batch_size, std::chrono::milliseconds(1));
            if (count > 0) {
                std::vector<MatchRecord> res(buffer, buffer + count);
                publishBatchToRedis(res);
            }
        }
    }

    void publishBatchToRedis(const std::vector<MatchRecord>& batch) {
        auto conn_ptr = RedisPool::getInstance().getConnection();
        if (!conn_ptr) return;
        redisContext* conn = conn_ptr.get();

        for (const auto& rec : batch) {
            std::string json_data = "{\"ticker\":" + std::to_string(rec.ticker_id) + 
                                    ",\"price\":" + std::to_string(rec.match_price) + 
                                    ",\"qty\":" + std::to_string(rec.match_quantity) + "}";
            std::string zset_key = "history:trade:" + std::to_string(rec.ticker_id);
            redisAppendCommand(conn, "ZADD %s %llu %s", zset_key.c_str(), rec.timestamp, json_data.c_str());
        }

        redisReply* reply = nullptr;
        for (size_t i = 0; i < batch.size(); i++) {
            if (redisGetReply(conn, (void**)&reply) == REDIS_OK) {
                // 修复：确保释放 reply 防止内存泄漏
                if (reply) freeReplyObject(reply);
            }
        }
    }

public:
    static MarketDataPublisher& getInstance() {
        static MarketDataPublisher instance;
        return instance;
    }

    ~MarketDataPublisher() {
        running_ = false;
        if (worker_thread_.joinable()) worker_thread_.join();
    }

    void pushRecords(const std::vector<MatchRecord>& records) {
        if (records.empty()) return;
        queue_.enqueue_bulk(records.begin(), records.size());
    }

    MarketDataPublisher(const MarketDataPublisher&) = delete;
    MarketDataPublisher& operator=(const MarketDataPublisher&) = delete;
};

#endif