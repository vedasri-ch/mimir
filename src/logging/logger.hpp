#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <string_view>

namespace mimir {

enum class LogLevel : int { DEBUG = 0, INFO = 1, WARN = 2, ERR = 3 };

class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    void set_level(LogLevel level) { min_level_.store(level, std::memory_order_relaxed); }

    template <typename... Args>
    void log(LogLevel level, const char* file, int line, const char* fmt, Args&&... args) {
        if (level < min_level_.load(std::memory_order_relaxed)) return;

        char msg[2048];
        std::snprintf(msg, sizeof(msg), fmt, std::forward<Args>(args)...);

        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        char timebuf[32];
        std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));

        const char* level_str = level_to_str(level);
        std::lock_guard<std::mutex> lock(mu_);
        std::fprintf(stderr, "[%s] [%s] %s:%d - %s\n", timebuf, level_str, file, line, msg);
    }

private:
    Logger() = default;
    std::atomic<LogLevel> min_level_{LogLevel::INFO};
    std::mutex mu_;

    static const char* level_to_str(LogLevel l) {
        switch (l) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO ";
            case LogLevel::WARN:  return "WARN ";
            case LogLevel::ERR:   return "ERROR";
        }
        return "?????";
    }
};

} // namespace mimir

#define LOG_DEBUG(fmt, ...) \
    mimir::Logger::instance().log(mimir::LogLevel::DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) \
    mimir::Logger::instance().log(mimir::LogLevel::INFO,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) \
    mimir::Logger::instance().log(mimir::LogLevel::WARN,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERR(fmt, ...) \
    mimir::Logger::instance().log(mimir::LogLevel::ERR,   __FILE__, __LINE__, fmt, ##__VA_ARGS__)
