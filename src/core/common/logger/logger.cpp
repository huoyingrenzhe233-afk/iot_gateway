#include "logger.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace gateway
{

    Logger &Logger::instance()
    {
        static Logger inst; // C++11 起局部静态变量初始化线程安全,天然单例
        return inst;
    }

    void Logger::init(const std::string &filepath, LogLevel min_level)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        min_level_ = min_level;
        if (!filepath.empty())
        {
            file_.open(filepath, std::ios::app); // 追加模式,重启不清空
        }
    }

    void Logger::set_level(LogLevel level)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        min_level_ = level;
    }

    const char *Logger::level_name(LogLevel level)
    {
        static const char *names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
        int idx = static_cast<int>(level);
        return (idx >= 0 && idx <= static_cast<int>(LogLevel::ERROR)) ? names[idx] : "?";
    }

    std::string Logger::time_str()
    {
        char buf[40];
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_r(&t, &tm);
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        char out[48];
        std::snprintf(out, sizeof(out), "%s.%03d", buf, static_cast<int>(ms.count()));
        return out;
    }

    void Logger::log(LogLevel level, const char *file, int line, const char *fmt, ...)
    {
        // 级别检查 + 输出 全程在锁内:保证 min_level_ 的读写都被
        // mutex_ 保护,避免与 set_level() 并发时的数据竞争(线程安全)
        std::lock_guard<std::mutex> lock(mutex_);
        if (level < min_level_)
        {
            return; // 低于门槛直接丢弃,连格式化都不做
        }

        // 格式化消息体
        va_list args;
        va_start(args, fmt);
        char msg[1024];
        std::vsnprintf(msg, sizeof(msg), fmt, args);
        va_end(args);

        // 只取文件名,不要全路径
        const char *short_file = std::strrchr(file, '/');
        short_file = short_file ? short_file + 1 : file;

        char line_buf[1280];
        std::snprintf(line_buf, sizeof(line_buf), "[%s][%s][%s:%d] %s",
                      time_str().c_str(), level_name(level), short_file, line, msg);

        // 控制台和文件原子输出,防多线程串行
        std::printf("%s\n", line_buf);
        std::fflush(stdout);
        if (file_.is_open())
        {
            file_ << line_buf << std::endl;
        }
    }
    LogLevel log_level_from_string(const std::string &level)
    {
        if (level == "DEBUG")
            return LogLevel::DEBUG;
        if (level == "WARN")
            return LogLevel::WARN;
        if (level == "ERROR")
            return LogLevel::ERROR;
        return LogLevel::INFO; // 兜底:不认识就 INFO
    }
} // namespace gateway
