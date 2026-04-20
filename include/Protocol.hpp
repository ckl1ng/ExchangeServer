#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include <cstdint>

// 禁用编译器字节对齐，保证网络传输时的紧凑性
#pragma pack(push, 1)

// 消息类型枚举，定义了系统中所有可能的消息类型
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

// 网络协议中所有消息的公共包头，包含消息体长度和消息类型
struct PacketHeader {
    uint32_t length;  // 包体 payload 长度
    MsgType  type;    // 消息类型
};

// 以下是各种消息类型对应的包体结构定义，所有字段都使用固定长度类型，且整体结构紧凑
struct AuthRequest {
    char username[32];
    char password[32];
};

// 注册和登录的响应包体结构相同，包含状态码和用户 ID
struct AuthResponse {
    uint8_t  status;     // 1: 成功, 0: 失败
    uint64_t user_id;    // 注册/登录成功后返回的内部 ID
};

// 通用响应结构，适用于一些不需要额外数据的操作，如注销、交易和撤单等
struct GenericResponse {
    uint8_t status;
};

// 资金相关的请求和响应结构定义
struct DepositRequest {
    uint64_t amount;     // 真实金额 * 10000
};

// 充值响应结构，包含状态码和充值后的最新余额
struct DepositResponse {
    uint8_t  status;
    uint64_t new_balance; // 充值后的最新余额
};

// 余额查询不需要 Request 包体，只需要包头带上 BALANCE_REQ 即可
struct BalanceResponse {
    uint8_t  status;
    uint64_t balance;
};

// 交易请求和响应结构定义
struct TradeRequest {
    uint32_t ticker_id;
    uint8_t  side;       // 0: BUY, 1: SELL
    uint64_t price;      
    uint32_t amount;
};

// 交易响应结构，包含状态码和成交价格（如果有的话）
struct CancelRequest {
    uint32_t ticker_id;
    uint64_t order_id;
};

// 撤单响应结构，包含状态码和撤单后订单的最新状态
struct TradeResponse { // 交易和撤单都可以复用这个基础响应
    uint8_t status;
};

// 市场数据请求和响应结构定义
struct MarketRequest {
    uint32_t ticker_id;
};

// 市场数据响应结构，包含状态码、当前最佳买价和卖价
struct MarketResponse {
    uint8_t  status;
    uint64_t best_bid;
    uint64_t best_ask;
};

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

// 用户订单记录结构体
struct OrderItem {
    uint64_t order_id;
    uint32_t ticker_id;
    uint8_t side;
    uint64_t price;
    uint64_t amount;
};

// 订单查询响应头，包含状态码和订单数量
struct OrdersResponseHeader {
    uint8_t status;
    uint32_t count;
};

// 趋势查询请求和响应结构定义
struct TrendRequest {
    uint32_t ticker_id;
};

// 趋势查询响应结构，包含状态码和趋势数据数量
struct TrendResponseHeader {
    uint8_t status;
    uint32_t count;
};

// 新闻查询请求和响应结构定义
struct NewsItem {
    uint32_t time;
    char title[128];
    char type[16];
    char symbol[16];
};

// 新闻查询响应头，包含状态码和新闻数量
struct NewsResponseHeader {
    uint8_t status;
    uint32_t count;
};

// 全部股票查询响应结构定义
struct StockItem {
    uint32_t ticker_id;
    char symbol[16];
};

// 全部股票查询响应头，包含状态码和股票数量
struct StocksResponseHeader {
    uint8_t status;
    uint32_t count;
};

// 恢复默认的编译器字节对齐
#pragma pack(pop)

#endif