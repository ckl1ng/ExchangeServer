#ifndef USER_REPOSITORY_IMPL_H
#define USER_REPOSITORY_IMPL_H

#include "Repositories.h"

/**
 * @class UserRepositoryImpl
 * @brief 用户数据仓库实现类
 * 
 * 封装了对数据库 accounts 表的所有操作，确保财务数据的强一致性。
 */
class UserRepositoryImpl : public IUserRepository {
public:
    /**
     * @brief 注册新用户并初始化账户
     * 
     * @param username 用户名
     * @param password 原始密码
     * @return true 注册成功，默认余额初始化为 0.00
     * @return false 注册失败（如用户名冲突或数据库故障）
     */
    bool registerUser(const std::string& username, const std::string& password) override;

    /**
     * @brief 登录身份校验
     * 
     * @param username 用户名
     * @param password 密码
     * @return true 匹配成功
     * @return false 用户不存在或密码错误
     */
    bool checkLogin(const std::string& username, const std::string& password) override;

    /**
     * @brief 获取用户的实时账户余额
     * 
     * @param username 目标用户名
     * @return double 当前余额。若查询出错返回 -1.0。
     */
    int64_t getBalance(const std::string& username) override;

    /**
     * @brief 增加用户余额（充值或卖出获利）
     * 
     * @param username 用户名
     * @param amount   增加的金额
     * @return true 更新成功
     * @return false 更新失败
     * 
     * @note 采用原子 SQL：`SET balance = balance + ?`，防止并发冲抵导致的坏账。
     */
    bool updateBalance(const std::string& username, int64_t amount) override;

    /**
     * @brief 扣减用户余额（买入扣款）
     * 
     * 核心财务安全函数，内置余额不足检查。
     * 
     * @param username 用户名
     * @param amount   扣减的金额
     * @return true 扣款成功
     * @return false 余额不足或操作失败
     * 
     * @note 关键 SQL：`WHERE balance >= ?` 确保账户不会透支。
     */
    bool deductBalance(const std::string& username, int64_t amount) override;

    uint64_t getUserId(const std::string& username) override;
};

#endif