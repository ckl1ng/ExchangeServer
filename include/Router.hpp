#ifndef ROUTER_HPP
#define ROUTER_HPP

#include "Logger.hpp"
#include "Protocol.hpp"
#include "json.hpp"
#include <memory>
#include <string>
#include <mutex>
#include <unordered_map>
using json = nlohmann::json;


/** * @struct Session
 * @brief 会话对象，绑定 TCP 连接与用户状态
 * @details 每个 TCP 连接对应一个 Session 实例，Session 中记录了用户的登录状态、用户名、用户ID等信息，以及一个缓冲区用于存储未处理的字节流数据。
 * 维护单个 TCP 连接在生命周期内的所有持久化状态。
 */
struct Session { 
    int fd;
    std::string buffer;

    std::string username;
    uint64_t user_id = 0;
    std::mutex mtx;

    Session(int f) : fd(f) {}
    bool isLoggedIn() const { return !username.empty(); }
};

/**
 * @class IHandler
 * @brief 业务逻辑处理器抽象基类 (Base Business Handler Interface)
 * 
 * 所有的具体业务逻辑（如登录、下单、查询）都必须继承此接口并实现 handle 方法。
 */
class IHandler {
public:

    virtual ~IHandler() = default;
    // 业务逻辑处理接口
    virtual std::string handle(Session* session, const char* req, uint32_t req_len) = 0;
};

/**
 * @class Router
 * @brief 核心业务路由分发器 (Central Business Router)
 * 
 * 负责维护请求指令 (Action) 与处理器 (Handler) 的映射关系。
 */
class Router {
private:
    
    // 消息类型到处理器实例的映射表，支持动态注册和高效查找
    std::unordered_map<MsgType, std::shared_ptr<IHandler>> handlers_;

    Router() = default; // 私有构造函数，禁止外部实例化
public:
    static Router& getInstance() {
        static Router instance;
        return instance;
    }

    // 注册处理器实例到对应的消息类型
    void registerHandler(MsgType type, std::shared_ptr<IHandler> handler) {
        handlers_[type] = handler;
    }

    // 根据消息类型分发请求到对应的处理器
    std::string dispatch(Session* Session, MsgType type, const char* req, uint32_t req_len) {
        // 尝试从 JSON 中提取 action 字段，默认为空
        if (handlers_.count(type)) {
            return handlers_[type]->handle(Session, req, req_len);
        }
        LOG_ERROR("未知的 msgType");
        return "";
    }
};

#endif