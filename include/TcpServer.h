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
    using BusinessCallback = std::function<bool(int)>;

    TcpServer(int port, int threadNum);

    ~TcpServer();

    void setBusinessCallback(BusinessCallback cb) { onMessage_ = cb; }

    void start();

private:

    void initSocket();

    void setNonBlocking(int fd);

    void addEvent(int fd, uint32_t events);

    void acceptConnection();

    void doCleanup(int fd);

    int port_;                 
    int listenFd_;             
    int epollFd_;              
    struct epoll_event events_[1024]; 
    
    TimerManager timerMgr_;   
    ThreadPool pool_;       
    BusinessCallback onMessage_; 
};

#endif