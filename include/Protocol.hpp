#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include <cstdint>

// 禁用编译器字节对齐，保证网络传输时的紧凑性
#pragma pack(push, 1)

// ==========================================
// 1. 消息类型枚举 (涵盖所有业务)
// ==========================================
enum class MsgType : uint16_t {
    // 基础鉴权
    REGISTER_REQ   = 1001, REGISTER_RES   = 1002,
    LOGIN_REQ      = 1003, LOGIN_RES      = 1004,
    LOGOUT_REQ     = 1005, LOGOUT_RES     = 1006,

    // 资金操作
    DEPOSIT_REQ    = 2001, DEPOSIT_RES    = 2002,
    BALANCE_REQ    = 2003, BALANCE_RES    = 2004,

    // 核心交易
    TRADE_REQ      = 3001, TRADE_RES      = 3002,
    CANCEL_REQ     = 3003, CANCEL_RES     = 3004,

    // 查询类 
    HOLDINGS_REQ   = 4001, HOLDINGS_RES   = 4002,
    MARKET_REQ     = 4003, MARKET_RES     = 4004,
    ORDERS_REQ     = 4005, ORDERS_RES     = 4006,
    TREND_REQ      = 4007, TREND_RES      = 4008,
    NEWS_REQ       = 4009, NEWS_RES       = 4010,
    ALL_STOCKS_REQ = 4011, ALL_STOCKS_RES = 4012
};

// ==========================================
// 2. 统一包头
// ==========================================
struct PacketHeader {
    uint32_t length;  // 包体 payload 长度
    MsgType  type;    // 消息类型
};

// ==========================================
// 3. 鉴权与基础操作 (注册/登录通用)
// ==========================================
struct AuthRequest {
    char username[32];
    char password[32];
};

struct AuthResponse {
    uint8_t  status;     // 1: 成功, 0: 失败
    uint64_t user_id;    // 注册/登录成功后返回的内部 ID
};

struct GenericResponse {
    uint8_t status;
};

// ==========================================
// 4. 充值与余额
// ==========================================
struct DepositRequest {
    uint64_t amount;     // 真实金额 * 10000
};

struct DepositResponse {
    uint8_t  status;
    uint64_t new_balance; // 充值后的最新余额
};

// 余额查询不需要 Request 包体，只需要包头带上 BALANCE_REQ 即可
struct BalanceResponse {
    uint8_t  status;
    uint64_t balance;
};

// ==========================================
// 5. 核心交易与撤单
// ==========================================
struct TradeRequest {
    uint32_t ticker_id;
    uint8_t  side;       // 0: BUY, 1: SELL
    uint64_t price;      
    uint32_t amount;
};

struct CancelRequest {
    uint32_t ticker_id;
    uint64_t order_id;
};

struct TradeResponse { // 交易和撤单都可以复用这个基础响应
    uint8_t status;
};

// ==========================================
// 6. 查询盘口行情
// ==========================================
struct MarketRequest {
    uint32_t ticker_id;
};

struct MarketResponse {
    uint8_t  status;
    uint64_t best_bid;
    uint64_t best_ask;
};

// ==========================================
// 7. 查询持仓及其他变长数据结构
// ==========================================

// 单个持仓记录
struct HoldingItem {
    uint32_t ticker_id;
    uint32_t quantity;
};

// 持仓响应头
struct HoldingsResponseHeader {
    uint8_t  status;
    uint32_t count;      // 关键：告诉客户端后面紧跟着几个 HoldingItem
};

struct OrderItem {
    uint64_t order_id;
    uint32_t ticker_id;
    uint8_t side;
    uint64_t price;
    uint64_t amount;
};

struct OrdersResponseHeader {
    uint8_t status;
    uint32_t count;
};

struct TrendRequest {
    uint32_t ticker_id;
};

struct TrendResponseHeader {
    uint8_t status;
    uint32_t count;
};

struct NewsItem {
    uint32_t time;
    char title[128];
    char type[16];
    char symbol[16];
};

struct NewsResponseHeader {
    uint8_t status;
    uint32_t count;
};

struct StockItem {
    uint32_t ticker_id;
    char symbol[16];
};

struct StocksResponseHeader {
    uint8_t status;
    uint32_t count;
};

#pragma pack(pop)

#endif