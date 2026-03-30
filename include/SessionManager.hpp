#include "Router.hpp"
#include <unordered_map>
#include <mutex>
#include <sys/epoll.h>
#include <functional>
#include <string>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
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
    std::unordered_map<int, std::shared_ptr<Session>> sessions_;
    std::mutex mtx_;

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

    std::shared_ptr<Session> getOrCreateSession(int fd) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (sessions_.find(fd) == sessions_.end()) {
            sessions_[fd] = std::make_shared<Session>(fd);
        }
        return sessions_[fd];
    }

    /**
     * @brief 移除会话记录
     * @param fd 客户端套接字
     */
    void removeSession(int fd) {
        std::lock_guard<std::mutex> lock(mtx_);
        sessions_.erase(fd);
    }

    void cleanConnection(int client_fd) {
        removeSession(client_fd);
        close(client_fd);
        LOG_INFO("连接 " + std::to_string(client_fd) + " 已关闭");
    }

    bool hasSession(int client_fd) {
        return sessions_.count(client_fd);
    }
};
