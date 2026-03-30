/**
 * @file TcpServer.h
 * @brief 高性能 Reactor 网络服务器类定义
 * 
 * 基于 Epoll 边缘触发模式 (ET) 实现的并发网络框架：
 * 1. 采用线程池分发业务任务，实现 IO 线程与业务线程分离。
 * 2. 内置心跳/超时管理机制，自动清理无效的长连接。
 * 3. 支持非阻塞 IO 和边缘触发，最大化单线程事件循环的吞吐量。
 */

#ifndef TCPSERVER_H
#define TCPSERVER_H

#include "ThreadPool.hpp"
#include "TimerManager.hpp"
#include <sys/epoll.h>
#include <functional>
#include <string>

/**
 * @class TcpServer
 * @brief TCP 服务器核心类
 * 
 * 封装了套接字初始化、Epoll 事件监听、多线程分发及定时器清理等底层逻辑。
 */
class TcpServer {
public:
    /**
     * @brief 业务回调函数类型定义
     * 
     * @param int 触发事件的客户端套接字 (fd)
     * @return bool 返回 true 表示保持连接，返回 false 表示关闭连接
     */
    using BusinessCallback = std::function<bool(int)>;

    /**
     * @brief 构造函数：初始化服务器配置
     * 
     * @param port      监听的端口号
     * @param threadNum 线程池中的工作线程数量
     */
    TcpServer(int port, int threadNum);

    /**
     * @brief 析构函数：关闭监听套接字与 Epoll 资源
     */
    ~TcpServer();

    /**
     * @brief 设置上层业务回调函数
     * 
     * @param cb 遵循 BusinessCallback 签名的函数对象
     */
    void setBusinessCallback(BusinessCallback cb) { onMessage_ = cb; }

    /**
     * @brief 启动服务器主循环
     * 
     * @details 进入阻塞的 epoll_wait 循环，分发新连接事件及数据就绪事件。
     * @note 调用此方法会阻塞当前线程（通常是主线程）。
     */
    void start();

private:
    /**
     * @brief 初始化监听套接字 (Listen Socket)
     * @note 包含 socket, setsockopt(SO_REUSEADDR), bind, listen 等操作。
     */
    void initSocket();

    /**
     * @brief 设置文件描述符为非阻塞模式
     * @param fd 需要设置的目标描述符
     */
    void setNonBlocking(int fd);

    /**
     * @brief 将描述符挂载到 Epoll 监控树上
     * @param fd     目标描述符
     * @param events Epoll 事件位掩码 (如 EPOLLIN | EPOLLET)
     */
    void addEvent(int fd, uint32_t events);

    /**
     * @brief 循环接收所有等待接入的客户端连接
     * @note 配合 ET 模式，必须使用 while(accept) 读尽所有连接请求。
     */
    void acceptConnection();

    /**
     * @brief 清理连接
     */
    void doCleanup(int fd);

    int port_;                 /**< 监听端口 */
    int listenFd_;             /**< 监听套接字描述符 */
    int epollFd_;              /**< Epoll 实例描述符 */
    struct epoll_event events_[1024]; /**< 用于接收就绪事件的缓冲区 */
    
    TimerManager timerMgr_;    /**< 连接超时管理器 */
    ThreadPool pool_;          /**< 业务处理线程池 */
    BusinessCallback onMessage_; /**< 上层业务逻辑钩子 */
};

#endif