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


/**
 * @struct Session
 * @brief 客户端连接会话上下文 (Client Session Context)
 * 
 * 维护单个 TCP 连接在生命周期内的所有持久化状态。
 */
struct Session { 
    int fd;
    std::string buffer;

    std::string username;
    uint64_t user_id = 0;
    std::mutex mtx;

    /**
     * @brief 构造函数
     * @param f 客户端套接字
     */
    Session(int f) : fd(f) {}
    /**
     * @brief 检查当前会话是否已通过身份验证
     * @return true 已登录
     * @return false 未登录
     */
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
    /**
     * @brief 虚析构函数，确保派生类资源正确释放
     */
    virtual ~IHandler() = default;
    /**
     * @brief 业务逻辑处理接口
     * 
     * @param session 当前请求所属的会话对象指针
     * @param req     解析后的 JSON 请求报文
     * @return json   返回给客户端的 JSON 响应报文
     */
    virtual std::string handle(Session* session, const char* req, uint32_t req_len) = 0;
};

/**
 * @class Router
 * @brief 核心业务路由分发器 (Central Business Router)
 * 
 * 负责维护请求指令 (Action) 与处理器 (Handler) 的映射关系。
 */
class Router {
public:
    /**
     * @brief 处理器映射表
     */
    std::unordered_map<MsgType, std::shared_ptr<IHandler>> handlers_;
    /**
    /**
     * @brief 注册业务处理器
     * 
     * @param type    消息类型枚举
     * @param handler 处理器实例指针
     */
    void registerHandler(MsgType type, std::shared_ptr<IHandler> handler) {
        handlers_[type] = handler;
    }

    /**
     * @brief 核心分发逻辑
     * 
     * 根据请求中的 MsgType 查找并调用对应的处理器。
     * 
     * @param Session 当前会话对象指针
     * @param type    请求的消息类型
     * @param req     客户端请求数据指针
     * @param req_len 请求包体长度
     * @return std::string 业务处理结果（二进制数据）
     */
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