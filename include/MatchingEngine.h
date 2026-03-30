#ifndef MATCHING_ENGINE_H
#define MATCHING_ENGINE_H

#include "Repositories.h"
#include "RedisPool.h"
#include "Logger.hpp"
#include <mutex>
#include <memory>
#include <string>
#include <algorithm>

struct Order {
    std::string username;
    std::string symbol;
    double price;
    int amount;
};


/**
 * @brief 执行内存订单撮合，并在成交后触发资产清算
 * 
 * 基于 Redis ZSET 维护的 L2 级订单簿进行深度扫描。支持部分成交、溢价退款及自动挂单。
 * 
 * @param newOrder 待处理的新订单引用，成交后其 amount 成员会动态扣减
 * @param isBuy 订单方向标识，true 为买入(Bid)，false 为卖出(Ask)
 * 
 * @note 
 * - 时间复杂度：平均 O(N * log M)，N 为吃单次数，M 为订单簿深度。
 * - 事务性：本函数通过 C++ 互斥锁实现串行撮合，确保内存状态与 Redis/DB 的强一致性。
 * 
 * @see IUserRepository::updateBalance, IStockRepository::updateStockHolding
 */
class MatchingEngine {
private:
    std::shared_ptr<IUserRepository> userRepo_;
    std::shared_ptr<IStockRepository> stockRepo_;
    std::unordered_map<std::string, std::shared_ptr<std::mutex>> symbol_lock_;
    std::mutex Order_mtx;

    /**
     * @brief 获取指定股票的专用锁
     */
    std::mutex& getSymbolLock(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(Order_mtx);
        // 如果该股票还没有对应的锁，则创建一个
        if (symbol_lock_.find(symbol) == symbol_lock_.end()) {
            symbol_lock_[symbol] = std::make_shared<std::mutex>();
        }
        return *symbol_lock_[symbol];
    }

public:
    MatchingEngine(std::shared_ptr<IUserRepository> userRepo, std::shared_ptr<IStockRepository> stockRepo): userRepo_(userRepo), stockRepo_(stockRepo) {}
    /**
     * @brief 执行订单撮合逻辑
     * 
     * 核心流程：
     * 1. 获取 Redis 连接并定位特定股票的买/卖盘 ZSET。
     * 2. 循环扫描对手盘（买家扫描卖盘，卖家扫描买盘）。
     * 3. 若价格匹配，计算成交量并调用 Repository 执行资金与股票的划转。
     * 4. 若无法完全成交，将剩余部分作为新挂单存入 Redis 订单簿。
     * 
     * @param newOrder 传入的新订单引用，成交后其 amount 会被原地修改为剩余未成交量。
     * @param isBuy    订单方向，true 表示买入方向 (Bid)，false 表示卖出方向 (Ask)。
     * 
     * @details 
     * **ZSET 结构设计**：
     * - Key: `buy_book:SYMBOL` / `sell_book:SYMBOL`
     * - Score: 价格 (Price)
     * - Member: `timestamp:username:amount` (确保相同价格下，先下单的排在前面)
     * 
     * @note 
     * 1. **线程安全**：通过 `Order_mtx` 确保同一时间只有一个线程在处理同一只股票的撮合。
     * 2. **原子性**：利用 Redis `ZREM` 命令的返回值判断成交是否有效，防止并发撮合冲突。
     * 3. **结算逻辑**：买单成交时，按【卖方挂单价】成交。若买方出价更高，差价会自动退回买方余额。
     */
    void matchOrders(Order& newOrder, bool isBuy);

    /**
     * @brief 成交后，将交易记录上传redis
     */
    void updateTrendInRedis(const std::string& symbol, double price);
};

#endif