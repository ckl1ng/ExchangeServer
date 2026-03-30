#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

/**
 * @class ThreadPool
 * @brief 并发任务调度中心
 * 
 * 内部采用“生产者-消费者”模型，配合互斥锁与条件变量实现高效的任务分发。
 */
class ThreadPool {
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
    ThreadPool(int numThreads) : stop(false) {
        for (int i = 0; i < numThreads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        // 获取互斥锁，准备从队列中提取任务
                        std::unique_lock<std::mutex> lock(mtx);
                        // 等待条件变量：当池停止或队列不为空时被唤醒
                        cv.wait(lock, [this] { return stop || !tasks.empty(); });
                        
                        // 如果池已停止且任务已排空，则线程安全退出
                        if (stop && tasks.empty()) return;
                        
                        // 提取队列首部的任务（移动语义减少拷贝）
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    // 在锁范围外执行任务，提升并发度
                    task(); 
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
        {
            std::lock_guard<std::mutex> lock(mtx);
            tasks.emplace(std::move(task));
        }
        cv.notify_one();
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
        { 
            std::lock_guard<std::mutex> lock(mtx); 
            stop = true; 
        }
        cv.notify_all();
        for (std::thread &worker : workers) {
            worker.join();
        }
    }

private:
    std::vector<std::thread> workers;       /**< 工作线程容器 */
    std::queue<std::function<void()>> tasks; /**< 待处理任务队列 */
    std::mutex mtx;                         /**< 保护任务队列的互斥锁 */
    std::condition_variable cv;             /**< 用于线程同步的条件变量 */
    bool stop;                              /**< 线程池停止标志位 */
};

#endif