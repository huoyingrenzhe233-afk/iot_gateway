#pragma once

#include <fstream>
#include <mutex>
#include <string>

namespace gateway {

enum class LogLevel
{
    DEBUG = 0,
    INFO,
    WARN,
    ERROR
};

// 简易日志器:时间戳 + 级别 + 文件名:行号,同时输出控制台和文件
// 单例,线程安全(内部互斥锁)。用法见底部 LOG_* 宏。
class Logger
{
public:
    static Logger &instance();

    // filepath 为空则不写文件
    void init(const std::string &filepath, LogLevel min_level = LogLevel::DEBUG);
    void set_level(LogLevel level);

    void log(LogLevel level, const char *file, int line, const char *fmt, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 5, 6)))
#endif
        ;

private:
    Logger() = default;
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    std::string time_str();
    static const char *level_name(LogLevel level);

    std::ofstream file_;
    std::mutex mutex_;
    LogLevel min_level_ = LogLevel::DEBUG;
};

} // namespace gateway

#define LOG_DEBUG(...) \
    gateway::Logger::instance().log(gateway::LogLevel::DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) \
    gateway::Logger::instance().log(gateway::LogLevel::INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...) \
    gateway::Logger::instance().log(gateway::LogLevel::WARN, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) \
    gateway::Logger::instance().log(gateway::LogLevel::ERROR, __FILE__, __LINE__, __VA_ARGS__)
