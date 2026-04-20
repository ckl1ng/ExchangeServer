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