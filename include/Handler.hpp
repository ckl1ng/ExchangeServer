#ifndef HANDLER_HPP
#define HANDLER_HPP

#include "Router.hpp"
#include "Repositories.h"
#include "Logger.hpp"
#include "Protocol.hpp"
#include "MatchingEngine.hpp"
#include "WALManager.hpp"
#include "RedisPool.h"
#include "UserManager.hpp"
#include "TickerManager.hpp"
#include "OrderIdGenerator.hpp"
#include <iomanip>
#include <sstream>
#include <random>
#include <chrono>
#include <atomic>
#include <unordered_map>
#include <algorithm>

inline uint32_t symbolToTickerId(const std::string& symbol) {
    uint32_t id = TickerManager::getInstance().getTickerId(symbol);
    return id != 0 ? id : 999;
}

inline std::string tickerIdToSymbol(uint32_t id) {
    return TickerManager::getInstance().getSymbol(id);
}

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

    std::string handle(Session* session, const char* req_data, uint32_t req_len) override {
        if (req_len < sizeof(AuthRequest)) return "";
        const AuthRequest* req = reinterpret_cast<const AuthRequest*>(req_data);
        std::string username = req->username;
        std::string password = req->password;

        AuthResponse res;
        if (userRepo_->checkLogin(username, password)) {
            LOG_INFO(username + ": 登录成功");
            session->username = username; //在会话中绑定身份

            uint64_t db_id = UserManager::getInstance().getId(username);
            if (db_id == 0) {
                db_id = userRepo_->getUserId(username);
                if (db_id != 0) UserManager::getInstance().createUserAccount(db_id, username);
            }
            session->user_id = db_id;
            res.status = 1;
            res.user_id = db_id;
        }
        else {
            LOG_INFO(username + " :登录失败");

            res.status = 0;
        }
        return std::string(reinterpret_cast<char*>(&res), sizeof(res));
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

    std::string handle(Session* session, const char* req_data, uint32_t req_len) override {
        if (req_len < sizeof(AuthRequest)) return "";
        const AuthRequest* req = reinterpret_cast<const AuthRequest*>(req_data);
        std::string username = req->username;
        std::string password = req->password;

        AuthResponse res;
        memset(&res, 0, sizeof(res));
        if (userRepo_->registerUser(username, password)) {
            // LOG_INFO(username + "注册成功");
            
            uint64_t new_id = userRepo_->getUserId(username);
            UserManager::getInstance().createUserAccount(new_id, username);
            session->username = username;
            session->user_id = new_id;
            res.status = 1;
            res.user_id = new_id;
        }
        else {
            // LOG_INFO(username + ": 注册失败（账号已存在）");
            res.status = 0;
        }
        return std::string(reinterpret_cast<char*>(&res), sizeof(res));
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

    std::string handle(Session* session, const char* req_data, uint32_t req_len) override {
        GenericResponse res;
        memset(&res, 0, sizeof(res));
        if (session->isLoggedIn()) {
            // LOG_INFO(session->username + ": 退出账号");
            session->username = ""; // 清空登录状态
            res.status = 1;
        }
        else {
            res.status = 0;
        }
        return std::string(reinterpret_cast<char*>(&res), sizeof(res));
    }
};

/**
 * @class GetBalanceHandler
 * @brief 余额查询处理器
 * @details 返回格式化的两位小数金额字符串。
 */
class GetBalanceHandler : public IHandler {
public:
    std::string handle(Session* session, const char* req_data, uint32_t req_len) override {
        BalanceResponse res;
        memset(&res, 0, sizeof(res));
        if (!session->isLoggedIn()) {
            res.status = 0;
            return std::string(reinterpret_cast<char*>(&res), sizeof(res));
        }
        // LOG_INFO("用户 " + session->username + " 查询余额");

        res.status = 1;
        res.balance = UserManager::getInstance().getBalance(session->user_id);
        return std::string(reinterpret_cast<char*>(&res), sizeof(res));
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

    std::string handle(Session* session, const char* req_data, uint32_t req_len) override {
        std::vector<std::string> stocks = stockRepo_->getAllStocks();
        
        StocksResponseHeader header;
        header.status = 1;
        header.count = stocks.size();

        std::string reply(reinterpret_cast<char*>(&header), sizeof(header));
        for (const auto& sym : stocks) {
            StockItem item;
            memset(&item, 0, sizeof(item));
            item.ticker_id = symbolToTickerId(sym);
            strncpy(item.symbol, sym.c_str(), sizeof(item.symbol) - 1);
            reply.append(reinterpret_cast<char*>(&item), sizeof(item));
        }
        return reply;
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

    std::string handle(Session* session, const char* req_data, uint32_t req_len) override {
        DepositResponse res;
        memset(&res, 0, sizeof(res));
        if (!session->isLoggedIn()) {
            res.status = 0;
            return std::string(reinterpret_cast<char*>(&res), sizeof(res));
        }

        if (req_len < sizeof(DepositRequest)) return "";
        const DepositRequest* req = reinterpret_cast<const DepositRequest*>(req_data);

        int64_t amount = req->amount;
        if (amount <= 0) {
            res.status = 0;
        }
        else {
            if (userRepo_->updateBalance(session->username, amount)) {
                // LOG_INFO(session->username + " 成功充值 " + std::to_string(amount));
                UserManager::getInstance().addBalance(session->user_id, amount);
                res.status = 1;
                res.new_balance = userRepo_->getBalance(session->username);
            } else {
                res.status = 0;
            }
        }
        return std::string(reinterpret_cast<char*>(&res), sizeof(res));
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
    std::shared_ptr<WALManager> wal_;

public:
    TradeHandler(std::shared_ptr<WALManager> wal) : wal_(wal) {}

    std::string handle(Session* session, const char* req_s, uint32_t req_len) override {
        TradeResponse res;
        memset(&res, 0, sizeof(res));
        if (!session->isLoggedIn()) {
            res.status = 0;
            return std::string(reinterpret_cast<char*>(&res), sizeof(res));
        }

        if (req_len < sizeof(TradeRequest)) return "";
        const TradeRequest* req = reinterpret_cast<const TradeRequest*>(req_s);

        auto order = std::make_shared<Order>();
        order->order_id = OrderIdGenerator::getInstance().generate();
        order->user_id = session->user_id;
        order->ticker_id = req->ticker_id;
        order->side = (req->side == 0) ? OrderSide::BUY : OrderSide::SELL;
        order->price = req->price;
        order->quantity = req->amount;
        order->timestamp = std::chrono::system_clock::now().time_since_epoch().count();

        if (order->side == OrderSide::BUY) {
            int64_t total_cost = req->price * req->amount;
            if (UserManager::getInstance().tryFreezeBalance(session->user_id, total_cost)) {
                wal_->appendOrder(order); 
                res.status = 1;
                // LOG_INFO("用户 " + session->username + " 已提交买单");
            } else {
                res.status = 0;
            }
        }
        else {
            if (UserManager::getInstance().tryFreezeStock(session->user_id, order->ticker_id, req->amount)) {
                wal_->appendOrder(order); 
                res.status = 1;
                // LOG_INFO("用户 " + session->username + " 已提交卖单");
            }
            else {
                res.status = 0;
            }
        }
        return std::string(reinterpret_cast<char*>(&res), sizeof(res));
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
    std::string handle(Session* session, const char* req_data, uint32_t req_len) override {
        HoldingsResponseHeader header;
        memset(&header, 0, sizeof(header));

        if (!session->isLoggedIn()) {
            header.status = 0;
            return std::string(reinterpret_cast<char*>(&header), sizeof(header));
        }

        auto holdings = UserManager::getInstance().getHoldings(session->user_id);
        std::vector<HoldingItem> items;
        for(const auto& [ticker_id, qty] : holdings) {
            if (qty > 0) {
                HoldingItem item;
                item.ticker_id = ticker_id;
                item.quantity = qty;
                items.push_back(item);
            }
        }

        header.status = 1;
        header.count = items.size();
        std::string reply(reinterpret_cast<char*>(&header), sizeof(header));
        if (!items.empty()) {
            reply.append(reinterpret_cast<char*>(items.data()), items.size() * sizeof(HoldingItem));
        }
        // LOG_INFO("用户 " + session->username + " 已检查持仓");
        return reply;
    }
};

/**
 * @class GetMarketHandler
 * @brief 市场行情处理器
 * @details 从 Redis 订单簿中获取指定股票的盘口第一档 (Best Bid & Offer)。
 */
class GetMarketHandler : public IHandler {
private:
    std::shared_ptr<MatchingEngine> engine_;

public:
    GetMarketHandler(std::shared_ptr<MatchingEngine> engine) : engine_(engine) {}

    std::string handle(Session* session, const char* req_data, uint32_t req_len) override {
        MarketResponse res;
        memset(&res, 0, sizeof(res));

        if (req_len < sizeof(MarketRequest)) return "";
        const MarketRequest* req = reinterpret_cast<const MarketRequest*>(req_data);

        uint32_t ticker_id = req->ticker_id;
        int64_t best_bid = 0, best_ask = 0;
        engine_->getBestBidOffer(ticker_id, best_bid, best_ask);

        res.status = 1;
        res.best_bid = best_bid;
        res.best_ask = best_ask;
        return std::string(reinterpret_cast<char*>(&res), sizeof(res));
    }
};

/**
 * @class GetNewsHandler
 * @brief 市场新闻处理器
 * @details 模拟外部新闻 API，返回随机的宏观市场快讯。
 */
class GetNewsHandler : public IHandler {
public:
    std::string handle(Session* session, const char* req_data, uint32_t req_len) override {
        NewsResponseHeader header;
        header.status = 1;

        struct NewsTemplate {
            std::string title;
            std::string type; 
            std::string symbol; 
        };

        std::vector<NewsTemplate> newsPool = {
            {"Apple (AAPL) 宣布将自研 AI 芯片整合至全线产品，市场反响剧烈", "Tech", "AAPL"},
            {"iPhone 16 供应链传出利好，印度组装效率大幅提升超出预期", "Tech", "AAPL"},
            {"App Store 服务业务收入创季度新高，AAPL 股价盘前上涨 2%", "Earnings", "AAPL"},
            {"Nvidia (NVDA) 发布 Blackwell 架构 GPU，AI 计算性能提升 5 倍", "Tech", "NVDA"},
            {"全球数据中心对 H100 需求依然强劲，NVDA 订单已排至 2025 年", "Macro", "NVDA"},
            {"分析师上调 NVDA 目标价至 $1000，称 AI 革命尚处于早期阶段", "Tech", "NVDA"},
            {"Tesla (TSLA) FSD V12 版本在北美全量推送，自动驾驶胜率提升", "Tech", "TSLA"},
            {"特斯拉上海超级工厂扩建项目获批，年产能有望突破 150 万辆", "Macro", "TSLA"},
            {"TSLA 储能业务 (Megapack) 利润率显著提升，成为新的增长引擎", "Earnings", "TSLA"},
            {"美联储 3 月会议纪要显示降息预期窗口可能延后，纳指承压", "Macro", "Global"},
            {"科技巨头集体增持 AI 基础设施，半导体板块出现避险资金流入", "Macro", "Global"},
            {"美债收益率小幅回落，成长型科技股吸引力增强", "Macro", "Global"}
        };

        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(newsPool.begin(), newsPool.end(), g);

        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        
        std::vector<NewsItem> items;
        for (int i = 0; i < 3; ++i) {
            NewsItem item;
            memset(&item, 0, sizeof(item));
            item.time = now - (i * 300);
            strncpy(item.title, newsPool[i].title.c_str(), sizeof(item.title) - 1);
            strncpy(item.type, newsPool[i].type.c_str(), sizeof(item.type) - 1);
            strncpy(item.symbol, newsPool[i].symbol.c_str(), sizeof(item.symbol) - 1);
            items.push_back(item);
        }

        header.count = items.size();
        std::string reply(reinterpret_cast<char*>(&header), sizeof(header));
        reply.append(reinterpret_cast<char*>(items.data()), items.size() * sizeof(NewsItem));
        
        // LOG_INFO("用户 " + session->username + " 查看新闻");
        return reply;
    }
};

/**
 * @class GetTrendHandler
 * @brief 价格走势处理器
 */
class GetTrendHandler : public IHandler {
public:
    std::string handle(Session* session, const char* req_data, uint32_t req_len) override {
        TrendResponseHeader header;
        memset(&header, 0, sizeof(header));

        if (req_len < sizeof(TrendRequest)) return "";
        const TrendRequest* req = reinterpret_cast<const TrendRequest*>(req_data);
        
        uint32_t ticker_id = req->ticker_id;
        
        auto conn_ptr = RedisPool::getInstance().getConnection();
        redisContext* conn = conn_ptr.get();
        std::string key = "history:trade:" + std::to_string(ticker_id);

        auto reply = make_reply(redisCommand(conn, "ZREVRANGE %s 0 9", key.c_str()));

        std::vector<uint64_t> prices;
        if (reply && reply->type == REDIS_REPLY_ARRAY) {
            for (size_t i = 0; i < reply->elements; i++) {
                try {
                    json j = json::parse(reply->element[i]->str);
                    prices.push_back(j["price"].get<uint64_t>());
                } catch(...) {}
            }
            std::reverse(prices.begin(), prices.end());
        }

        header.status = 1;
        header.count = prices.size();
        
        std::string reply_str(reinterpret_cast<char*>(&header), sizeof(header));
        if (!prices.empty()) {
            reply_str.append(reinterpret_cast<char*>(prices.data()), prices.size() * sizeof(uint64_t));
        }

        // LOG_INFO("用户 " + session->username + " 查看价格走势");
        return reply_str;
    }
};

/**
 * @class GetOrdersHandler
 * @brief 活跃订单查询处理器
 * @details 扫描 Redis 订单簿，筛选出属于当前用户的未成交挂单。
 */
class GetOrdersHandler : public IHandler {
private:
    std::shared_ptr<MatchingEngine> engine_;

public:
    GetOrdersHandler(std::shared_ptr<MatchingEngine> engine) : engine_(engine) {}

    std::string handle(Session* session, const char* req_data, uint32_t req_len) override {
        OrdersResponseHeader header;
        memset(&header, 0, sizeof(header));

        if (!session->isLoggedIn()) {
            header.status = 0;
            return std::string(reinterpret_cast<char*>(&header), sizeof(header));
        }
        
        std::vector<std::shared_ptr<Order>> active_orders = engine_->getUserActiveOrder(session->user_id);

        std::vector<OrderItem> items;
        for (const auto& o : active_orders) {
            OrderItem item;
            item.order_id = o->order_id;
            item.ticker_id = o->ticker_id;
            item.side = (o->side == OrderSide::BUY) ? 0 : 1;
            item.price = o->price;
            item.amount = o->quantity;
            items.push_back(item);
        }

        header.status = 1;
        header.count = items.size();

        std::string reply(reinterpret_cast<char*>(&header), sizeof(header));
        if (!items.empty()) {
            reply.append(reinterpret_cast<char*>(items.data()), items.size() * sizeof(OrderItem));
        }

        // LOG_INFO("用户 " + session->username + " 查看当前活跃订单");
        return reply;
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
    std::shared_ptr<WALManager> wal_;

public:
    CancelOrderHandler(std::shared_ptr<WALManager>wal) : wal_(wal) {}
    
    std::string handle(Session* session, const char* req_data, uint32_t req_len) override {
        GenericResponse res;
        memset(&res, 0, sizeof(res));

        if (!session->isLoggedIn()) {
            // LOG_INFO("用户未登录，无法查询");
            res.status = 0;
            return std::string(reinterpret_cast<char*>(&res), sizeof(res));
        }

        if (req_len < sizeof(CancelRequest)) return "";
        const CancelRequest* req = reinterpret_cast<const CancelRequest*>(req_data);

        wal_->appendCancel(req->ticker_id, req->order_id, session->user_id);
        res.status = 1;
        return std::string(reinterpret_cast<char*>(&res), sizeof(res));
    }
};

#endif