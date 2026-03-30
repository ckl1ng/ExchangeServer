#include "StockRepositoryImpl.h"
#include "DBPool.h"
#include "Logger.hpp"

int StockRepositoryImpl::getStockHolding(const std::string& username, const std::string& symbol) {
    auto conn_ptr = DBPool::getInstance().getConnection();
    MYSQL* conn = conn_ptr.get();

    const char* sql = "SELECT quantity FROM holdings WHERE username=? AND symbol=?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return 0;

    if (mysql_stmt_prepare(stmt, sql, strlen(sql))) {
        mysql_stmt_close(stmt);
        return 0;
    }

    MYSQL_BIND input_bind[2];
    memset(input_bind, 0, sizeof(input_bind));
    
    input_bind[0].buffer_type = MYSQL_TYPE_STRING;
    input_bind[0].buffer = (char*)username.c_str();
    input_bind[0].buffer_length = username.length();

    input_bind[1].buffer_type = MYSQL_TYPE_STRING;
    input_bind[1].buffer = (char*)symbol.c_str();
    input_bind[1].buffer_length = symbol.length();

    if (mysql_stmt_bind_param(stmt, input_bind) || mysql_stmt_execute(stmt)) {
        mysql_stmt_close(stmt);
        return 0;
    }

    int quantity = 0;
    unsigned long length;
    bool is_null;
    bool error;

    MYSQL_BIND result_bind[1];
    memset(result_bind, 0, sizeof(result_bind));
    result_bind[0].buffer_type = MYSQL_TYPE_LONG;
    result_bind[0].buffer = (char*)&quantity;
    result_bind[0].is_null = &is_null;
    result_bind[0].length = &length;
    result_bind[0].error = &error;

    if (mysql_stmt_bind_result(stmt, result_bind)) {
        mysql_stmt_close(stmt);
        return 0;
    }

    int final_quantity = 0;
    if (mysql_stmt_fetch(stmt) == 0) {
        final_quantity = quantity;
    }

    mysql_stmt_close(stmt);
    return final_quantity;
}

bool StockRepositoryImpl::updateStockHolding(const std::string& username, const std::string& symbol, int delta) {
    auto conn_ptr = DBPool::getInstance().getConnection();
    MYSQL* conn = conn_ptr.get();

    std::string sql = "INSERT INTO holdings (username, symbol, quantity) VALUES (?, ?, ?) "
                      "ON DUPLICATE KEY UPDATE quantity = quantity + ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return false;

    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.size())) {
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)username.c_str();
    bind[0].buffer_length = username.length();

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)symbol.c_str();
    bind[1].buffer_length = symbol.length();

    bind[2].buffer_type = MYSQL_TYPE_LONG;
    bind[2].buffer = (char*)&delta;

    bind[3].buffer_type = MYSQL_TYPE_LONG;
    bind[3].buffer = (char*)&delta;
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    return success;
}

nlohmann::json StockRepositoryImpl::getAllHoldings(const std::string& username) {
    auto conn_ptr = DBPool::getInstance().getConnection();
    std::string sql = "SELECT symbol, quantity FROM holdings WHERE username = '" + username + "'";
    mysql_query(conn_ptr.get(), sql.c_str());
    MYSQL_RES* res = mysql_store_result(conn_ptr.get());
    nlohmann::json h_json = nlohmann::json::object();
    if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            // 安全防范：确保 row[0] 和 row[1] 有效
            if(row[0] != nullptr && row[1] != nullptr) {
                h_json[row[0]] = std::stoi(row[1]);
            }
        }
        mysql_free_result(res);
    }
    return h_json;
}

std::vector<std::string> StockRepositoryImpl::getAllStocks() {
    auto conn_ptr = DBPool::getInstance().getConnection();
    MYSQL* conn = conn_ptr.get();
    std::vector<std::string> symbols;

    const char* sql = "SELECT DISTINCT symbol FROM holdings";
    
    if (mysql_query(conn, sql) == 0) {
        MYSQL_RES* res = mysql_store_result(conn);
        if (res) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res))) {
                if (row[0] != nullptr) {
                    symbols.push_back(row[0]);
                }
            }
            mysql_free_result(res);
        }
    } else {
        LOG_ERROR("获取所有股票代码失败: " + std::string(mysql_error(conn)));
    }

    return symbols;
}

bool StockRepositoryImpl::deductStockHolding(const std::string& username, const std::string& symbol, int amount) {
    auto conn_ptr = DBPool::getInstance().getConnection();
    MYSQL* conn = conn_ptr.get();

    const char* sql = "UPDATE holdings SET quantity = quantity - ? "
                      "WHERE username = ? AND symbol = ? AND quantity >= ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return false;

    if (mysql_stmt_prepare(stmt, sql, strlen(sql))) {
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = (char*)&amount;

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)username.c_str();
    bind[1].buffer_length = username.length();

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)symbol.c_str();
    bind[2].buffer_length = symbol.length();

    bind[3].buffer_type = MYSQL_TYPE_LONG;
    bind[3].buffer = (char*)&amount;

    mysql_stmt_bind_param(stmt, bind);
    mysql_stmt_execute(stmt);

    // 检查是否有行数据被修改，若为 0 说明 WHERE 条件不满足（即 quantity 不足）
    my_ulonglong affected = mysql_stmt_affected_rows(stmt);
    mysql_stmt_close(stmt);

    return affected > 0;
}