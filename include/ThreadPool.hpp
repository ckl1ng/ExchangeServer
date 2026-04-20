#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <atomic>
#include "blockingconcurrentqueue.h"
#include <functional>

/**
 * @class ThreadPool
 * @brief 并发任务调度中心
 * 
 * 内部采用“生产者-消费者”模型，配合互斥锁与条件变量实现高效的任务分发。
 */
class ThreadPool {
private:
    std::vector<std::thread> workers_;
    moodycamel::BlockingConcurrentQueue<std::function<void()>> tasks_;
    std::atomic<bool> running_;

public:
    // 构造函数：初始化线程池并启动工作线程
    ThreadPool(int numThreads) : running_(true) {
        for (int i = 0; i < numThreads; ++i) {
            workers_.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    
                    tasks_.wait_dequeue(task);

                    if (!running_ && !task) {
                        return;
                    }

                    if (task) {
                        task();
                    }
                } 
            });
        }
    }

    // 提交任务到线程池
    void enqueue(std::function<void()> task) {
        tasks_.enqueue(std::move(task));
    }
    
    // 析构函数：停止线程池并等待所有线程完成
    ~ThreadPool() {
        running_ = false;
        for (size_t i = 0; i < workers_.size(); i++) {
            tasks_.enqueue(nullptr);
        }

        for (std::thread &workers_ : workers_) {
            if (workers_.joinable()) workers_.join();
        }
    }
};

#endif