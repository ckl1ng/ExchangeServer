#ifndef STOCK_REPOSITORY_IMPL_H
#define STOCK_REPOSITORY_IMPL_H

#include "Repositories.h"

/**
 * @class StockRepositoryImpl
 * @brief 股票持仓数据仓库实现类
 * 
 * 封装了对数据库 holdings 表的所有 CRUD 操作。
 */
class StockRepositoryImpl : public IStockRepository {
public:
    /**
     * @brief 查询指定用户对特定股票的持有数量
     * 
     * @param username 用户名
     * @param symbol   股票代码
     * @return int     持有股数；若无持仓或查询失败则返回 0
     * 
     * @details 内部执行：`SELECT quantity FROM holdings WHERE username=? AND symbol=?`
     */
    int getStockHolding(const std::string& username, const std::string& symbol) override ;

    /**
     * @brief 原子化更新用户持仓（增减持）
     * 
     * 采用 "UPSERT" 逻辑：若记录不存在则插入，若存在则在原基础上累加。
     * 
     * @param username 用户名
     * @param symbol   股票代码
     * @param delta    变动数量（正数为买入增加，负数为卖出减少）
     * @return true    更新成功
     * @return false   数据库执行异常
     * 
     * @note 
     * 使用 `ON DUPLICATE KEY UPDATE` 语句，保证在高并发撮合下，
     * 持仓数量的加减是行级锁定的原子操作。
     */
    bool updateStockHolding(const std::string& username, const std::string& symbol, int delta) override ;

    /**
     * @brief 获取用户名下的完整持仓清单
     * 
     * @param username 用户名
     * @return nlohmann::json 返回格式：{"AAPL": 100, "TSLA": 50, ...}
     * 
     * @details 
     * 该函数遍历结果集并构建 JSON 对象，方便直接通过网络返回给客户端。
     * 内部包含对空指针 (row[i] == nullptr) 的安全校验。
     */
    nlohmann::json getAllHoldings(const std::string& username) override ;

    /**
     * @brief 检索系统中当前所有已上市/被持有的股票代码
     * 
     * @return std::vector<std::string> 唯一股票代码集合
     * 
     * @details 
     * 执行 `SELECT DISTINCT symbol FROM holdings`，通常用于行情初始化。
     */
    std::vector<std::string> getAllStocks() override ;

    /**
     * @brief 安全扣减用户持仓（卖出前置检查）
     * 
     * 确保扣减后持仓不会出现负数，是交易系统防“超卖”的核心逻辑。
     * 
     * @param username 用户名
     * @param symbol   股票代码
     * @param amount   拟卖出/扣减的数量
     * @return true    扣减成功，持仓已减少
     * @return false   扣减失败（通常因为余额不足或记录不存在）
     * 
     * @note 
     * SQL 关键约束：`WHERE ... AND quantity >= ?`。
     * 只有当受影响行数 (Affected Rows) 大于 0 时，才判定为成功。
     */
    bool deductStockHolding(const std::string& username, const std::string& symbol, int amount) override ;
};

#endif