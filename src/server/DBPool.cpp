/**
 * @file DBPool.cpp
 * @brief MySQL 数据库连接池的具体实现 (MySQL Connection Pool Implementation)
 * 
 * 该模块实现了高性能的数据库连接管理逻辑：
 * 1. 预创建连接：初始化时建立指定数量的长连接，减少业务触发时的握手开销。
 * 2. 线程安全调度：通过条件变量 (Condition Variable) 实现多线程竞争连接时的排队与唤醒。
 * 3. 自动归还机制：利用 std::unique_ptr 的自定义销毁器，实现连接的自动回收复用。
 */

#include "DBPool.h"
#include <mysql/mysql.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <string>
#include <functional>
#include <iostream>
#include <cstring>
#include "json.hpp"
#include "Logger.hpp"

DBPool::~DBPool() {
    std::lock_guard<std::mutex> lock(mtx_);
    while (!connQueue_.empty()) {
        mysql_close(connQueue_.front());
        connQueue_.pop();
    }
}

void DBPool::init(const std::string& host, const std::string& user, const std::string&password, const std::string& db, int maxSize) {
    host_ = host; user_ = user; pwd_ = password; db_ = db;

    for (int i = 0; i < maxSize; i++){
        MYSQL* conn = mysql_init(NULL);
        // 执行物理连接尝试
        if (mysql_real_connect(conn, host_.c_str(), user_.c_str(), pwd_.c_str(), db_.c_str(), 0, NULL, 0)) {
            connQueue_.push(conn);
        } else {
            LOG_ERROR("数据库连接失败: " + std::string(mysql_error(conn)));
        }
    }
    LOG_INFO("数据库连接池初始化完成, 可用连接数: " + std::to_string(connQueue_.size()));
}

std::unique_ptr<MYSQL, std::function<void(MYSQL*)>> DBPool::getConnection() {
    std::unique_lock<std::mutex> lock(mtx_);
    
    // 如果池已空，阻塞等待直到有其他线程归还连接
    cv_.wait(lock, [this] { return !connQueue_.empty(); });

    MYSQL* conn = connQueue_.front();
    connQueue_.pop();

    // 定义归还逻辑（自定义销毁器）
    auto deleter = [this](MYSQL* c) {
        if (c != nullptr) {
            std::lock_guard<std::mutex> res_lock(mtx_);
            connQueue_.push(c);
            cv_.notify_one(); // 唤醒正在阻塞等待获取连接的业务线程
        }
    };

    return std::unique_ptr<MYSQL, decltype(deleter)>(conn, deleter);
}