#ifndef LOGGER_HPP
#define LOGGER_HPP

#include "concurrentqueue.h"
#include <iostream>
#include <fstream>
#include <string>
#include <mutex>
#include <chrono>
#include <ctime>
#include <atomic>
#include <iomanip>
#include <thread>

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

struct LogItem {
    LogLevel level;
    std::string msg;
    std::chrono::system_clock::time_point timestamp;

    LogItem() = default;
    LogItem(LogLevel l, std::string m) : level(l), msg(std::move(m)), timestamp(std::chrono::system_clock::now()) {}
};

class Logger {
private:
    std::ofstream fileStream_;
    moodycamel::ConcurrentQueue<LogItem> queue_;
    std::thread worker_;
    std::atomic<bool> running_;
    Logger() : running_(false) {};
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    void processLogs() {
        const size_t batch_size = 500;
        LogItem items[batch_size];

        while (running_ || queue_.size_approx() > 0) {
            size_t count = queue_.try_dequeue_bulk(items, batch_size);

            if (count > 0) {
                for (size_t i = 0; i < count; i++) {
                    writeToSink(items[i]);
                }
                fileStream_.flush();
            }
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    void writeToSink(const LogItem& item) {
        std::string level_str;
        switch (item.level) {
            case LogLevel::DEBUG: level_str = "[DEBUG]"; break;
            case LogLevel::INFO:  level_str = "[INFO ]"; break;
            case LogLevel::WARN:  level_str = "[WARN ]"; break;
            case LogLevel::ERROR: level_str = "[ERROR]"; break;
        }

        auto ti = std::chrono::system_clock::to_time_t(item.timestamp);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(item.timestamp.time_since_epoch()) % 1000;

        struct tm timeinfo; 
        localtime_r(&ti, &timeinfo);

        std::stringstream ss;
        ss << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S") 
        << "." << std::setfill('0') << std::setw(3) << ms.count()
        << " " << level_str << " " << item.msg << "\n";

        std::string final_msg = ss.str();
        std::cout << final_msg;

        if (fileStream_.is_open()) {
            fileStream_ << final_msg;
        }
    }

public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    // 设置日志文件
    void init(const std::string& filename) {
        if (running_) return;
        
        fileStream_.open(filename, std::ios::app);
        if (!fileStream_.is_open()) {
            std::cerr << "无法打开日志文件" << filename << std::endl;
            return;
        }

        running_ = true;
        worker_ = std::thread(&Logger::processLogs, this);
    }

    /**
     * @brief 核心日志写入接口
     * 
     * 将格式化的日志行同步写入标准输出流及本地文件流。
     * 
     * @param level 日志等级
     * @param msg   日志包体内容
     * 
     * @details 
     * 格式标准：[YYYY-MM-DD HH:MM:SS.ms] [LEVEL] Message
     * 
     * @note 
     * - 性能说明：该函数包含文件 IO 刷盘操作 (flush)，频繁调用可能会对高吞吐业务产生微小延迟。
     * - 线程安全：受 mtx_ 保护。
     */
    void log(LogLevel level, const std::string& msg) {
        if (!running_) return;
        queue_.enqueue(LogItem(level, std::move(msg)));
    }

    ~Logger() {
        running_ = false;
        if (worker_.joinable()) worker_.join();
        if (fileStream_.is_open()) {
            fileStream_.close();
        }
    }
};

/**
 * @brief 便捷调用宏：输出普通信息
 * @param msg 日志内容字符串
 */
#define LOG_INFO(msg)  Logger::getInstance().log(LogLevel::INFO,  msg)

/**
 * @brief 便捷调用宏：输出错误信息
 * @param msg 日志内容字符串
 */
#define LOG_ERROR(msg) Logger::getInstance().log(LogLevel::ERROR, msg)

#define LOG_DEBUG(msg) Logger::getInstance().log(LogLevel::DEBUG, msg)

#endif