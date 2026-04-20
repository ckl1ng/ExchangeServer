#ifndef TICKER_MANAGER_HPP
#define TICKER_MANAGER_HPP

#include "DBPool.h"
#include "Logger.hpp"
#include <unordered_map>
#include <string>
#include <mutex>

/**
 * @class TickerManager
 * @brief 证券代码管理器
 * 
 * 负责在内存中维护证券代码与 ID 之间的映射关系，并支持从数据库加载数据。
 */
class TickerManager {
private:
    std::unordered_map<std::string, uint32_t> symbol_to_id_;
    std::unordered_map<uint32_t, std::string> id_to_symbol_;

    TickerManager() = default;

public:
    static TickerManager& getInstance() {
        static TickerManager instance;
        return instance;
    }

    // 禁止复制和移动构造函数
    TickerManager(const TickerManager&) = delete;
    
    TickerManager& operator=(const TickerManager&) = delete;

    // 从数据库加载所有证券代码和对应的 ID
    void loadAllTickersFromDB() {
        auto conn_ptr = DBPool::getInstance().getConnection();
        MYSQL* conn = conn_ptr.get();

        const char* sql = "SELECT id, symbol FROM stocks";
        if (mysql_query(conn, sql)) {
            LOG_ERROR("加载股票列表失败: " + std::string(mysql_error(conn)));
            return;
        }

        MYSQL_RES* res = mysql_store_result(conn);
        if (!res) return;

        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            uint32_t id = std::stoul(row[0]);
            std::string symbol = row[1];
            symbol_to_id_[symbol] = id;
            id_to_symbol_[id] = symbol;
        }
        mysql_free_result(res);
        // LOG_INFO("股票标加载完成，共 " + std::to_string(symbol_to_id_.size()) + " 只股票");
    }

    // 根据证券代码获取对应的 ID
    uint32_t getTickerId(const std::string& symbol) {
        auto it = symbol_to_id_.find(symbol);
        return it != symbol_to_id_.end() ? it->second : 0;
    }

    // 根据 ID 获取对应的证券代码
    std::string getSymbol(uint32_t ticker_id) {
        auto it = id_to_symbol_.find(ticker_id);
        return it != id_to_symbol_.end() ? it->second : "UNKNOWN";
    }

    // 获取所有证券代码的 ID 列表
    std::vector<uint32_t> getAllTickerIds() {
        std::vector<uint32_t> ids;
        for (auto& p : id_to_symbol_) ids.push_back(p.first);
        return ids;
    }
};

#endif