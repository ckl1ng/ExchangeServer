#include "TcpServer.h"
#include "Logger.hpp"
#include "SessionManager.hpp"
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <cstring>
#include <netinet/tcp.h>

TcpServer::TcpServer(int port, int threadNum) 
    : port_(port), pool_(threadNum), listenFd_(-1), epollFd_(-1) {
    initSocket();
    epollFd_ = epoll_create1(0);
    addEvent(listenFd_, EPOLLIN); 
}

TcpServer::~TcpServer() {
    if (listenFd_ != -1) close(listenFd_);
    if (epollFd_ != -1) close(epollFd_);
}

void TcpServer::initSocket() {
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    setsockopt(listenFd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(listenFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("端口绑定失败: " + std::to_string(port_));
        exit(EXIT_FAILURE);
    }
    
    
    listen(listenFd_, 4096); 
    setNonBlocking(listenFd_);
}

void TcpServer::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK); 
}

void TcpServer::addEvent(int fd, uint32_t events) {
    struct epoll_event ev;
    ev.events = events; // 恢复纯净的 events 赋值
    ev.data.fd = fd;
    epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev);
}

void TcpServer::acceptConnection() {
    while (true) {
        sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int client_sock = accept(listenFd_, (struct sockaddr*)&client_addr, &len);
        
        if (client_sock == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; 
            else { LOG_ERROR("Accept 异常中断"); break; }
        }
        
        setNonBlocking(client_sock);
        int opt = 1;
        setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
        SessionManager::getInstance().createSession(client_sock);

        addEvent(client_sock, EPOLLIN | EPOLLET | EPOLLONESHOT);
        timerMgr_.addTimer(client_sock, 60); 
    }
}

void TcpServer::start() {
    while (true) {
        int nfds = epoll_wait(epollFd_, events_, 1024, 1000);

        for (int i = 0; i < nfds; ++i) {
            int fd = events_[i].data.fd;

            if (fd == listenFd_) {
                LOG_INFO("新的连接");
                acceptConnection();
            } else {
                timerMgr_.addTimer(fd, 60);
                
                pool_.enqueue([this, fd] {
                    bool keepAlive = true;
                    if (onMessage_) {
                        keepAlive = onMessage_(fd); 
                    }
                    
                    if (keepAlive) {
                        struct epoll_event ev;
                        ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                        ev.data.fd = fd;
                        epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev);
                    } else {
                        doCleanup(fd);
                    }
                });
            }
        }

        timerMgr_.handleTimeouts([this](int fd) {
            if(!SessionManager::getInstance().hasSession(fd))return;
            doCleanup(fd);
        });
    }
}

void TcpServer::doCleanup(int fd) {
    if(!SessionManager::getInstance().hasSession(fd)) return;
    epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
    timerMgr_.cancelTimer(fd);
    SessionManager::getInstance().cleanConnection(fd);
}