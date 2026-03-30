#ifndef ROUTER_HPP
#define ROUTER_HPP

#include "json.hpp"
#include "Logger.hpp"
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
    virtual json handle(std::shared_ptr<Session> session, const json& req) = 0;
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
     * Key: action 字符串（如 "login", "buy"）
     * Value: 对应的 Handler 智能指针
     */
    std::unordered_map<std::string, std::shared_ptr<IHandler>> handlers_;
    /**
     * @brief 注册业务处理器
     * 
     * @param action  请求指令名称
     * @param handler 处理器实例指针
     * @note 通常在程序启动初始化阶段 (main 函数) 调用此方法。
     */
    void registerHandler(const std::string& action, std::shared_ptr<IHandler> handler) {
        handlers_[action] = handler;
    }

    /**
     * @brief 核心分发逻辑
     * 
     * 根据请求中的 "action" 字段查找并调用对应的处理器。
     * 
     * @param Session 当前会话对象指针
     * @param req     客户端请求 JSON
     * @return json   业务处理结果或错误信息
     * 
     * @details 
     * 如果请求中缺少 action 字段，或请求了未注册的 action，
     * 该函数会记录一条 ERROR 日志并返回标准错误 JSON。
     */
    json dispatch(std::shared_ptr<Session> Session, const json& req) {
        // 尝试从 JSON 中提取 action 字段，默认为空
        std::string action = req.value("action", "");
        
        // 检查是否存在对应的处理器
        if (handlers_.count(action)) {
            return handlers_[action]->handle(Session, req);
        }

        // 异常处理：未知的指令请求
        LOG_ERROR("未知的action:" + action);
        return {
            {"status", "false"}, 
            {"msg", "未知的 action: " + action}
        };
    }
};

#endif