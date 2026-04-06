#ifndef REPOSITORIES_H
#define REPOSITORIES_H

#include "json.hpp"
#include <vector>
#include <string>

/**
 * @class IUserRepository
 * @brief 用户账户数据访问接口类
 * 
 * 负责处理用户注册、登录鉴权、资金变更等核心财务操作。
 */
class IUserRepository {
public:
    virtual ~IUserRepository() = default;
    /**
     * @brief 注册新用户
     * 
     * @param username 用户名（唯一索引）
     * @param password 经过哈希处理或原始的密码
     * @return true 注册成功
     * @return false 注册失败（如用户名已存在）
     */
    virtual bool registerUser(const std::string& username, const std::string& password) = 0;
    /**
     * @brief 校验用户登录凭证
     * 
     * @param username 用户名
     * @param password 密码
     * @return true 验证通过
     * @return false 用户名不存在或密码错误
     */
    virtual bool checkLogin(const std::string& username, const std::string& password) = 0;
    /**
     * @brief 获取用户当前的现金余额
     * 
     * @param username 用户名
     * @return double 账户余额；若查询失败或用户不存在通常返回 -1.0 或 0.0
     */
    virtual int64_t getBalance(const std::string& username) = 0;
    /**
     * @brief 原子化增加用户余额（充值/卖出获利）
     * 
     * @param username 用户名
     * @param amount   增加的金额（必须大于0）
     * @return true 更新成功
     * @return false 数据库操作失败
     * @note 内部应使用 `UPDATE accounts SET balance = balance + ?` 确保并发安全。
     */
    virtual bool updateBalance(const std::string& username, int64_t amount) = 0;
    /**
     * @brief 原子化扣减用户余额（买入扣费）
     * 
     * @param username 用户名
     * @param amount   扣减的金额
     * @return true 扣减成功（余额充足且更新完成）
     * @return false 扣减失败（通常是余额不足或并发冲突）
     * @note 必须包含 `WHERE balance >= amount` 的约束条件防止账户出现负值。
     */
    virtual bool deductBalance(const std::string& username, int64_t amount) = 0;

    virtual uint64_t getUserId(const std::string& username) = 0;
};

/**
 * @class IStockRepository
 * @brief 股票持仓数据访问接口类
 * 
 * 维护用户与各股票代码之间的持有数量关系。
 */
class IStockRepository {
public:
    virtual ~IStockRepository() = default;
    /**
     * @brief 查询用户对特定股票的持仓数量
     * 
     * @param username 用户名
     * @param symbol   股票代码 (如 "AAPL")
     * @return int 持有股数
     */
    virtual int getStockHolding(const std::string& username, const std::string& symbol) = 0;
    /**
     * @brief 原子化更新持仓数量（增加/减少）
     * 
     * @param username 用户名
     * @param symbol   股票代码
     * @param delta    变动量（正数为增持，负数为减持）
     * @return true 更新成功
     * @return false 操作失败
     */
    virtual bool updateStockHolding(const std::string& username, const std::string& symbol, int delta) = 0;
    /**
     * @brief 获取用户名下的所有股票持仓列表
     * 
     * @param username 用户名
     * @return nlohmann::json 格式为 {"AAPL": 100, "TSLA": 50} 的 JSON 对象
     */
    virtual nlohmann::json getAllHoldings(const std::string& username) = 0;
    /**
     * @brief 获取系统中所有被持有的股票代码列表
     * 
     * @return std::vector<std::string> 唯一股票代码集合
     */
    virtual std::vector<std::string> getAllStocks() = 0;
    /**
     * @brief 原子化扣减持仓数量（卖出前校验）
     * 
     * @param username 用户名
     * @param symbol   股票代码
     * @param amount   扣减的数量
     * @return true 扣减成功（持仓充足）
     * @return false 扣减失败（持仓不足或 symbol 错误）
     * @note 必须包含 `WHERE quantity >= amount` 约束防止空头头寸。
     */
    virtual bool deductStockHolding(const std::string& username, const std::string& symbol, int amount) = 0;
};

#endif