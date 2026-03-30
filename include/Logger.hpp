#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <iostream>
#include <fstream>
#include <string>
#include <mutex>
#include <chrono>
#include <ctime>
#include <iomanip>

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

class Logger {
public:
    // 【单例模式】通过这个静态函数获取唯一的对象
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    // 设置日志文件
    void init(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (fileStream_.is_open()) fileStream_.close();
        fileStream_.open(filename, std::ios::app); // 追加模式打开
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
        std::lock_guard<std::mutex> lock(mtx_); // 保证多线程打印不乱序

        std::string levelStr;
        switch (level) {
            case LogLevel::DEBUG: levelStr = "[DEBUG]"; break;
            case LogLevel::INFO:  levelStr = "[INFO ]"; break;
            case LogLevel::WARN:  levelStr = "[WARN ]"; break;
            case LogLevel::ERROR: levelStr = "[ERROR]"; break;
        }

        // 获取当前时间
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        // 格式化输出到控制台和文件
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S") 
           << "." << std::setfill('0') << std::setw(3) << ms.count()
           << " " << levelStr << " " << msg << std::endl;

        std::cout << ss.str();
        if (fileStream_.is_open()) {
            fileStream_ << ss.str();
            fileStream_.flush(); // 确保立刻写入硬盘
        }
    }

private:
    Logger() {} // 私有构造函数，防止外部 new
    ~Logger() { if (fileStream_.is_open()) fileStream_.close(); }
    
    std::ofstream fileStream_;
    std::mutex mtx_;
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

#endif