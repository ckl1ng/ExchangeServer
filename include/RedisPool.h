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


/**
 * @brief创建一个管理reply的指针
 */
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

    /**
     * @brief 清理所有连接资源
     */
    ~RedisPool();

    /**
     * @brief 执行物理连接创建并压入队列
     * 
     * @details 
     * 包含身份验证逻辑。若某个连接创建失败，会跳过该连接并记录错误日志。
     */
    void init(const std::string& host, int port, const std::string& password, int maxSize);
    
    /**
     * @brief 获取连接并配置自动归还的 Deleter
     * 
     * @note 
     * 这里的核心逻辑是 unique_ptr 的销毁器，它不执行 redisFree，
     * 而是执行 connQueue_.push(c) 并通知条件变量。
     */
    std::unique_ptr<redisContext, std::function<void(redisContext*)>> getConnection();
};


#endif