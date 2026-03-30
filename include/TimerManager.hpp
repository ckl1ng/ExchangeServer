#ifndef TIMERMANAGER_HPP
#define TIMERMANAGER_HPP

#include <iostream>
#include <queue>
#include <unordered_map>
#include <chrono>
#include <functional>

// 使用 C++11 的时间库
using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

struct TimerNode {
    int fd;                 // 哪个连接
    TimePoint expire;    // 什么时候过期
    
    // 为了让优先级队列把“最早过期”的放在最上面
    bool operator>(const TimerNode& other) const {
        return expire > other.expire;
    }
};

/**
 * @brief 向管理器添加或更新一个连接的超时计时器
 * 
 * @param fd 需要监控的客户端套接字
 * @param timeout_sec 相对超时时间（秒），过期后该连接将被踢出
 * 
 * @details 
 * 该方法采用“延迟删除”策略。如果在堆中已存在该 FD，则在 secretTable_ 中更新时间戳，
 * 堆顶节点过期时会进行二次校验。
 */
class TimerManager {
public:
    // 添加或更新定时器
    void addTimer(int fd, int timeout_sec) {
        auto now = Clock::now();
        auto expire = now + std::chrono::seconds(timeout_sec);
        minHeap_.push({fd, expire});
        secretTable_[fd] = expire; // 记录这个 fd 最新的过期时间
    }

    // 处理超时的连接
    void handleTimeouts(std::function<void(int)> closeCallback) {
        auto now = Clock::now();
        while (!minHeap_.empty()) {
            TimerNode top = minHeap_.top();
            
            // 1. 如果最上面的还没到期，后面的肯定也没到期，直接退出
            if (top.expire > now) break;

            // 2. 延迟删除优化（懒删除）：
            // 如果这个节点的过期时间不是最新的（说明中途更新过），就直接丢弃
            if (top.expire != secretTable_[top.fd]) {
                minHeap_.pop();
                continue;
            }
            closeCallback(top.fd);
            
            minHeap_.pop();
            secretTable_.erase(top.fd);
        }
    }

    void cancelTimer(int fd) {
        secretTable_.erase(fd);
    }

private:
    // 优先级队列（小顶堆）
    std::priority_queue<TimerNode, std::vector<TimerNode>, std::greater<TimerNode>> minHeap_;
    // 哈希表：记录每个 FD 最后一次更新的过期时间（用于验证堆顶是否有效）
    std::unordered_map<int, TimePoint> secretTable_;
};

#endif