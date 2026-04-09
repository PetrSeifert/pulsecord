#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace drpc {

class Logger {
public:
    explicit Logger(std::filesystem::path logPath);
    ~Logger();

    void Info(const std::string& message);
    void Warn(const std::string& message);
    void Error(const std::string& message);

    const std::filesystem::path& Path() const;

private:
    void Write(const char* level, const std::string& message);

    std::filesystem::path logPath_;
    std::ofstream stream_;
    std::mutex mutex_;
};

}  // namespace drpc
