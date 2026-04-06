#ifndef USERMANAGER_HPP
#define USERMANAGER_HPP

#include "Logger.hpp"
#include "DBPool.h"
#include "TickerManager.hpp"
#include <unordered_map>
#include <iomanip>
#include <sstream>
#include <random>
#include <chrono>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <shared_mutex> 
#include <string>
#include <memory>
#include <vector>

struct UserAccount {
    uint64_t user_id;
    std::string username;

    int64_t available_balance = 0;
    int64_t frozen_balance = 0;
    std::unordered_map<uint32_t, int> available_stock;
    std::unordered_map<uint32_t, int> frozen_stock;

    std::mutex mtx;
};

/**
 * @class UserManager
 * @brief 全局用户 ID 与 Username 双向内存映射缓存 (分段锁并发优化)
 */
class UserManager {
private:
    // 将两个 map 和锁合并的结构体
    struct AccountBucket {
        std::shared_mutex mtx;
        std::unordered_map<uint64_t, std::unique_ptr<UserAccount>> account_;
        std::unordered_map<std::string, UserAccount*> find_account_;
    };

    static constexpr size_t mtx_count = 256;
    AccountBucket mtx_[mtx_count]; // 分段桶数组

    UserManager() = default;

    size_t IdToMtx(uint64_t uid) {
        return uid % mtx_count;
    }

    size_t nameToMtx(const std::string& username) {
        return std::hash<std::string>{}(username) % mtx_count;
    }

    UserAccount* getAccount(uint64_t uid) {
        size_t idx = IdToMtx(uid);
        std::shared_lock<std::shared_mutex> lock(mtx_[idx].mtx); // 使用读锁，允许多线程并发读取
        auto it = mtx_[idx].account_.find(uid);
        if (it == mtx_[idx].account_.end()) return nullptr;
        return it->second.get();
    }

public:
    static UserManager& getInstance() {
        static UserManager instance;
        return instance;
    }

    void loadAllFromDB() {
        auto conn_ptr = DBPool::getInstance().getConnection();
        MYSQL* conn = conn_ptr.get();
        if (!conn) {
            LOG_ERROR("UserManager 预热失败: 无法连接数据库");
            return;
        }

        // 1. 加载基本信息与现金余额
        if (!mysql_query(conn, "SELECT id, username, balance FROM accounts")) {
            MYSQL_RES* res = mysql_store_result(conn);
            if (res) {
                MYSQL_ROW row;
                while ((row = mysql_fetch_row(res))) {
                    uint64_t uid = std::stoull(row[0]);
                    std::string username = row[1];
                    int64_t balance = std::stoll(row[2]);

                    auto account = std::make_unique<UserAccount>();
                    account->user_id = uid;
                    account->username = username;
                    account->available_balance = balance;

                    // 按 UID 分段存入
                    size_t idx = IdToMtx(uid);
                    mtx_[idx].find_account_[username] = account.get();
                    mtx_[idx].account_[uid] = std::move(account);
                }
                mysql_free_result(res);
            }
        }

        // 2. 加载股票持仓
        const char* sql = "SELECT a.id, h.symbol, h.quantity FROM holdings h JOIN accounts a ON h.username = a.username";
        if (!mysql_query(conn, sql)) {
            MYSQL_RES* res = mysql_store_result(conn);
            if (res) {
                MYSQL_ROW row;
                while ((row = mysql_fetch_row(res))) {
                    uint64_t uid = std::stoull(row[0]);
                    std::string symbol = row[1];
                    int quantity = std::stoi(row[2]);
                    
                    uint32_t ticker_id = TickerManager::getInstance().getTickerId(symbol);
                    if (ticker_id != 0) {
                        size_t idx = IdToMtx(uid);
                        if (mtx_[idx].account_.count(uid)) {
                            mtx_[idx].account_[uid]->available_stock[ticker_id] = quantity;
                        }
                    }
                }
                mysql_free_result(res);
            }
        }
        LOG_INFO("UserManager 内存预热完成，共加载 " + std::to_string(mtx_count) + " 分段的账户及资产记录");
    }

    void createUserAccount(uint64_t uid, const std::string& username) {
        size_t idx = IdToMtx(uid);
        std::unique_lock<std::shared_mutex> lock(mtx_[idx].mtx);
        if (mtx_[idx].account_.count(uid) == 0) {
            auto account = std::make_unique<UserAccount>();
            account->user_id = uid;
            account->username = username;
            mtx_[idx].find_account_[username] = account.get();
            mtx_[idx].account_[uid] = std::move(account);
        }
    }

    uint64_t getId(const std::string& username) {
        for (size_t i = 0; i < mtx_count; ++i) {
            std::shared_lock<std::shared_mutex> lock(mtx_[i].mtx);
            auto it = mtx_[i].find_account_.find(username);
            if (it != mtx_[i].find_account_.end()) {
                return it->second->user_id;
            }
        }
        return 0;
    }

    std::string getName(uint64_t uid) {
        size_t idx = IdToMtx(uid);
        std::shared_lock<std::shared_mutex> lock(mtx_[idx].mtx);
        auto it = mtx_[idx].account_.find(uid);
        return it != mtx_[idx].account_.end() ? it->second->username : "UNKNOWN";
    }

    bool tryFreezeBalance(uint64_t uid, int64_t amount) {
        UserAccount* acc = getAccount(uid);
        if (!acc) return false;
        std::lock_guard<std::mutex> lock(acc->mtx);
        if (acc->available_balance >= amount) {
            acc->available_balance -= amount;
            acc->frozen_balance += amount;
            return true;
        }
        return false;
    }

    bool tryFreezeStock(uint64_t uid, uint32_t ticker_id, int qty) {
        UserAccount* acc = getAccount(uid);
        if (!acc) return false;
        std::lock_guard<std::mutex> lock(acc->mtx);
        if (acc->available_stock[ticker_id] >= qty) {
            acc->available_stock[ticker_id] -= qty;
            acc->frozen_stock[ticker_id] += qty;
            return true;
        }
        return false;
    }

    // ================== 资产清算确认 (Confirm) ==================
    void commitBuy(uint64_t uid, uint32_t ticker_id, int64_t cost, int qty, int64_t refund_diff) {
        UserAccount* acc = getAccount(uid);
        if (!acc) return;
        std::lock_guard<std::mutex> lock(acc->mtx);
        acc->frozen_balance -= (cost + refund_diff);
        acc->available_balance += refund_diff;
        acc->available_stock[ticker_id] += qty;
    }

    void commitSell(uint64_t uid, uint32_t ticker_id, int64_t revenue, int qty) {
        UserAccount* acc = getAccount(uid);
        if (!acc) return;
        std::lock_guard<std::mutex> lock(acc->mtx);
        acc->frozen_stock[ticker_id] -= qty;
        acc->available_balance += revenue;
    }

    // ================== 资产撤回解冻 (Cancel) ==================
    void unfreezeBalance(uint64_t uid, int64_t amount) {
        UserAccount* acc = getAccount(uid);
        if (!acc) return;
        std::lock_guard<std::mutex> lock(acc->mtx);
        acc->frozen_balance -= amount;
        acc->available_balance += amount;
    }

    void unfreezeStock(uint64_t uid, uint32_t ticker_id, int qty) {
        UserAccount* acc = getAccount(uid);
        if (!acc) return;
        std::lock_guard<std::mutex> lock(acc->mtx);
        acc->frozen_stock[ticker_id] -= qty;
        acc->available_stock[ticker_id] += qty;
    }

    // ================== 辅助查询与更新 ==================
    void addBalance(uint64_t uid, int64_t amount) {
        UserAccount* acc = getAccount(uid);
        if (!acc) return;
        std::lock_guard<std::mutex> lock(acc->mtx);
        acc->available_balance += amount;
    }

    int64_t getBalance(uint64_t uid) {
        UserAccount* acc = getAccount(uid);
        if (!acc) return 0;
        std::lock_guard<std::mutex> lock(acc->mtx);
        return acc->available_balance;
    }

    std::unordered_map<uint32_t, int> getHoldings(uint64_t uid) {
        UserAccount* acc = getAccount(uid);
        if (!acc) return {};
        std::lock_guard<std::mutex> lock(acc->mtx);
        return acc->available_stock;
    }
};

#endif