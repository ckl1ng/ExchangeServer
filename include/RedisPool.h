#ifndef REDISMANAGER_H
#define REDISMANAGER_H

#include <hiredis/hiredis.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <string>
#include <functional>
#include "Logger.hpp"

using RedisReplyPtr = std::unique_ptr<redisReply, std::function<void(redisReply*)>>;


// 连接池管理类，提供线程安全的 Redis 连接获取和归还功能
inline RedisReplyPtr make_reply(void* r) {
    return RedisReplyPtr(static_cast<redisReply*>(r), [](redisReply* reply) {
        if (reply) {
            freeReplyObject(reply);
        }
    });
}

class RedisPool {
private:
    RedisPool() = default;
    
    std::string host_;
    int port_;
    std::string password_;
    
    std::queue<redisContext*> connQueue_;
    std::mutex mtx_;
    std::condition_variable cv_;

public:
    static RedisPool& getInstance() {
        static RedisPool instance;
        return instance;
    }

    // 禁止复制和赋值
    ~RedisPool();

    // 初始化连接池，创建指定数量的 Redis 连接并加入连接队列
    void init(const std::string& host, int port, const std::string& password, int maxSize);
    
    // 获取 Redis 连接：从连接队列中取出一个连接，如果没有可用连接则等待
    std::unique_ptr<redisContext, std::function<void(redisContext*)>> getConnection();
};


#endif