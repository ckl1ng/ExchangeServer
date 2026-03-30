#include "TcpServer.h"
#include "Logger.hpp"
#include "SessionManager.hpp"
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <cstring>


/**
 * @brief 构造并初始化服务器运行环境
 * 
 * @param port      服务器监听的 TCP 端口
 * @param threadNum 业务执行线程池的大小
 * 
 * @details 
 * 初始化流程：
 * 1. 调用 initSocket 创建监听套接字并绑定端口。
 * 2. 创建 Epoll 实例。
 * 3. 将监听套接字挂载至 Epoll 树，开启新连接监听。
 */
TcpServer::TcpServer(int port, int threadNum) 
    : port_(port), pool_(threadNum), listenFd_(-1), epollFd_(-1) {
    initSocket();
    epollFd_ = epoll_create1(0);
    addEvent(listenFd_, EPOLLIN); // 初始监听：等待“大门”被敲响
}

/**
 * @brief 析构函数：执行物理资源的回收
 */
TcpServer::~TcpServer() {
    if (listenFd_ != -1) close(listenFd_);
    if (epollFd_ != -1) close(epollFd_);
}

/**
 * @brief 初始化监听套接字并配置非阻塞属性
 * 
 * @details 
 * 关键配置：
 * - SO_REUSEADDR: 允许服务器崩溃或重启后立即占用原端口，无需等待 TIME_WAIT 状态结束。
 * - listen(fd, 64): 设置全连接队列长度。
 * - setNonBlocking: 边缘触发 (ET) 模式必须配合非阻塞套接字使用。
 */
void TcpServer::initSocket() {
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    // 配置地址复用，提升调试与运维效率
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(listenFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("端口绑定失败: " + std::to_string(port_));
        exit(EXIT_FAILURE);
    }
    
    listen(listenFd_, 64);
    setNonBlocking(listenFd_);
}

/**
 * @brief 将文件描述符切换为非阻塞 (Non-Blocking) 模式
 * @param fd 目标文件描述符
 */
void TcpServer::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK); 
}

/**
 * @brief 将文件描述符注册到 Epoll 实例中
 * @param fd     目标套接字
 * @param events 事件掩码 (如 EPOLLIN | EPOLLET)
 */
void TcpServer::addEvent(int fd, uint32_t events) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev);
}

/**
 * @brief 循环处理所有新连接请求
 * 
 * @details 
 * 由于使用了 EPOLLET (边缘触发) 模式，当多个客户端同时发起连接时，
 * Epoll 仅会发出一次通知。因此，必须使用 while 循环不断 accept，
 * 直到返回 EAGAIN 或 EWOULDBLOCK 为止。
 * 
 * @note 
 * 1. 成功建立的连接会立即设为非阻塞并挂载。
 * 2. 初始赋予连接 60 秒的生存时间。
 */
void TcpServer::acceptConnection() {
    while (true) {
        sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int client_sock = accept(listenFd_, (struct sockaddr*)&client_addr, &len);
        
        if (client_sock == -1) {
            // EAGAIN 说明当前时刻所有的并发连接已接收完毕
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; 
            } else {
                LOG_ERROR("Accept 异常中断");
                break;
            }
        }
        LOG_INFO("新的连接: " + std::to_string(client_sock));
        
        setNonBlocking(client_sock);
        // 配置新连接：监听读事件，启用边缘触发
        addEvent(client_sock, EPOLLIN | EPOLLET);
        // 加入定时器管理
        timerMgr_.addTimer(client_sock, 60); 
    }
}

/**
 * @brief 启动服务器事件循环 (Event Loop)
 * 
 * @details 
 * 该函数是整个服务器的心跳中心，负责执行以下循环：
 * 1. **事件等待**：调用 epoll_wait 阻塞监听活跃套接字。
 * 2. **连接接入**：若 listenFd 就绪，调用 acceptConnection 处理握手。
 * 3. **任务派发**：若普通数据 FD 就绪，先执行 `EPOLL_CTL_DEL` 摘除该 FD
 *    （防止多线程环境下同一 FD 的后续数据包导致两个线程同时操作一个 Socket），
 *    然后将业务逻辑 (onMessage_) 包装为任务投递至线程池。
 * 4. **超时清理**：每次循环检测并剔除超过 60 秒无心跳的僵尸连接。
 * 
 * @note 业务处理完成后，若回调返回 true，会重新执行 addEvent 挂载 FD 回 Epoll 树。
 */
void TcpServer::start() {
    while (true) {
        // 等待 I/O 事件，设置 1000ms 超时用于驱动定时器检查
        int nfds = epoll_wait(epollFd_, events_, 1024, 1000);

        for (int i = 0; i < nfds; ++i) {
            int fd = events_[i].data.fd;

            if (fd == listenFd_) {
                // 处理新客户端连接接入
                acceptConnection();
            } else {
                // --- 核心安全机制：摘除处理中的 FD ---
                // 在多线程环境下，为保证同一连接的数据处理在逻辑上是串行的，
                // 且避免 Epoll 重复触发，必须在派发前先摘除。
                epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);

                // 更新活跃时间戳，给予该连接新的 60 秒配额
                timerMgr_.addTimer(fd, 60);
                
                // 将 IO 读写与业务逻辑任务入队线程池
                pool_.enqueue([this, fd] {
                    bool keepAlive = true;
                    if (onMessage_) {
                        // 执行 MyEchoBusiness 回调逻辑
                        keepAlive = onMessage_(fd); 
                    }
                    
                    // 若业务层表示需要继续保持长连接，则重新挂载回 Epoll
                    if (keepAlive) {
                        addEvent(fd, EPOLLIN | EPOLLET);
                    }
                    else {
                        doCleanup(fd);
                    }
                });
            }
        }

        // --- 定时清理：淘汰死连接 ---
        timerMgr_.handleTimeouts([this](int fd) {
            if(!SessionManager::getInstance().hasSession(fd))return;
            LOG_INFO("正在清理超时僵尸连接: " + std::to_string(fd));
            doCleanup(fd);
        });
    }
}

void TcpServer::doCleanup(int fd) {
    if(!SessionManager::getInstance().hasSession(fd))return;
    LOG_INFO("正在执行清理: " + std::to_string(fd));

    epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);

    timerMgr_.cancelTimer(fd);
    SessionManager::getInstance().cleanConnection(fd);
}