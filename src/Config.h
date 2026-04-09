#pragma once

#include "ActivityPreset.h"

#include <filesystem>
#include <string>
#include <vector>

namespace drpc {

struct AppConfig {
    std::string applicationId;
    unsigned int updateIntervalMs = 15000;
    std::vector<ActivityPreset> presets;
};

class ConfigLoader {
public:
    static AppConfig LoadOrCreate(const std::filesystem::path& path);
    static AppConfig MakeDefault();
    static void WriteDefault(const std::filesystem::path& path);
};

std::wstring ToWide(std::string_view value);
std::string ToUtf8(std::wstring_view value);

}  // namespace drpc
