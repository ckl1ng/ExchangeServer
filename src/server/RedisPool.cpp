/**
 * @file RedisPool.cpp
 * @brief Redis 连接池调度与管理逻辑实现 (Redis Connection Pool Implementation)
 * 
 * 该模块提供了高性能的 Redis 资源调度方案：
 * 1. 线程同步：利用 std::condition_variable 实现生产-消费者模型的连接借出。
 * 2. 身份验证：内置自动 AUTH 认证逻辑。
 * 3. 资源复用：采用独创的智能指针销毁器劫持技术，实现零开销的连接回收。
 */

#include "RedisPool.h"
#include "Logger.hpp"
#include <iostream>

RedisPool::~RedisPool() {
    std::lock_guard<std::mutex> lock(mtx_);
    while (!connQueue_.empty()) {
        redisContext* conn = connQueue_.front();
        connQueue_.pop();
        if (conn) {
            redisFree(conn);
        }
    } 
}

void RedisPool::init(const std::string& host, int port, const std::string& password, int maxSize) {
    host_ = host;
    port_ = port;
    password_ = password;

    for (int i = 0; i < maxSize; i++) {
        redisContext* conn = redisConnect(host_.c_str(), port_);
        if (conn == nullptr || conn->err) {
            if (conn) {
                LOG_ERROR("Redis连接失败: " + std::string(conn->errstr));
                redisFree(conn);
            }
            else {
                LOG_ERROR("分配Redis上下文失败");
            }
            continue;
        }

        // --- 自动身份验证 ---
        if (!password_.empty()) {
            redisReply* reply = (redisReply*)redisCommand(conn, "AUTH %s", password_.c_str());
            if (reply == nullptr || reply->type == REDIS_REPLY_ERROR) {
                LOG_ERROR("Redis密码认证失败");
                if (reply) freeReplyObject(reply);
                redisFree(conn);
                continue;
            }
            freeReplyObject(reply);
        }

        connQueue_.push(conn);
    }

    LOG_INFO("Redis连接池初始化完成, 可用连接数： " + std::to_string(connQueue_.size()));
}

std::unique_ptr<redisContext, std::function<void(redisContext*)>> RedisPool::getConnection(){
    std::unique_lock<std::mutex> lock(mtx_);
    
    // 队列为空时阻塞，直到 cv_.notify_one() 唤醒
    cv_.wait(lock, [this] { return !connQueue_.empty(); });

    redisContext* conn = connQueue_.front();
    connQueue_.pop();

    // --- 核心设计：RAII 自动归还器 ---
    auto deleter = [this](redisContext *c) {
        if (c != nullptr) {
            std::lock_guard<std::mutex> res_lock(mtx_);
            // 归还连接至队尾
            connQueue_.push(c);
            // 信号通知：已释放资源，唤醒正在排队的其他业务线程
            cv_.notify_one();
        }
    };

    return std::unique_ptr<redisContext, decltype(deleter)>(conn, deleter);
}