#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <string>
#include <fstream>
#include <mutex>

namespace agent_cli {

enum class LogLevel {
    OFF = 0,
    ERROR = 1,
    WARN = 2,
    INFO = 3,
    DEBUG = 4
};

class Logger {
public:
    static Logger& instance();

    void init(const std::string& log_file, LogLevel level);
    void log(LogLevel level, const std::string& message);

    void error(const std::string& message) { log(LogLevel::ERROR, message); }
    void warn(const std::string& message) { log(LogLevel::WARN, message); }
    void info(const std::string& message) { log(LogLevel::INFO, message); }
    void debug(const std::string& message) { log(LogLevel::DEBUG, message); }

    bool is_debug_enabled() const { return level_ >= LogLevel::DEBUG; }
private:
    Logger() = default;

    std::mutex mutex_;
    std::ofstream log_file_;
    LogLevel level_ = LogLevel::INFO;
    bool initialized_ = false;

    const char* level_to_string(LogLevel level);
    std::string get_timestamp();
};

// Convenience macros
#define LOG_ERROR(msg) Logger::instance().error(msg)
#define LOG_WARN(msg) Logger::instance().warn(msg)
#define LOG_INFO(msg) Logger::instance().info(msg)
#define LOG_DEBUG(msg) Logger::instance().debug(msg)

} // namespace agent_cli

#endif // LOGGER_HPP
