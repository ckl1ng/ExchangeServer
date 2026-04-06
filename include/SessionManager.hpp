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
 * @brief 客户端会话状态管理器 (Client Session Manager)
 * 
 * 维护全局 TCP 客户端连接的状态与独立缓冲区，是实现有状态协议的关键：
 * - sessions_: 线程安全的映射表，通过 FD 索引对应的 Session 对象。
 * - getOrCreateSession: 采用懒加载模式，确保每个连接都有独立的协议解析缓冲区。
 * - mtx_: 互斥锁保护，支持在高并发接入/断开时维护映射表的一致性。
 */
class SessionManager {
private:
    static constexpr int MAX_FD = 65535;
    // std::unordered_map<int, std::shared_ptr<Session>> sessions_;
    std::atomic<Session*> sessions_[MAX_FD];

    SessionManager() = default;

public:
    /**
     * @brief 获取或创建会话对象
     * @param fd 客户端套接字
     * @return std::shared_ptr<Session> 会话对象的智能指针
     */
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

    /**
     * @brief 移除会话记录
     * @param fd 客户端套接字
     */
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