#ifndef HANDLER_HPP
#define HANDLER_HPP

#include "Router.hpp"
#include "Repositories.h"
#include "Logger.hpp"
#include "json.hpp"
#include "MatchingEngine.h"
#include "RedisPool.h"
#include <iomanip>
#include <sstream>
#include <random>
#include <chrono>
#include <algorithm>

/**
 * @class LoginHandler
 * @brief 用户登录处理器
 * @details 校验用户名密码，验证通过后在 Session 中记录状态，实现“有状态”的长连接。
 */
class LoginHandler : public IHandler {
private:
    std::shared_ptr<IUserRepository> userRepo_;

public:
    LoginHandler(std::shared_ptr<IUserRepository>repo) : userRepo_(repo) {}

    json handle(std::shared_ptr<Session> session, const json& req) override {
        std::string username = req["username"];
        std::string password = req["password"];

        json res;
        if (userRepo_->checkLogin(username, password)) {
            LOG_INFO(username + ": 登录成功");
            session->username = username; // 核心：在会话中绑定身份
            res["status"] = "true";
            res["msg"] = "登陆成功";
        }
        else {
            LOG_INFO(username + " :登录失败");
            res["status"] = "false";
            res["msg"] = "用户名不存在或密码错误";
        }
        return res;
    }
};

/**
 * @class RegisterUserHandler
 * @brief 用户注册处理器
 * @details 在数据库中创建新账号，默认余额初始化为 0。
 */
class RegisterUserHandler : public IHandler {
private:
    std::shared_ptr<IUserRepository> userRepo_;

public:
    RegisterUserHandler(std::shared_ptr<IUserRepository> repo) :userRepo_(repo) {}

    json handle(std::shared_ptr<Session> session, const json& req) override {
        std::string username = req["username"];
        std::string password = req["password"];

        json res;
        if (userRepo_->registerUser(username, password)) {
            LOG_INFO(username + "注册成功");
            session->username = username;
            res["status"] = "true";
            res["msg"] = "注册成功";
        }
        else {
            LOG_INFO(username + ": 注册失败（账号已存在）");
            res["status"] = "false";
            res["msg"] = "账号已存在";
        }
        return res;
    }
};

/**
 * @class ExitHandler
 * @brief 账号退出处理器
 * @details 清除 Session 中的用户信息，但不强制断开 TCP 连接。
 */
class ExitHandler : public IHandler {
private:
    std::shared_ptr<IUserRepository> userRepo_;

public:
    ExitHandler(std::shared_ptr<IUserRepository> repo) :userRepo_(repo) {}

    json handle(std::shared_ptr<Session> session, const json& req) override {
        json res;
        if (session->isLoggedIn()) {
            LOG_INFO(session->username + ": 退出账号");
            session->username = ""; // 清空登录状态
            res["status"] = "true";
            res["msg"] = "退出账号成功";
        }
        else {
            res["status"] = "false";
            res["msg"] = "无法退出，账号未登录";
        }
        return res;
    }
};

/**
 * @class GetBalanceHandler
 * @brief 余额查询处理器
 * @details 返回格式化的两位小数金额字符串。
 */
class GetBalanceHandler : public IHandler {
private:
    std::shared_ptr<IUserRepository> userRepo_;

public:
    GetBalanceHandler(std::shared_ptr<IUserRepository> repo) :userRepo_(repo) {}

    json handle(std::shared_ptr<Session> session, const json& req) override {
        json res;
        if (!session->isLoggedIn()) {
            res["status"] = "false";
            res["msg"] = "未登录，无法查询余额";
            return res;
        }
        LOG_INFO("用户 " + session->username + " 查询余额");

        double balance = userRepo_->getBalance(session->username);
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << balance; // 格式化为 0.00 格式

        res["status"] = "true";
        res["msg"] = ss.str();
        return res;
    }
};

/**
 * @class GetAllStocksHandler
 * @brief 市场标的查询处理器
 * @details 返回当前交易所内所有可交易的股票代码列表。
 */
class GetAllStocksHandler : public IHandler {
private:
    std::shared_ptr<IStockRepository> stockRepo_;

public:
    GetAllStocksHandler(std::shared_ptr<IStockRepository>stockRepo): stockRepo_(stockRepo){}

    json handle(std::shared_ptr<Session> session, const json& req) override {
        json res;
        res["status"] = "true";

        std::vector<std::string> stocks = stockRepo_->getAllStocks();
        res["stocks"] = stocks;
        res["msg"] = "获取市场可用股票列表成功";
        return res;
    }
};

/**
 * @class DepositHandler
 * @brief 资金充值处理器
 * @details 执行余额更新并返回充值后的最新余额。
 */
class DepositHandler : public IHandler {
private:
    std::shared_ptr<IUserRepository> userRepo_;

public:
    DepositHandler(std::shared_ptr<IUserRepository> repo) :userRepo_(repo) {}

    json handle(std::shared_ptr<Session> session, const json& req) override {
        json res;
        if (!session->isLoggedIn()) {
            res["status"] = "false";
            res["msg"] = "未登录";
            return res;
        }

        double amount = req["amount"];
        if (amount <= 0) {
            res["status"] = "false";
            res["msg"] = "充值金额必须大于零";
        }
        else {
            if (userRepo_->updateBalance(session->username, amount)) {
                LOG_INFO(session->username + " 成功充值 " + std::to_string(amount));
                res["status"] = "true";
                res["msg"] = "充值成功";

                std::stringstream ss;
                ss << std::fixed << std::setprecision(2) << userRepo_->getBalance(session->username);
                res["new_balance"] = ss.str();
            } else {
                res["status"] = "false";
                res["msg"] = "数据库操作失败";
            }
        }
        return res;
    }
};

/**
 * @class TradeHandler
 * @brief 买卖交易执行处理器
 * @details 
 * 1. 验证登录。
 * 2. 预扣除资产（买入扣钱，卖出扣股票）以防止超卖/透支。
 * 3. 调用撮合引擎进行订单匹配。
 */
class TradeHandler : public IHandler {
private:
    std::shared_ptr<IUserRepository> userRepo_;
    std::shared_ptr<IStockRepository> stockRepo_;
    std::shared_ptr<MatchingEngine> engine_;

public:
    TradeHandler(std::shared_ptr<IUserRepository> userRepo, std::shared_ptr<IStockRepository> stockRepo, std::shared_ptr<MatchingEngine> engine): userRepo_(userRepo), stockRepo_(stockRepo), engine_(engine) {}

    json handle(std::shared_ptr<Session> session, const json& req) override {
        json res;
        if (!session->isLoggedIn()) {
            res["status"] = "false";
            res["msg"] = "未登录";
            return res;
        }

        std::string action = req["action"]; // "buy" 或 "sell"
        std::string symbol = req["symbol"];
        double price = req["price"];
        int amount = req["amount"];

        if (action == "buy") {
            double total_cost = price * amount;
            // 先从数据库扣钱
            if (userRepo_->deductBalance(session->username, total_cost)) {
                Order o = {session->username, symbol, price, amount};
                engine_->matchOrders(o, true); // 送去撮合
                res["status"] = "true";
                LOG_INFO("用户 " + session->username + " 已提交买单");
            } else {
                res["status"] = "false";
                res["msg"] = "余额不足或下单失败";
            }
        }
        else {
            // 先从数据库扣持仓
            if (stockRepo_->deductStockHolding(session->username, symbol, amount)) {
                Order o = {session->username, symbol, price, amount};
                engine_->matchOrders(o, false); // 送去撮合
                res["status"] = "true";
                res["msg"] = "卖出订单已接收";
                LOG_INFO("用户 " + session->username + " 已提交卖单");
            }
            else {
                res["status"] = "false";
                res["msg"] = "该股票持仓不足";
            }
        }
        return res;
    }
};

/**
 * @class GetHoldingsHandler
 * @brief 持仓查询处理器
 * @details 返回用户持有的所有股票及其对应的数量。
 */
class GetHoldingsHandler : public IHandler {
private:
    std::shared_ptr<IStockRepository> stockRepo_;
public:
    GetHoldingsHandler(std::shared_ptr<IStockRepository> stockRepo) : stockRepo_(stockRepo) {}
    json handle(std::shared_ptr<Session> session, const json& req) override {
        json res;
        if (!session->isLoggedIn()) {
            res["status"] = "false";
            res["msg"] = "未登录，无法查询";
            return res;
        }
        LOG_INFO("用户 " + session->username + " 已检查持仓");
        res["status"] = "true";
        res["data"] = stockRepo_->getAllHoldings(session->username);
        return res;
    }
};

/**
 * @class GetMarketHandler
 * @brief 市场行情处理器
 * @details 从 Redis 订单簿中获取指定股票的盘口第一档 (Best Bid & Offer)。
 */
class GetMarketHandler : public IHandler {
public:
    json handle(std::shared_ptr<Session> session, const json& req) override {
        json res;
        std::string symbol = req["symbol"];
        auto conn_ptr = RedisPool::getInstance().getConnection();
        redisContext* conn = conn_ptr.get();

        std::string buy_key = "buy_book:" + symbol;
        std::string sell_key = "sell_book:" + symbol;

        json data;
        // ZREVRANGE 获取最高买价 (Bid)
        redisReply* buy_reply = (redisReply*)redisCommand(conn, "ZREVRANGE %s 0 0 WITHSCORES", buy_key.c_str());
        if (buy_reply && buy_reply->type == REDIS_REPLY_ARRAY && buy_reply->elements >= 2) {
            data["bid"] = std::stod(buy_reply->element[1]->str);
        } else {
            data["bid"] = 0.0;
        }
        if (buy_reply) freeReplyObject(buy_reply);

        // ZRANGE 获取最低卖价 (Ask)
        redisReply* sell_reply = (redisReply*)redisCommand(conn, "ZRANGE %s 0 0 WITHSCORES", sell_key.c_str());
        if (sell_reply && sell_reply->type == REDIS_REPLY_ARRAY && sell_reply->elements >= 2) {
            data["ask"] = std::stod(sell_reply->element[1]->str);
        } else {
            data["ask"] = 0.0;
        }
        if (sell_reply) freeReplyObject(sell_reply);

        LOG_INFO("用户 " + session->username + " 查看 " + symbol + " 股票行情");
        res["status"] = "true";
        res["data"] = data;
        return res;
    }
};

/**
 * @class GetNewsHandler
 * @brief 市场新闻处理器
 * @details 模拟外部新闻 API，返回随机的宏观市场快讯。
 */
class GetNewsHandler : public IHandler {
public:
    json handle(std::shared_ptr<Session> session, const json& req) override {
        json res;
        res["status"] = "true";

        // 模拟新闻池
        struct NewsTemplate {
            std::string title;
            std::string type; // "Macro", "Tech", "Earnings"
            std::string symbol; // 相关公司
        };

        std::vector<NewsTemplate> newsPool = {
            // AAPL 专用新闻
            {"Apple (AAPL) 宣布将自研 AI 芯片整合至全线产品，市场反响剧烈", "Tech", "AAPL"},
            {"iPhone 16 供应链传出利好，印度组装效率大幅提升超出预期", "Tech", "AAPL"},
            {"App Store 服务业务收入创季度新高，AAPL 股价盘前上涨 2%", "Earnings", "AAPL"},
            
            // NVDA 专用新闻
            {"Nvidia (NVDA) 发布 Blackwell 架构 GPU，AI 计算性能提升 5 倍", "Tech", "NVDA"},
            {"全球数据中心对 H100 需求依然强劲，NVDA 订单已排至 2025 年", "Macro", "NVDA"},
            {"分析师上调 NVDA 目标价至 $1000，称 AI 革命尚处于早期阶段", "Tech", "NVDA"},
            
            // TSLA 专用新闻
            {"Tesla (TSLA) FSD V12 版本在北美全量推送，自动驾驶胜率提升", "Tech", "TSLA"},
            {"特斯拉上海超级工厂扩建项目获批，年产能有望突破 150 万辆", "Macro", "TSLA"},
            {"TSLA 储能业务 (Megapack) 利润率显著提升，成为新的增长引擎", "Earnings", "TSLA"},
            
            // 宏观新闻
            {"美联储 3 月会议纪要显示降息预期窗口可能延后，纳指承压", "Macro", "Global"},
            {"科技巨头集体增持 AI 基础设施，半导体板块出现避险资金流入", "Macro", "Global"},
            {"美债收益率小幅回落，成长型科技股（AAPL, NVDA）吸引力增强", "Macro", "Global"}
        };

        // 随机打乱并抽取 3 条
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(newsPool.begin(), newsPool.end(), g);

        json newsList = json::array();
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

        for (int i = 0; i < 3; ++i) {
            json item;
            item["title"] = newsPool[i].title;
            item["type"] = newsPool[i].type;
            item["symbol"] = newsPool[i].symbol;
            item["time"] = now - (i * 300); // 模拟每隔5分钟一条
            
            newsList.push_back(item);
        }

        LOG_INFO("用户 " + session->username + " 查看新闻");
        res["news"] = newsList;
        res["total_count"] = 3;
        return res;
    }
};

/**
 * @class GetTrendHandler
 * @brief 价格走势处理器
 */
class GetTrendHandler : public IHandler {
public:
    json handle(std::shared_ptr<Session> session, const json& req) override {
        json res;
        std::string symbol = req.value("symbol", "AAPL");
        
        auto conn_ptr = RedisPool::getInstance().getConnection();
        redisContext* conn = conn_ptr.get();
        std::string key = "trend:" + symbol;

        auto reply = make_reply(redisCommand(conn, "LRANGE %s 0 9", key.c_str()));

        json prices = json::array();
        if (reply && reply->type == REDIS_REPLY_ARRAY) {
            for (size_t i = 0; i < reply->elements; i++) {
                prices.push_back(std::stod(reply->element[i]->str));
            }
            std::reverse(prices.begin(), prices.end());
        }

        if (prices.empty()) {
            res["status"] = "true";
            res["trend"] = {0.0};
            res["msg"] = "暂无成交记录";
        }
        else {
            res["status"] = "true";
            res["symbol"] = symbol;
            res["trend"] = prices;
            res["msg"] = "获取最近成交趋势成功";
        }

        LOG_INFO("用户 " + session->username + " 查看价格走势");
        return res;
    }
};

/**
 * @class GetOrdersHandler
 * @brief 活跃订单查询处理器
 * @details 扫描 Redis 订单簿，筛选出属于当前用户的未成交挂单。
 */
class GetOrdersHandler : public IHandler {
public:
    json handle(std::shared_ptr<Session> session, const json& req) override {
        json res;
        if (!session->isLoggedIn()) {
            res["status"] = "false";
            res["msg"] = "未登录，无法查询";
            return res;
        }

        auto conn_ptr = RedisPool::getInstance().getConnection();
        redisContext* conn = conn_ptr.get();
        json orders = json::array();

        // 内部 Lambda：扫描 Redis Key 并过滤用户订单
        auto fetchOrder = [&](const std::string& prefix, const std::string& type) {
            auto reply = make_reply(redisCommand(conn, "KEYS %s*", prefix.c_str()));
            if (reply && reply->type == REDIS_REPLY_ARRAY) {
                for (size_t i = 0; i < reply->elements; i++) {
                    std::string key = reply->element[i]->str;
                    std::string symbol = key.substr(prefix.length() + 1); // 修正前缀长度

                    auto zrep = make_reply(redisCommand(conn, "ZRANGE %s 0 -1 WITHSCORES", key.c_str()));
                    
                    if (zrep && zrep->type == REDIS_REPLY_ARRAY) {
                        for (size_t j = 0; j < zrep->elements; j+=2) {
                            std::string member = zrep->element[j]->str;
                            double price = std::stod(zrep->element[j+1]->str);

                            // 解析 member 格式: "ts:user:amount"
                            size_t p1 = member.find(':');
                            size_t p2 = member.find(':', p1 + 1);
                            if (p1 != std::string::npos && p2 !=std::string::npos) {
                                std::string order_user = member.substr(p1 + 1, p2 - p1 - 1);

                                if (order_user == session->username) {
                                    int amount = std::stoi(member.substr(p2 + 1));
                                    json order;
                                    order["symbol"] = symbol;
                                    order["type"] = type;
                                    order["price"] = price;
                                    order["amount"] = amount;
                                    order["order_id"] = member;
                                    orders.push_back(order);
                                }
                            }
                        }
                    }
                }
            }
        };

        fetchOrder("buy_book", "buy");
        fetchOrder("sell_book", "sell");

        res["status"] = "true";
        res["data"] = orders;
        LOG_INFO("用户 " + session->username + " 查看当前活跃订单");
        return res;
    }
};

/**
 * @class CancelOrderHandler
 * @brief 订单撤销处理器
 * 
 * 负责处理用户的撤单请求，内置金融级的高并发防作弊机制与安全性校验：
 * - 越权拦截 (Auth Check): 解析 order_id 提取挂单用户名，严格比对当前 Session 用户。
 * - 防价格欺诈 (Anti-Fraud): 通过 Redis ZSCORE 实时查询订单真实价格，杜绝退款漏洞。
 * - 并发防刷 (Concurrency Safe): 利用 Redis ZREM 命令的原子性判断撤单与成交的竞争。
 * - 资产回退 (Asset Rollback): 撤单成功后原子化退回冻结的现金或股票。
 */
class CancelOrderHandler : public IHandler {
private:
    std::shared_ptr<IUserRepository> userRepo_;
    std::shared_ptr<IStockRepository> stockRepo_;

public:
    CancelOrderHandler(std::shared_ptr<IUserRepository> userRepo, std::shared_ptr<IStockRepository> stockRepo): userRepo_(userRepo), stockRepo_(stockRepo) {}

    json handle(std::shared_ptr<Session> session, const json& req) override {
        json res;
        if (!session->isLoggedIn()) {
            res["status"] = "false";
            res["msg"] = "未登录";
            return res;
        }

        std::string symbol = req["symbol"];
        std::string type = req["type"];
        std::string order_id = req["order_id"];

        // 1. 基础格式解析
        size_t p1 = order_id.find(':');
        size_t p2 = order_id.find(':', p1 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos) {
            res["status"] = "false";
            res["msg"] = "无效的订单格式";
            return res;
        }

        // 2. 越权校验：防止 A 撤销 B 的订单
        std::string order_user = order_id.substr(p1 + 1, p2 - p1 - 1);
        if (order_user != session->username) {
            res["status"] = "false";
            res["msg"] = "您无法删除他人订单";
            return res;
        }

        int amount = std::stoi(order_id.substr(p2 + 1));
        std::string key = (type == "buy" ? "buy_book:" : "sell_book:") + symbol;

        auto conn_ptr = RedisPool::getInstance().getConnection();
        redisContext* conn = conn_ptr.get();

        // 3. 价格校验：确保退款金额正确
        auto zscore_rep = make_reply(redisCommand(conn, "ZSCORE %s %s", key.c_str(), order_id.c_str()));
        if (!zscore_rep || zscore_rep->type != REDIS_REPLY_STRING) {
            res["status"] = "false";
            res["msg"] = "撤单失败，订单不存在或已成交";
            return res;
        }

        double real_price = std::stod(zscore_rep->str);

        // 4. 原子撤销：ZREM 返回 1 表示撤销成功，返回 0 表示已被撮合
        auto rm_reply = make_reply(redisCommand(conn, "ZREM %s %s", key.c_str(), order_id.c_str()));

        if (rm_reply && rm_reply->type == REDIS_REPLY_INTEGER && rm_reply->integer == 1) {
            // 资产退回
            if (type == "buy") {
                userRepo_->updateBalance(session->username, real_price * amount);
            }
            else if (type == "sell") {  
                stockRepo_->updateStockHolding(session->username, symbol, amount);
            }
            LOG_INFO(session->username + " 撤单成功: " + order_id);
            res["status"] = "true";
            res["msg"] = "撤单成功，相应资产已退回";
        }
        else {
            res["status"] = "false";
            res["msg"] = "撤单失败，订单已瞬间成交";
        }
        LOG_INFO("用户 " + session->username + " 申请撤单");

        return res;
    }
};

#endif