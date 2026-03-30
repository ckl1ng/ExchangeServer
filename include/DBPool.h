#ifndef DB_POOL_H
#define DB_POOL_H

#include <mysql/mysql.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <string>
#include <functional>
#include "json.hpp"

/**
 * @brief 从池中获取一个可用的数据库连接
 * 
 * 动态结合 RAII 机制，利用自定义销毁器 (Deleter) 实现连接的自动“假销毁、真归还”。
 * 
 * @return std::unique_ptr<MYSQL, std::function<void(MYSQL*)>> 
 *         返回一个托管的 MYSQL 指针。当该指针离开作用域时，连接会自动放回池中。
 * 
 * @throw std::runtime_error 如果连接池已损坏或由于极端原因无法分配资源
 * 
 * @warning 请勿手动调用 mysql_close() 释放返回的指针，否则会导致连接池失效。
 */
class DBPool {
public:
    static DBPool& getInstance() {
        static DBPool instance;
        return instance;
    }

    ~DBPool();
    void init(const std::string& host, const std::string& user, const std::string&password, const std::string& db, int maxSize);
    std::unique_ptr<MYSQL, std::function<void(MYSQL*)>> getConnection();

private:
    DBPool() {}
    std::string host_, user_, pwd_, db_;
    std::queue<MYSQL*> connQueue_;
    std::mutex mtx_;
    std::condition_variable cv_;
};

#endif