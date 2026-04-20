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
    // 构造函数：初始化数据库连接池/资源
    bool registerUser(const std::string& username, const std::string& password) override;

    // 校验用户登录凭证
    bool checkLogin(const std::string& username, const std::string& password) override;

    // 获取用户余额
    int64_t getBalance(const std::string& username) override;

    // 原子化增加用户余额（充值/卖出获利）
    bool updateBalance(const std::string& username, int64_t amount) override;

    // 原子化扣减用户余额（买入扣费）
    bool deductBalance(const std::string& username, int64_t amount) override;

    // 获取用户 ID
    uint64_t getUserId(const std::string& username) override;
};

#endif