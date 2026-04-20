#ifndef SESSION_MANAGER_HPP
#define SESSION_MANAGER_HPP

#include "Router.hpp"
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <sys/epoll.h>
#include <functional>
#include <string>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <atomic>
#include <arpa/inet.h>
#include <cstring>

/**
 * @class SessionManager
 * @brief 会话管理器，负责维护 TCP 连接与用户状态的映射
 */
class SessionManager {
private:
    static constexpr int MAX_FD = 65535;
    // std::unordered_map<int, std::shared_ptr<Session>> sessions_;
    std::atomic<Session*> sessions_[MAX_FD];

    SessionManager() = default;

public:

    // 获取单例实例
    static SessionManager& getInstance(){
        static SessionManager instance;
        return instance;
    }
    
    // 仅在 TcpServer accept 新连接时调用（写锁）
    void createSession(int fd) {
        if (fd < MAX_FD) {
            Session* old = sessions_[fd].exchange(new Session(fd), std::memory_order_acq_rel);
            if (old) delete old;
        }
    }

    // 在收发数据时高频调用（读锁，支持48个线程完全并发）
    Session* getSession(int fd) {
        if (fd >= MAX_FD) return nullptr;
        return sessions_[fd].load(std::memory_order_acquire);
    }

    // 在连接断开时调用（写锁）
    void removeSession(int fd) {
        if (fd < MAX_FD) {
            Session* s = sessions_[fd].exchange(nullptr, std::memory_order_acq_rel);
            delete s;
        }
    }

    void cleanConnection(int fd) {
        removeSession(fd);
        close(fd);
        // LOG_INFO("连接 " + std::to_string(client_fd) + " 已关闭");
    }

    bool hasSession(int fd) {
        return sessions_[fd] == nullptr;
    }
};

#endif