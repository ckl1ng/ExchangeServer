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
    /**
     * @brief 构造函数：初始化线程池并启动工作线程
     * 
     * @param numThreads 池中保持的工作线程总数。
     * 
     * @details 
     * 每个工作线程在初始化后都会进入一个无限循环，尝试从任务队列中提取并执行任务。
     * 如果队列为空，线程将通过条件变量挂起，不消耗 CPU 周期。
     */
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

    /**
     * @brief 向线程池提交一个异步任务
     * 
     * @param task 一个符合 `void()` 签名的可执行对象（如 Lambda 表达式、函数指针等）。
     * 
     * @note 
     * 1. 任务会被推入 FIFO 队列。
     * 2. 提交后会自动通过 `cv.notify_one()` 唤醒一个正在睡眠的工作线程。
     */
    void enqueue(std::function<void()> task) {
        tasks_.enqueue(std::move(task));
    }

    /**
     * @brief 析构函数：优雅地停止所有工作线程
     * 
     * @details 
     * 1. 将停止标志 `stop` 置为 true。
     * 2. 调用 `notify_all()` 唤醒所有正在阻塞等待任务的线程。
     * 3. 阻塞等待所有线程执行完当前任务并退出 (`join`)。
     */
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