#pragma once

#include "ActivityPreset.h"

#include <filesystem>
#include <string>
#include <vector>

namespace drpc {

enum class ActivityMode {
    Mock,
    Browser,
};

struct BrowserDetectionConfig {
    bool enabled = true;
    unsigned int staleAfterMs = 45000;
    std::string fallbackPreset = "Idle";
    std::vector<std::string> supportedSites = {"crunchyroll", "hidive", "netflix", "9anime"};
};

struct DiscordAuthConfig {
    bool enabled = true;
    bool useDeviceAuth = false;
    bool autoAuthenticate = true;
    bool autoRefresh = true;
    unsigned int refreshLeewaySeconds = 86400;
    std::string redirectUri = "http://127.0.0.1/callback";
    std::string scopes;
    std::string tokenStoragePath = "discord-auth.json";
};

struct AppConfig {
    std::string applicationId;
    unsigned int updateIntervalMs = 15000;
    ActivityMode activityMode = ActivityMode::Browser;
    BrowserDetectionConfig browserDetection;
    DiscordAuthConfig discordAuth;
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
