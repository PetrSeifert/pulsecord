#include "Logging.h"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace drpc {

Logger::Logger(std::filesystem::path logPath) : logPath_(std::move(logPath)) {
    std::filesystem::create_directories(logPath_.parent_path());
    stream_.open(logPath_, std::ios::out | std::ios::app);
    if (!stream_) {
        throw std::runtime_error("Failed to open log file: " + logPath_.string());
    }
}

Logger::~Logger() {
    std::scoped_lock lock(mutex_);
    if (stream_) {
        stream_.flush();
    }
}

void Logger::Info(const std::string& message) {
    Write("INFO", message);
}

void Logger::Warn(const std::string& message) {
    Write("WARN", message);
}

void Logger::Error(const std::string& message) {
    Write("ERROR", message);
}

const std::filesystem::path& Logger::Path() const {
    return logPath_;
}

void Logger::Write(const char* level, const std::string& message) {
    std::scoped_lock lock(mutex_);

    const auto now = std::chrono::system_clock::now();
    const auto nowTime = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &nowTime);
#else
    localtime_r(&nowTime, &localTime);
#endif

    std::ostringstream line;
    line << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S") << " [" << level << "] " << message << '\n';

    stream_ << line.str();
    stream_.flush();
}

}  // namespace drpc
