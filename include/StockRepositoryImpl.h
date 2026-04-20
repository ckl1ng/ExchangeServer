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

    //  查询用户对特定股票的持仓数量
    int getStockHolding(const std::string& username, const std::string& symbol) override ;

    //  原子化更新持仓数量（增加/减少）
    bool updateStockHolding(const std::string& username, const std::string& symbol, int delta) override ;

    //  获取用户名下的所有股票持仓列表
    nlohmann::json getAllHoldings(const std::string& username) override ;

    //  获取系统中所有被持有的股票代码列表
    std::vector<std::string> getAllStocks() override ;

    //  原子化扣减持仓数量（卖出前校验）
    bool deductStockHolding(const std::string& username, const std::string& symbol, int amount) override ;
};

#endif