#ifndef COMMON_HPP
#define COMMON_HPP

#include <string>
#include <cstdint>

enum class OrderSide { BUY, SELL };
constexpr int64_t PRICE_SCALE = 10000;

// 订单结构体定义
struct Order {
    uint64_t order_id;
    uint32_t ticker_id;
    uint64_t user_id;

    OrderSide side;
    uint64_t price;
    uint64_t quantity;
    uint64_t timestamp;
};

// 成交记录结构体定义
struct MatchRecord {
    uint32_t ticker_id;
    uint64_t maker_order_id;
    uint64_t taker_order_id;

    uint64_t buyer_user_id;
    uint64_t sell_user_id;

    uint64_t buyer_expected_price; 
    uint64_t match_price;
    uint64_t match_quantity;
    uint64_t timestamp;
};

// 撤单记录结构体定义
struct CancelRecord {
    uint32_t ticker_id;
    uint64_t order_id;
    uint64_t user_id;
    OrderSide side;
    uint64_t price;
    uint64_t quantity;
    uint64_t timestamp;
};

#endif