#include "DBPool.h"
#include "Router.hpp"
#include "Logger.hpp"
#include "Handler.hpp"
#include "RedisPool.h"
#include "TcpServer.h"
#include "Repositories.h"
#include "MatchingEngine.hpp"
#include "nlohmann/json.hpp"
#include "SessionManager.hpp"
#include "UserRepositoryImpl.h"
#include "StockRepositoryImpl.h"
#include "SettlementProcessor.hpp"
#include <map>
#include <list>
#include <cstring>
#include <unistd.h>
#include <iostream>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>

Router g_router; //全局业务路由器实例，负责 Action -> Handler 的映射

bool MyEchoBusiness(int client_fd) {
    auto session = SessionManager::getInstance().getSession(client_fd);
    if (!session) return false; 

    char buff[4096] = {0};
    std::string localData;

    // --- 阶段1：读取字节流 ---
    while (true) {
        ssize_t n = recv(client_fd, buff, sizeof(buff), 0);
        if (n > 0) {
            localData.append(buff, n);
        }
        else if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // 读空缓冲区
            else if (errno == EINTR) continue;                 // 被信号中断，重试
            else { SessionManager::getInstance().cleanConnection(client_fd); return false; } // 真实错误
        }
        else { SessionManager::getInstance().cleanConnection(client_fd); return false; } // 客户端关闭
    }

    if (localData.empty()) return true;

    // 保护 Session 缓冲区
    std::lock_guard<std::mutex> lock(session->mtx);
    session->buffer += localData;

    size_t processed_len = 0;

    // --- 阶段2：协议分帧与分发 ---
    // [包头: PacketHeader] + [包体: 二进制 Payload]
    while (session->buffer.size() - processed_len >= sizeof(PacketHeader)) {
        PacketHeader header;
        memcpy(&header, session->buffer.data() + processed_len, sizeof(PacketHeader));
        header.length = ntohl(header.length);
        uint16_t raw_type = ntohs(static_cast<uint16_t>(header.type));
        header.type = static_cast<MsgType>(raw_type);

        // 半包检查：缓冲区剩余数据不足一个完整包
        if (session->buffer.size() - processed_len < sizeof(PacketHeader) + header.length) break;
        
        const char* payload = session->buffer.data() + processed_len + sizeof(PacketHeader);
        
        std::string reply = "";
        try {
            // 路由派发
            reply = Router::getInstance().dispatch(session, header.type, payload, header.length);
            // LOG_INFO("收到消息: " + std::to_string(client_fd) + " 类型: " + std::to_string(raw_type));
        } catch (const std::exception& e) {
            LOG_ERROR("处理请求异常: " + std::string(e.what()));
        }

        processed_len += (sizeof(PacketHeader) + header.length);

        // --- 阶段3：封装响应并发送 ---
        if (!reply.empty()) {
            PacketHeader res_header;
            res_header.length = htonl(reply.size());
            res_header.type = static_cast<MsgType>(htons(raw_type + 1));
            
            std::string packet;
            packet.append((char*)&res_header, sizeof(res_header));
            packet.append(reply);

            size_t total_sent = 0;
            while (total_sent < packet.size()) {
                ssize_t s = send(client_fd, packet.data() + total_sent, packet.size() - total_sent, 0);
                if (s > 0) total_sent += s;
                else if (s < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        continue;
                    }
                    LOG_ERROR("发送数据失败");
                    break;
                }
                else break;
            }
        }
    }
    
    // 清理已消费的缓冲区
    if (processed_len > 0) {
        session->buffer.erase(0, processed_len);
    }

    return true;
}

int main() {
    // 初始化日志
    Logger::getInstance().init("server.log");
    
    // 初始化连接池
    DBPool::getInstance().init("127.0.0.1", "root", "root", "test_db", 49);
    RedisPool::getInstance().init("127.0.0.1", 6379, "", 49);

    UserManager::getInstance().loadAllFromDB();
    TickerManager::getInstance().loadAllTickersFromDB();
        // 实例化数据层与撮合引擎
    std::shared_ptr<IUserRepository> userRepo = std::make_shared<UserRepositoryImpl>();
    std::shared_ptr<IStockRepository> stockRepo = std::make_shared<StockRepositoryImpl>();
    std::shared_ptr<SettlementProcessor> settlement = std::make_shared<SettlementProcessor>(userRepo, stockRepo);
    
    std::shared_ptr<MatchingEngine> engine = std::make_shared<MatchingEngine>(settlement);

    auto all_ticker_ids = TickerManager::getInstance().getAllTickerIds();
    for (auto id : all_ticker_ids) {
        engine->init(id);
        LOG_INFO("初始化OrderBook: " + TickerManager::getInstance().getSymbol(id));
    }

    std::shared_ptr<WALManager> wal = std::make_shared<WALManager>(engine);

    // --- 路由注册表 ---
    g_router.registerHandler(MsgType::LOGIN_REQ,      std::make_shared<LoginHandler>(userRepo));
    g_router.registerHandler(MsgType::REGISTER_REQ,   std::make_shared<RegisterUserHandler>(userRepo));
    g_router.registerHandler(MsgType::LOGOUT_REQ,     std::make_shared<ExitHandler>(userRepo));
    g_router.registerHandler(MsgType::BALANCE_REQ,    std::make_shared<GetBalanceHandler>());
    g_router.registerHandler(MsgType::DEPOSIT_REQ,    std::make_shared<DepositHandler>(userRepo));
    g_router.registerHandler(MsgType::TRADE_REQ,      std::make_shared<TradeHandler>(wal));
    g_router.registerHandler(MsgType::HOLDINGS_REQ,   std::make_shared<GetHoldingsHandler>(stockRepo));
    g_router.registerHandler(MsgType::MARKET_REQ,     std::make_shared<GetMarketHandler>(engine));
    g_router.registerHandler(MsgType::NEWS_REQ,       std::make_shared<GetNewsHandler>());
    g_router.registerHandler(MsgType::TREND_REQ,      std::make_shared<GetTrendHandler>());
    g_router.registerHandler(MsgType::ORDERS_REQ,     std::make_shared<GetOrdersHandler>(engine));
    g_router.registerHandler(MsgType::CANCEL_REQ,     std::make_shared<CancelOrderHandler>(wal));
    g_router.registerHandler(MsgType::ALL_STOCKS_REQ, std::make_shared<GetAllStocksHandler>(stockRepo));
    // 启动网络引擎
    TcpServer server(12345, 24);
    server.setBusinessCallback(MyEchoBusiness);
    LOG_INFO("服务器启动成功，开始监听端口 12345...");
    server.start();

    return 0;
}