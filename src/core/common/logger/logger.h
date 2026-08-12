#pragma once

// ============================================================
// 自写日志器(零依赖)
// 功能:时间戳 + 日志级别 + 文件名:行号,同时输出控制台和文件
// 单例 + 互斥锁(线程安全),级别过滤(低于门槛直接丢弃)
// 用法:LOG_DEBUG/LOG_INFO/LOG_WARN/LOG_ERROR(文件底部宏定义)
//   LOG_INFO("value=%d", x);   ← 格式化输出,和 printf 一样
// ============================================================
#include <fstream>
#include <mutex>
#include <string>

namespace gateway
{
    // 日志级别:DEBUG < INFO < WARN < ERROR(数值越大越重要)
    enum class LogLevel
    {
        DEBUG = 0,
        INFO,
        WARN,
        ERROR
    };

    // 字符串 "DEBUG"/"INFO"/"WARN"/"ERROR" → LogLevel(不认识返回 INFO)
    LogLevel log_level_from_string(const std::string &level);

    // 简易日志器:时间戳 + 级别 + 文件名:行号,同时输出控制台和文件
    // 单例,线程安全(内部互斥锁)。用法见底部 LOG_* 宏。
    class Logger
    {
    public:
        // 单例访问点(全局唯一实例)
        static Logger &instance();

        // 初始化:filepath 为空则不写文件;min_level 是过滤门槛
        void init(const std::string &filepath, LogLevel min_level = LogLevel::DEBUG);
        // 运行中改级别(低于它的日志被丢弃)
        void set_level(LogLevel level);

        // 核心记录函数(通常用 LOG_* 宏调用,不用直接调)
        // file/line 是调用点(__FILE__/__LINE__),fmt 是 printf 风格格式串
        void log(LogLevel level, const char *file, int line, const char *fmt, ...)
#if defined(__GNUC__)
            __attribute__((format(printf, 5, 6))) // 让编译器检查格式串参数
#endif
            ;

    private:
        Logger() = default;                    // 私有构造:只能经 instance() 拿单例
        Logger(const Logger &) = delete;       // 禁止拷贝
        Logger &operator=(const Logger &) = delete;

        std::string time_str();                // "YYYY-MM-DD HH:MM:SS.mmm"
        static const char *level_name(LogLevel level); // "DEBUG" 等

        std::ofstream file_;                   // 日志文件(追加模式)
        std::mutex mutex_;                     // 保护输出,多线程安全
        LogLevel min_level_ = LogLevel::DEBUG; // 过滤门槛
    };

} // namespace gateway

// 日志宏:自动带上文件名和行号,调用方式同 printf
//   LOG_INFO("hello %d", 42);
#define LOG_DEBUG(...) \
    gateway::Logger::instance().log(gateway::LogLevel::DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) \
    gateway::Logger::instance().log(gateway::LogLevel::INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...) \
    gateway::Logger::instance().log(gateway::LogLevel::WARN, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) \
    gateway::Logger::instance().log(gateway::LogLevel::ERROR, __FILE__, __LINE__, __VA_ARGS__)
