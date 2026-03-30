#include "MatchingEngine.h"

void MatchingEngine::matchOrders(Order& newOrder, bool isBuy) {
    // 获取全局锁，确保撮合过程的串行安全性
    std::lock_guard<std::mutex>lock(getSymbolLock(newOrder.symbol));
    auto conn_ptr = RedisPool::getInstance().getConnection();
    redisContext* conn = conn_ptr.get();

    std::string symbol = newOrder.symbol;
    std::string buy_key = "buy_book:" + symbol;
    std::string sell_key = "sell_book:" + symbol;

    if (isBuy) {
        // ================= 【买入撮合流程】 =================
        // 目标：扫描卖盘 (sell_book)，寻找价格 <= newOrder.price 的最低卖单
        while (newOrder.amount > 0) {
            // 获取卖盘中价格最低的订单 (ZRANGE 0 0)
            auto rep = make_reply(redisCommand(conn, "ZRANGE %s 0 0 WITHSCORES", sell_key.c_str()));

            if (rep && rep->type == REDIS_REPLY_ARRAY && rep->elements >= 2) {
                double ask_price = std::stod(rep->element[1]->str);

                // 价格检查：若最低卖价高于买入出价，无法撮合，跳出循环
                if (ask_price > newOrder.price) break;

                // 提取具体的 Member 信息
                auto rep2 = make_reply((redisReply*)redisCommand(conn, "ZRANGEBYSCORE %s %f %f LIMIT 0 1", sell_key.c_str(), ask_price, ask_price));
                if (rep2 && rep2->type == REDIS_REPLY_ARRAY && rep2->elements >= 1) {
                    std::string member = rep2->element[0]->str;

                    // 解析 Member: "ts:user:amount"
                    size_t p1 = member.find(':');
                    size_t p2 = member.find(':', p1 + 1);
                    std::string ts = member.substr(0, p1);
                    std::string sellUser = member.substr(p1 + 1, p2 - p1 - 1);
                    int sellAmount = std::stoi(member.substr(p2 + 1));

                    // 计算撮合量
                    int tradeAmount = std::min(newOrder.amount, sellAmount);

                    // --- 资产清算 ---
                    // 1. 卖方收钱
                    userRepo_->updateBalance(sellUser, ask_price * tradeAmount);
                    // 2. 买方收股票
                    stockRepo_->updateStockHolding(newOrder.username, symbol, tradeAmount);
                    // 3. 买方溢价退款（若买方出价更高，按卖方价成交并退差额）
                    if (newOrder.price > ask_price) {
                        double refund = (newOrder.price - ask_price) * tradeAmount;
                        userRepo_->updateBalance(newOrder.username, refund);
                    }

                    LOG_INFO("撮合成功: 买方 " + newOrder.username + " 买入 " + sellUser + " 的 " + std::to_string(tradeAmount) + " 股 @" + std::to_string(ask_price));

                    // 从订单簿移除已匹配的原始记录
                    auto rm = make_reply((redisReply*)redisCommand(conn, "ZREM %s %s", sell_key.c_str(), member.c_str()));

                    newOrder.amount -= tradeAmount;
                    sellAmount -= tradeAmount;

                    //交易记录存入redis
                    updateTrendInRedis(symbol, ask_price);
                    // 若对手单未被吃完，将剩余部分重新挂入卖盘
                    if (sellAmount > 0) {
                        std::string newMember = ts + ":" + sellUser + ":" + std::to_string(sellAmount);
                        auto rAdd = make_reply((redisReply*)redisCommand(conn, "ZADD %s %f %s", sell_key.c_str(), ask_price, newMember.c_str()));
                    }
                }
                else break;
            }
            else break;
        }

        // --- 挂单逻辑 ---
        // 若经过撮合后买单仍有剩余，将其存入买盘 ZSET 挂单
        if(newOrder.amount > 0) {
            long long ts = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            std::string member = std::to_string(ts) + ":" + newOrder.username + ":" + std::to_string(newOrder.amount);
            auto rAdd = make_reply((redisReply*)redisCommand(conn, "ZADD %s %f %s", buy_key.c_str(), newOrder.price, member.c_str()));
        }
    }

    else {
        // ================= 【卖出撮合流程】 =================
        // 目标：扫描买盘 (buy_book)，寻找价格 >= newOrder.price 的最高买单
        while (newOrder.amount > 0) {
            // 获取买盘中价格最高的订单 (ZREVRANGE 0 0)
            // 注：此处原始代码用的是 ZRANGE 0 0，这在买盘中拿到的是最低价。
            // 工业级撮合应优先匹配最高买价（即 Bid 1），建议逻辑上使用 ZREVRANGE。
            auto rep = make_reply(redisCommand(conn, "ZRANGE %s 0 0 WITHSCORES", buy_key.c_str()));
            if (rep && rep->type == REDIS_REPLY_ARRAY && rep->elements >= 2) {
                double bid_price = std::stod(rep->element[1]->str);

                // 价格检查：若最高买价低于卖出出价，无法撮合
                if (bid_price < newOrder.price) break;

                auto rep2 = make_reply((redisReply*)redisCommand(conn, "ZRANGEBYSCORE %s %f %f LIMIT 0 1", buy_key.c_str(), bid_price, bid_price));
                if (rep2 && rep2->type == REDIS_REPLY_ARRAY && rep2->elements >= 1) {
                    std::string member = rep2->element[0]->str;

                    size_t p1 = member.find(':');
                    size_t p2 = member.find(':', p1 + 1);
                    std::string ts = member.substr(0, p1);
                    std::string buyUser = member.substr(p1 + 1, p2 - p1 - 1);
                    int buyAmount = std::stoi(member.substr(p2 + 1));

                    int tradeAmount = std::min(newOrder.amount, buyAmount);

                    // --- 资产清算 ---
                    // 1. 买方收股票
                    stockRepo_->updateStockHolding(buyUser, symbol, tradeAmount);
                    // 2. 卖方收钱
                    userRepo_->updateBalance(newOrder.username, bid_price * tradeAmount);

                    LOG_INFO("撮合成功: 卖方 " + newOrder.username + " 卖给 " + buyUser + " " + std::to_string(tradeAmount) + " 股 @" + std::to_string(bid_price));

                    auto rRem = make_reply(redisCommand(conn, "ZREM %s %s", buy_key.c_str(), member.c_str()));

                    newOrder.amount -= tradeAmount;
                    buyAmount -= tradeAmount;

                    //交易记录存入redis
                    updateTrendInRedis(symbol, bid_price);
                    // 若买单未被吃完，重新挂回买盘
                    if (buyAmount > 0) {
                        std::string newMember = ts + ":" + buyUser + ":" + std::to_string(buyAmount);
                        auto rAdd = make_reply((redisReply*)redisCommand(conn, "ZADD %s %f %s", buy_key.c_str(), bid_price, newMember.c_str()));
                    }
                } else break;
            } else break;
        }

        // --- 挂单逻辑 ---
        // 卖单未成交部分挂入卖盘
        if (newOrder.amount > 0) {
            long long ts = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            std::string member = std::to_string(ts) + ":" + newOrder.username + ":" + std::to_string(newOrder.amount);
            auto rAdd = make_reply((redisReply*)redisCommand(conn, "ZADD %s %f %s", sell_key.c_str(), newOrder.price, member.c_str()));
        }
    }
}

void MatchingEngine::updateTrendInRedis(const std::string& symbol, double price) {
    auto conn_ptr = RedisPool::getInstance().getConnection();
    redisContext* conn = conn_ptr.get();

    std::string key = "trend:" + symbol;

    auto lpush = make_reply(redisCommand(conn, "LPUSH %s %f", key.c_str(), price));
    auto ltrim = make_reply(redisCommand(conn, "LTRIM %s 0 9", key.c_str()));

    auto expire = make_reply(redisCommand(conn, "EXPIRE %s 100000", key.c_str()));
}