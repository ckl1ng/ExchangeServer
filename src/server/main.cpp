#include "DBPool.h"
#include "Router.hpp"
#include "Logger.hpp"
#include "Handler.hpp"
#include "RedisPool.h"
#include "TcpServer.h"
#include "Repositories.h"
#include "MatchingEngine.h"
#include "nlohmann/json.hpp"
#include "SessionManager.hpp"
#include "UserRepositoryImpl.h"
#include "StockRepositoryImpl.h"
#include <map>
#include <list>
#include <cstring>
#include <unistd.h>
#include <iostream>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>

using json = nlohmann::json;

Router g_router; //全局业务路由器实例，负责 Action -> Handler 的映射

/**
 * @brief 核心业务处理回调 (Reactor Business Callback)
 * 
 * 该函数是网络层与业务层的粘合剂，处理了最核心的 TCP 协议解析逻辑。
 * 
 * @param client_fd 触发可读事件的套接字
 * @return true 保持连接（长连接）；false 出错或断开，要求底层关闭 FD
 * 
 * @details 
 * 核心流程包括：
 * 1. **非阻塞循环读取**：配合 Epoll ET 模式，读光内核缓冲区。
 * 2. **协议拆包 (Unpacking)**：识别 4 字节长度头，解决粘包与半包问题。
 * 3. **JSON 路由**：解析指令并通过 g_router 分发至具体业务处理器。
 * 4. **可靠发送**：循环调用 send 确保大数据量的响应包完整发出。
 * 
 * @note 内部通过 `session->mtx` 保证了同一连接在多线程环境下的时序安全。
 */
bool MyEchoBusiness(int client_fd) {
    auto session = SessionManager::getInstance().getOrCreateSession(client_fd);

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
    // [包头: 4字节长度] + [包体: JSON]
    while (session->buffer.size() - processed_len >= 4) {
        uint32_t msg_len;
        memcpy(&msg_len, session->buffer.data() + processed_len, 4);
        msg_len = ntohl(msg_len); // 网络序转主机序

        // 半包检查：缓冲区剩余数据不足一个完整包
        if (session->buffer.size() - processed_len < 4 + msg_len) break;
        
        std::string json_str = session->buffer.substr(processed_len + 4, msg_len);
        processed_len += (4 + msg_len);

        try {
            // 路由派发
            json req = json::parse(json_str);
            json res = g_router.dispatch(session, req);
            LOG_INFO("收到消息: " + std::to_string(client_fd));

            // --- 阶段3：封装响应并发送 ---
            std::string reply = res.dump();
            uint32_t reply_len = htonl(reply.size()); // 主机序转网络序

            std::string packet;
            packet.append((char*)&reply_len, 4);
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
                    LOG_INFO("发送数据失败");
                    break;
                }
                else break;
            }
        }
        catch (const std::exception& e) {
            LOG_ERROR("处理请求异常: " + std::string(e.what()));
        }
    }
    
    // 清理已消费的缓冲区
    if (processed_len > 0) {
        session->buffer.erase(0, processed_len);
    }

    return true;
}

/**
 * @brief 程序入口：系统环境初始化与服务启动
 * 
 * @return int 退出状态码
 * 
 * @details 
 * 初始化序列：
 * 1. 日志系统初始化。
 * 2. 数据库连接池 (MySQL/Redis) 初始化。
 * 3. 仓储层 (Repository) 与 业务引擎 (Matching Engine) 实例化。
 * 4. 路由注册：定义系统支持的所有 API 接口。
 * 5. 启动网络服务器：监听 12345 端口，配置 4 个工作线程。
 */
int main() {
    // 初始化日志
    Logger::getInstance().init("server.log");
    
    // 初始化连接池
    DBPool::getInstance().init("127.0.0.1", "root", "root", "test_db", 10);
    RedisPool::getInstance().init("127.0.0.1", 6379, "", 10);

    // 实例化数据层与撮合引擎
    std::shared_ptr<IUserRepository> userRepo = std::make_shared<UserRepositoryImpl>();
    std::shared_ptr<IStockRepository> stockRepo = std::make_shared<StockRepositoryImpl>();
    std::shared_ptr<MatchingEngine> engine = std::make_shared<MatchingEngine>(userRepo, stockRepo);
    
    // 实例化共用的交易处理器
    auto tradeHandler = std::make_shared<TradeHandler>(userRepo, stockRepo, engine);

    // --- 路由注册表 ---
    g_router.registerHandler("login",        std::make_shared<LoginHandler>(userRepo));
    g_router.registerHandler("register",     std::make_shared<RegisterUserHandler>(userRepo));
    g_router.registerHandler("exit",         std::make_shared<ExitHandler>(userRepo));
    g_router.registerHandler("getbalance",   std::make_shared<GetBalanceHandler>(userRepo));
    g_router.registerHandler("deposit",      std::make_shared<DepositHandler>(userRepo));
    g_router.registerHandler("buy",          tradeHandler);
    g_router.registerHandler("sell",         tradeHandler);
    g_router.registerHandler("getholdings",  std::make_shared<GetHoldingsHandler>(stockRepo));
    g_router.registerHandler("getmarket",    std::make_shared<GetMarketHandler>());
    g_router.registerHandler("getnews",      std::make_shared<GetNewsHandler>());
    g_router.registerHandler("gettrend",     std::make_shared<GetTrendHandler>());
    g_router.registerHandler("getorders",    std::make_shared<GetOrdersHandler>());
    g_router.registerHandler("cancelorder",  std::make_shared<CancelOrderHandler>(userRepo, stockRepo));
    g_router.registerHandler("getallstocks", std::make_shared<GetAllStocksHandler>(stockRepo));
    // 启动网络引擎
    TcpServer server(12345, 4);
    server.setBusinessCallback(MyEchoBusiness);
    LOG_INFO("服务器启动成功，开始监听端口 12345...");
    server.start();

    return 0;
}