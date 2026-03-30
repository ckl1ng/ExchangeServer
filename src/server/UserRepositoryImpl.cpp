#include "UserRepositoryImpl.h"
#include "DBPool.h"
#include "Logger.hpp"
#include <cstring>
#include <iostream>

bool UserRepositoryImpl::registerUser(const std::string& username, const std::string& password) {
    auto conn_ptr = DBPool::getInstance().getConnection();
    MYSQL* conn = conn_ptr.get();

    const char* sql = "INSERT INTO accounts (username, password, balance) VALUES (?, ?, 0.00)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return false;

    if (mysql_stmt_prepare(stmt, sql, strlen(sql))) {
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));

    // 绑定 username
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)username.c_str();
    bind[0].buffer_length = username.length();

    // 绑定 password
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)password.c_str();
    bind[1].buffer_length = password.length();

    if (mysql_stmt_bind_param(stmt, bind)) {
        mysql_stmt_close(stmt);
        return false;
    }

    bool success = (mysql_stmt_execute(stmt) == 0);
    
    if (!success) {
        std::cerr << "注册失败: " << mysql_stmt_error(stmt) << std::endl;
    }

    mysql_stmt_close(stmt);
    return success;
}

bool UserRepositoryImpl::checkLogin(const std::string& username, const std::string& password) {
    auto conn_ptr = DBPool::getInstance().getConnection();
    MYSQL* conn = conn_ptr.get();

    std::string sql = "SELECT * FROM accounts WHERE username=? AND password=?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return false;

    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.size())) {
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)username.c_str();
    bind[0].buffer_length = username.length();

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)password.c_str();
    bind[1].buffer_length = password.length();

    mysql_stmt_bind_param(stmt, bind);
    mysql_stmt_execute(stmt);

    mysql_stmt_store_result(stmt);
    bool exists = (mysql_stmt_num_rows(stmt) > 0);

    mysql_stmt_close(stmt);
    return exists;
}

double UserRepositoryImpl::getBalance(const std::string& username) {
    auto conn_ptr = DBPool::getInstance().getConnection();
    MYSQL* conn = conn_ptr.get();

    const char* sql = "SELECT balance FROM accounts WHERE username=?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return -1.0;

    if (mysql_stmt_prepare(stmt, sql, strlen(sql))) {
        mysql_stmt_close(stmt);
        return -1.0;
    }

    // 绑定输入参数
    MYSQL_BIND input_bind[1];
    memset(input_bind, 0, sizeof(input_bind));
    input_bind[0].buffer_type = MYSQL_TYPE_STRING;
    input_bind[0].buffer = (char*)username.c_str();
    input_bind[0].buffer_length = username.length();

    mysql_stmt_bind_param(stmt, input_bind);
    mysql_stmt_execute(stmt);

    // 绑定输出结果
    double balance = 0.0;
    unsigned long length;
    bool is_null;
    bool error;
    
    MYSQL_BIND result_bind[1];
    memset(result_bind, 0, sizeof(result_bind));
    result_bind[0].buffer_type = MYSQL_TYPE_DOUBLE; // 假设数据库中是 double/decimal
    result_bind[0].buffer = (char*)&balance;
    result_bind[0].is_null = &is_null;
    result_bind[0].length = &length;
    result_bind[0].error = &error;

    mysql_stmt_bind_result(stmt, result_bind);
    
    double final_balance = -1.0;
    if (mysql_stmt_fetch(stmt) == 0) {
        final_balance = balance;
    }

    mysql_stmt_close(stmt);
    return final_balance;
}

bool UserRepositoryImpl::updateBalance(const std::string& username, double amount) {
    auto conn_ptr = DBPool::getInstance().getConnection();
    MYSQL* conn = conn_ptr.get();
    
    // 使用 SQL 的原子加法防止并发冲抵
    std::string sql = "UPDATE accounts SET balance = balance + ? WHERE username = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return false;

    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.size())) {
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_DOUBLE;
    bind[0].buffer = (char*)&amount;
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)username.c_str();
    bind[1].buffer_length = username.length();

    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    return success;
}

bool UserRepositoryImpl::deductBalance(const std::string& username, double amount) {
    auto conn_ptr = DBPool::getInstance().getConnection();
    MYSQL* conn = conn_ptr.get();
    
    // 使用带有 WHERE 条件的原子 UPDATE 语句
    // 只有当 balance >= amount 时，才会更新成功（受影响行数为 1）
    std::string sql = "UPDATE accounts SET balance = balance - ? WHERE username = ? AND balance >= ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return false;

    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.size())) {
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_DOUBLE;
    bind[0].buffer = (char*)&amount;
    
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)username.c_str();
    bind[1].buffer_length = username.length();

    bind[2].buffer_type = MYSQL_TYPE_DOUBLE;
    bind[2].buffer = (char*)&amount;

    mysql_stmt_bind_param(stmt, bind);
    mysql_stmt_execute(stmt);

    my_ulonglong affected_rows = mysql_stmt_affected_rows(stmt);
    mysql_stmt_close(stmt);

    return affected_rows > 0;
}
