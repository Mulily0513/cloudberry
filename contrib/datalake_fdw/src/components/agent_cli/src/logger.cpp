#include <logger.hpp>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace agent_cli {

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

void Logger::init(const std::string& log_file, LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);

    level_ = level;

    if (!log_file.empty()) {
        log_file_.open(log_file, std::ios::app);
        if (!log_file_.is_open()) {
            std::cerr << "Failed to open log file: " << log_file << std::endl;
        }
    }

    initialized_ = true;
}

void Logger::log(LogLevel level, const std::string& message) {
    if (!initialized_ || level > level_) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    std::string log_line = get_timestamp() + " [" + level_to_string(level) + "] " + message;

    if (log_file_.is_open()) {
        log_file_ << log_line << std::endl;
        log_file_.flush();
    } else {
        std::cout << log_line << std::endl;
    }
}

const char* Logger::level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::WARN: return "WARN";
        case LogLevel::INFO: return "INFO";
        case LogLevel::DEBUG: return "DEBUG";
        default: return "UNKNOWN";
    }
}

std::string Logger::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    
    return oss.str();
}

} // namespace agent_cli
