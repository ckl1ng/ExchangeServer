#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <random>
#include <iomanip>
#include <netinet/tcp.h>

// ==========================================
// 1. 二进制协议定义 (必须与服务端保持绝对一致)
// ==========================================
#pragma pack(push, 1)

enum class MsgType : uint16_t {
    REGISTER_REQ = 1001, REGISTER_RES = 1002,
    LOGIN_REQ    = 1003, LOGIN_RES    = 1004,
    DEPOSIT_REQ  = 2001, DEPOSIT_RES  = 2002,
    BALANCE_REQ  = 2003, BALANCE_RES  = 2004,
    TRADE_REQ    = 3001, TRADE_RES    = 3002,
    CANCEL_REQ   = 3003, CANCEL_RES   = 3004,
    HOLDINGS_REQ = 4001, HOLDINGS_RES = 4002,
    MARKET_REQ   = 4003, MARKET_RES   = 4004,
    ORDERS_REQ   = 4005, ORDERS_RES   = 4006,
    TREND_REQ    = 4007, TREND_RES    = 4008,
    NEWS_REQ     = 4009, NEWS_RES     = 4010,
    ALL_STOCKS_REQ = 4011, ALL_STOCKS_RES = 4012
};

struct PacketHeader {
    uint32_t length;
    MsgType  type;
};

struct AuthRequest { char username[32]; char password[32]; };
struct AuthResponse { uint8_t status; uint64_t user_id; };
struct DepositRequest { uint64_t amount; };
struct TradeRequest { uint32_t ticker_id; uint8_t side; uint64_t price; uint32_t amount; };
struct MarketRequest { uint32_t ticker_id; };
struct CancelRequest { uint32_t ticker_id; uint64_t order_id; };
struct TrendRequest { uint32_t ticker_id; };
struct StocksResponseHeader { uint8_t status; uint32_t count; };
struct StockItem { uint32_t ticker_id; char symbol[16]; };
struct OrdersResponseHeader { uint8_t status; uint32_t count; };
struct OrderItem { uint64_t order_id; uint32_t ticker_id; uint8_t side; uint64_t price; uint64_t amount; };

// 新增：持仓查询相关结构体
struct HoldingsResponseHeader { uint8_t status; uint32_t count; };
struct HoldingItem { uint32_t ticker_id; uint32_t quantity; };

#pragma pack(pop)

// ==========================================
// 统计与网络底层增强
// ==========================================
std::atomic<uint64_t> g_success(0);
std::atomic<uint64_t> g_fail(0);
std::atomic<uint64_t> g_latency(0);

bool send_all(int sock, const void* buffer, size_t length) {
    const char* ptr = static_cast<const char*>(buffer);
    size_t total = 0;
    while (total < length) {
        ssize_t s = send(sock, ptr + total, length - total, 0);
        if (s <= 0) return false;
        total += s;
    }
    return true;
}

bool recv_all(int sock, void* buffer, size_t length) {
    char* ptr = static_cast<char*>(buffer);
    size_t total = 0;
    while (total < length) {
        ssize_t r = recv(sock, ptr + total, length - total, 0);
        if (r <= 0) return false;
        total += r;
    }
    return true;
}

bool send_pkt(int sock, MsgType type, const void* data, uint32_t len) {
    PacketHeader hdr;
    hdr.length = htonl(len);
    hdr.type = static_cast<MsgType>(htons(static_cast<uint16_t>(type)));
    
    char buf[1024]; 
    memcpy(buf, &hdr, sizeof(hdr));
    if (len > 0) {
        memcpy(buf + sizeof(hdr), data, len);
    }
    return send_all(sock, buf, sizeof(hdr) + len);
}

bool recv_pkt(int sock, MsgType& type, std::string& body) {
    PacketHeader hdr;
    if (!recv_all(sock, &hdr, sizeof(hdr))) return false;
    
    type = static_cast<MsgType>(ntohs(static_cast<uint16_t>(hdr.type)));
    uint32_t len = ntohl(hdr.length);
    
    if (len > 1024 * 1024) return false; 
    
    if (len > 0) {
        body.resize(len);
        if (!recv_all(sock, &body[0], len)) return false;
    } else {
        body.clear();
    }
    return true;
}

// ==========================================
// 查看模式：展示 root 股票与持仓
// ==========================================
void view_root_stocks(std::string host, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { std::cerr << "创建 Socket 失败\n"; return; }

    struct timeval tv;
    tv.tv_sec = 3; tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) { 
        std::cerr << "连接服务器失败\n"; 
        close(sock); return; 
    }

    MsgType r_type;
    std::string r_body;

    // 1. 登录 root 账户
    AuthRequest auth;
    memset(&auth, 0, sizeof(auth));
    strncpy(auth.username, "root", 31);
    strncpy(auth.password, "root", 31);
    
    std::cout << "正在登录 root 账户...\n";
    if (!send_pkt(sock, MsgType::LOGIN_REQ, &auth, sizeof(auth)) || !recv_pkt(sock, r_type, r_body)) {
        std::cerr << "登录请求失败或无响应\n";
        close(sock); return;
    }
    AuthResponse* auth_res = reinterpret_cast<AuthResponse*>(&r_body[0]);
    if (auth_res->status != 1) {
        std::cerr << "登录被拒绝，请确认 root 账号存在且密码为 root\n";
        close(sock); return;
    }
    std::cout << "✅ root 登录成功! (User ID: " << auth_res->user_id << ")\n\n";

    // 2. 获取全市场可用股票列表
    std::vector<StockItem> stock_list;
    if (send_pkt(sock, MsgType::ALL_STOCKS_REQ, nullptr, 0) && recv_pkt(sock, r_type, r_body)) {
        if (r_body.size() >= sizeof(StocksResponseHeader)) {
            auto* h = reinterpret_cast<const StocksResponseHeader*>(r_body.data());
            auto* items = reinterpret_cast<const StockItem*>(r_body.data() + sizeof(StocksResponseHeader));
            std::cout << "📊 市场总计支持 " << h->count << " 支股票:\n";
            std::cout << "----------------------------------------\n";
            for (uint32_t i = 0; i < h->count; ++i) {
                stock_list.push_back(items[i]);
                std::cout << std::setw(4) << items[i].ticker_id << " | " << items[i].symbol << "\n";
            }
            std::cout << "----------------------------------------\n\n";
        }
    }

    // 3. 获取 root 的持仓情况
    if (send_pkt(sock, MsgType::HOLDINGS_REQ, nullptr, 0) && recv_pkt(sock, r_type, r_body)) {
        if (r_body.size() >= sizeof(HoldingsResponseHeader)) {
            auto* h = reinterpret_cast<const HoldingsResponseHeader*>(r_body.data());
            auto* items = reinterpret_cast<const HoldingItem*>(r_body.data() + sizeof(HoldingsResponseHeader));
            
            std::cout << "💼 root 账户当前持仓 (" << h->count << " 条记录):\n";
            std::cout << "----------------------------------------\n";
            std::cout << std::left << std::setw(10) << "Ticker ID" 
                      << std::setw(15) << "Symbol" 
                      << "Quantity\n";
            std::cout << "----------------------------------------\n";
            
            for (uint32_t i = 0; i < h->count; ++i) {
                std::string symbol = "UNKNOWN";
                for (const auto& s : stock_list) {
                    if (s.ticker_id == items[i].ticker_id) {
                        symbol = s.symbol;
                        break;
                    }
                }
                std::cout << std::left << std::setw(10) << items[i].ticker_id
                          << std::setw(15) << symbol
                          << items[i].quantity << " 股\n";
            }
            std::cout << "----------------------------------------\n";
        }
    }

    close(sock);
}

// ==========================================
// 压测工作线程
// ==========================================
void benchmark_worker(int id, std::string host, int port, int requests, int mode) {
    // ... （压测代码保持不变，已省略以免过长，保留你之前的逻辑即可）...
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { g_fail += requests; return; }

    struct timeval tv;
    tv.tv_sec = 3; tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

    int opt = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) { g_fail += requests; return; }

    std::string username = "testuser_" + std::to_string(id);
    MsgType r_type;
    std::string r_body;

    AuthRequest auth;
    memset(&auth, 0, sizeof(auth));
    strncpy(auth.username, username.c_str(), 31);
    strncpy(auth.password, "123", 31);
    if (!send_pkt(sock, MsgType::REGISTER_REQ, &auth, sizeof(auth)) || !recv_pkt(sock, r_type, r_body)) {
        close(sock); g_fail += requests; return;
    }

    std::vector<uint32_t> ticker_ids;
    if (send_pkt(sock, MsgType::ALL_STOCKS_REQ, nullptr, 0) && recv_pkt(sock, r_type, r_body)) {
        if (r_body.size() >= sizeof(StocksResponseHeader)) {
            auto* h = reinterpret_cast<const StocksResponseHeader*>(r_body.data());
            auto* items = reinterpret_cast<const StockItem*>(r_body.data() + sizeof(StocksResponseHeader));
            for (uint32_t i = 0; i < h->count; ++i) ticker_ids.push_back(items[i].ticker_id);
        }
    }
    if (ticker_ids.empty()) ticker_ids.push_back(1); 

    DepositRequest dep; dep.amount = 100000000; 
    if (!send_pkt(sock, MsgType::DEPOSIT_REQ, &dep, sizeof(dep)) || !recv_pkt(sock, r_type, r_body)) {
        close(sock); g_fail += requests; return;
    }

    std::mt19937 rng(id + time(0));
    std::vector<uint64_t> active_orders;

    for (int i = 0; i < requests; ++i) {
        uint32_t weight = rng() % 100;
        uint32_t tid = ticker_ids[rng() % ticker_ids.size()];
        auto start = std::chrono::high_resolution_clock::now();
        bool send_ok = false;

        if (mode == 1) {
            if (weight < 40) { 
                MarketRequest m; m.ticker_id = tid;
                send_ok = send_pkt(sock, MsgType::MARKET_REQ, &m, sizeof(m));
            } else if (weight < 80) { 
                TradeRequest tr; tr.ticker_id = tid; tr.side = rng() % 2; 
                tr.price = (100 + (rng() % 50)) * 10000; tr.amount = 1 + (rng() % 10);
                send_ok = send_pkt(sock, MsgType::TRADE_REQ, &tr, sizeof(tr));
            } else if (weight < 90) { 
                send_ok = send_pkt(sock, (rng() % 2 == 0) ? MsgType::HOLDINGS_REQ : MsgType::BALANCE_REQ, nullptr, 0);
            } else { 
                if (!active_orders.empty()) {
                    CancelRequest cr; cr.order_id = active_orders.back(); cr.ticker_id = tid; 
                    send_ok = send_pkt(sock, MsgType::CANCEL_REQ, &cr, sizeof(cr));
                    active_orders.pop_back();
                } else {
                    send_ok = send_pkt(sock, MsgType::ORDERS_REQ, nullptr, 0);
                    if (send_ok && recv_pkt(sock, r_type, r_body) && r_body.size() >= sizeof(OrdersResponseHeader)) {
                        auto* h = reinterpret_cast<const OrdersResponseHeader*>(r_body.data());
                        auto* items = reinterpret_cast<const OrderItem*>(r_body.data() + sizeof(OrdersResponseHeader));
                        for (uint32_t j = 0; j < h->count; ++j) active_orders.push_back(items[j].order_id);
                        auto end = std::chrono::high_resolution_clock::now();
                        g_success++;
                        g_latency += std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                        continue;
                    } else { send_ok = false; }
                }
            }
        } 
        else {
            if (weight < 30) {
                MarketRequest m; m.ticker_id = tid;
                send_ok = send_pkt(sock, MsgType::MARKET_REQ, &m, sizeof(m));
            } else if (weight < 60) {
                TrendRequest t; t.ticker_id = tid;
                send_ok = send_pkt(sock, MsgType::TREND_REQ, &t, sizeof(t)); 
            } else if (weight < 90) {
                TradeRequest tr; tr.ticker_id = tid; tr.side = rng() % 2; 
                tr.price = 120 * 10000; tr.amount = 5;
                send_ok = send_pkt(sock, MsgType::TRADE_REQ, &tr, sizeof(tr));
            } else {
                send_ok = send_pkt(sock, MsgType::NEWS_REQ, nullptr, 0);
            }
        }

        if (!send_ok) { g_fail++; break; }

        if (recv_pkt(sock, r_type, r_body)) {
            auto end = std::chrono::high_resolution_clock::now();
            g_success++;
            g_latency += std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        } else {
            g_fail++; break; 
        }
    }
    close(sock);
}

int main(int argc, char** argv) {
    // 默认参数：如果在终端不传任何参数，默认进入 mode 2 也就是“查看股票持仓模式”
    int mode    = 0;
    int threads = (argc > 1) ? std::stoi(argv[1]) : 500;   
    int reqs    = (argc > 2) ? std::stoi(argv[2]) : 2000;  

    std::cout << "=======================================\n";
    std::cout << " 交易系统压测与查询工具 \n";
    std::cout << " 用法: ./client [线程数] [每线程请求数] [模式]\n";
    std::cout << " 模式: (0=混合压测, 1=纯交易压测, 2=查看 root 股票)\n";
    std::cout << " 当前模式: " << mode << "\n";
    std::cout << "=======================================\n\n";

    if (mode == 2) {
        view_root_stocks("127.0.0.1", 12345);
        return 0;
    }

    std::cout << " 并发线程 (Connections) : " << threads << "\n";
    std::cout << " 线程请求 (Req/Thread)  : " << reqs << "\n";
    std::cout << " 总计请求 (Total Reqs)  : " << threads * reqs << "\n\n";
    std::cout << "预热并建立连接中，开始压测...\n";

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> vt;
    for (int i = 0; i < threads; ++i) {
        vt.emplace_back(benchmark_worker, i, "127.0.0.1", 12345, reqs, mode);
    }
    for (auto& t : vt) t.join();

    auto end = std::chrono::high_resolution_clock::now();
    double sec = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() / 1000.0;
    
    uint64_t succ = g_success.load();
    uint64_t failed = g_fail.load();
    double qps = succ > 0 ? (succ / sec) : 0;
    double avg_lat = succ > 0 ? (g_latency.load() / (double)succ / 1000.0) : 0;

    std::cout << "\n--- 压测报告 ---\n";
    std::cout << "总耗时   : " << sec << " s\n";
    std::cout << "吞吐量   : " << qps << " QPS\n";
    std::cout << "平均延迟 : " << avg_lat << " ms\n";
    std::cout << "成功数   : " << succ << "\n";
    std::cout << "失败数   : " << failed << "\n";

    return 0;
}