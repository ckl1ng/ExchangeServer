#ifndef TIMERMANAGER_HPP
#define TIMERMANAGER_HPP

#include <iostream>
#include <queue>
#include <unordered_map>
#include <chrono>
#include <functional>
#include <mutex> // 新增

using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

/**
 * @struct TimerNode
 * @brief 定时器节点
 */
struct TimerNode {
    int fd;                 
    TimePoint expire;    
    
    bool operator>(const TimerNode& other) const {
        return expire > other.expire;
    }
};

/**
 * @class TimerManager
 * @brief 定时器管理器
 */
class TimerManager {
public:
    // 添加定时器，指定文件描述符和超时时间（秒）
    void addTimer(int fd, int timeout_sec) {
        auto now = Clock::now();
        auto expire = now + std::chrono::seconds(timeout_sec);
        std::lock_guard<std::mutex> lock(mtx_);
        minHeap_.push({fd, expire});
        secretTable_[fd] = expire; 
    }

    // 处理超时事件，调用回调函数关闭连接
    void handleTimeouts(std::function<void(int)> closeCallback) {
        auto now = Clock::now();
        std::lock_guard<std::mutex> lock(mtx_);
        while (!minHeap_.empty()) {
            TimerNode top = minHeap_.top();
            
            if (top.expire > now) break;

            if (top.expire != secretTable_[top.fd]) {
                minHeap_.pop();
                continue;
            }
            closeCallback(top.fd);
            
            minHeap_.pop();
            secretTable_.erase(top.fd);
        }
    }

    // 取消定时器
    void cancelTimer(int fd) {
        std::lock_guard<std::mutex> lock(mtx_);
        secretTable_.erase(fd);
    }

private:
    std::mutex mtx_;
    std::priority_queue<TimerNode, std::vector<TimerNode>, std::greater<TimerNode>> minHeap_;
    std::unordered_map<int, TimePoint> secretTable_;
};

#endif