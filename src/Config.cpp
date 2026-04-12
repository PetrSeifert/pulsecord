#include "Config.h"

#include <fstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>
#include <windows.h>

namespace drpc {
namespace {

using json = nlohmann::json;

ActivityMode ParseActivityMode(const std::string& value) {
    if (value == "mock") {
        return ActivityMode::Mock;
    }

    return ActivityMode::Browser;
}

std::string ToConfigString(ActivityMode value) {
    switch (value) {
    case ActivityMode::Mock:
        return "mock";
    case ActivityMode::Browser:
    default:
        return "browser";
    }
}

ActivityPreset ParsePreset(const json& item) {
    ActivityPreset preset;
    preset.name = item.value("name", "Unnamed");
    preset.details = item.value("details", "");
    preset.detailsUrl = item.value("detailsUrl", "");
    preset.state = item.value("state", "");
    preset.stateUrl = item.value("stateUrl", "");
    preset.type = ParseActivityType(item.value("type", "playing"));
    preset.statusDisplayType = ParseStatusDisplayType(item.value("statusDisplayType", "name"));
    preset.showElapsedTime = item.value("showElapsedTime", true);

    if (item.contains("assets") && item["assets"].is_object()) {
        const auto& assets = item["assets"];
        preset.assets.largeImage = assets.value("largeImage", "");
        preset.assets.largeText = assets.value("largeText", "");
        preset.assets.largeUrl = assets.value("largeUrl", "");
        preset.assets.smallImage = assets.value("smallImage", "");
        preset.assets.smallText = assets.value("smallText", "");
        preset.assets.smallUrl = assets.value("smallUrl", "");
    }

    if (item.contains("buttons") && item["buttons"].is_array()) {
        for (const auto& buttonJson : item["buttons"]) {
            ActivityButton button;
            button.label = buttonJson.value("label", "");
            button.url = buttonJson.value("url", "");
            if (!button.label.empty() && !button.url.empty()) {
                preset.buttons.push_back(std::move(button));
            }
        }
    }

    return preset;
}

json SerializePreset(const ActivityPreset& preset) {
    json item;
    item["name"] = preset.name;
    item["details"] = preset.details;
    item["detailsUrl"] = preset.detailsUrl;
    item["state"] = preset.state;
    item["stateUrl"] = preset.stateUrl;
    item["type"] = std::string(ActivityTypeToString(preset.type));
    item["statusDisplayType"] = std::string(StatusDisplayTypeToString(preset.statusDisplayType));
    item["showElapsedTime"] = preset.showElapsedTime;
    item["assets"] = {
        {"largeImage", preset.assets.largeImage},
        {"largeText", preset.assets.largeText},
        {"largeUrl", preset.assets.largeUrl},
        {"smallImage", preset.assets.smallImage},
        {"smallText", preset.assets.smallText},
        {"smallUrl", preset.assets.smallUrl},
    };

    item["buttons"] = json::array();
    for (const auto& button : preset.buttons) {
        item["buttons"].push_back({
            {"label", button.label},
            {"url", button.url},
        });
    }

    return item;
}

}  // namespace

AppConfig ConfigLoader::LoadOrCreate(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        WriteDefault(path);
        return MakeDefault();
    }

    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("Failed to open config file: " + path.string());
    }

    json root;
    stream >> root;

    AppConfig config;
    config.applicationId = root.value("applicationId", "");
    config.updateIntervalMs = root.value("updateIntervalMs", 15000U);
    config.activityMode = ParseActivityMode(root.value("activityMode", "browser"));

    if (root.contains("browserDetection") && root["browserDetection"].is_object()) {
        const auto& browserDetection = root["browserDetection"];
        config.browserDetection.enabled = browserDetection.value("enabled", true);
        config.browserDetection.staleAfterMs = browserDetection.value("staleAfterMs", 45000U);
        config.browserDetection.fallbackPreset = browserDetection.value("fallbackPreset", "Idle");
        if (browserDetection.contains("supportedSites") && browserDetection["supportedSites"].is_array()) {
            config.browserDetection.supportedSites.clear();
            for (const auto& site : browserDetection["supportedSites"]) {
                if (site.is_string()) {
                    config.browserDetection.supportedSites.push_back(site.get<std::string>());
                }
            }
        }
    }

    if (root.contains("discordAuth") && root["discordAuth"].is_object()) {
        const auto& discordAuth = root["discordAuth"];
        config.discordAuth.enabled = discordAuth.value("enabled", true);
        config.discordAuth.useDeviceAuth = discordAuth.value("useDeviceAuth", false);
        config.discordAuth.autoAuthenticate = discordAuth.value("autoAuthenticate", true);
        config.discordAuth.autoRefresh = discordAuth.value("autoRefresh", true);
        config.discordAuth.refreshLeewaySeconds = discordAuth.value("refreshLeewaySeconds", 86400U);
        config.discordAuth.redirectUri = discordAuth.value("redirectUri", "http://127.0.0.1/callback");
        config.discordAuth.scopes = discordAuth.value("scopes", "");
        config.discordAuth.tokenStoragePath = discordAuth.value("tokenStoragePath", "discord-auth.json");
    }

    if (root.contains("presets") && root["presets"].is_array()) {
        for (const auto& item : root["presets"]) {
            config.presets.push_back(ParsePreset(item));
        }
    }

    if (config.presets.empty()) {
        config = MakeDefault();
        WriteDefault(path);
    }

    return config;
}

AppConfig ConfigLoader::MakeDefault() {
    AppConfig config;
    config.applicationId = "1491798009942507712";
    config.updateIntervalMs = 15000;
    config.activityMode = ActivityMode::Browser;
    config.browserDetection = BrowserDetectionConfig{};
    config.discordAuth = DiscordAuthConfig{};

    config.presets = {
        ActivityPreset{
            .name = "Coding",
            .details = "Working on drpc",
            .detailsUrl = "",
            .state = "Writing C++",
            .stateUrl = "",
            .assets = ActivityAssets{"coding", "Coding", "", "keyboard", "Focused", ""},
            .buttons = {ActivityButton{"Source", "https://github.com/"}},
            .type = ActivityType::Playing,
            .statusDisplayType = StatusDisplayType::Details,
            .showElapsedTime = true,
        },
        ActivityPreset{
            .name = "Gaming",
            .details = "Playing something fun",
            .detailsUrl = "",
            .state = "In a session",
            .stateUrl = "",
            .assets = ActivityAssets{"gaming", "Gaming", "", "controller", "Relaxing", ""},
            .buttons = {},
            .type = ActivityType::Playing,
            .statusDisplayType = StatusDisplayType::Name,
            .showElapsedTime = true,
        },
        ActivityPreset{
            .name = "Watching Video",
            .details = "Watching a video",
            .detailsUrl = "",
            .state = "Full screen",
            .stateUrl = "",
            .assets = ActivityAssets{"video", "Watching", "", "play", "Now playing", ""},
            .buttons = {},
            .type = ActivityType::Watching,
            .statusDisplayType = StatusDisplayType::State,
            .showElapsedTime = true,
        },
        ActivityPreset{
            .name = "Idle",
            .details = "Away from keyboard",
            .detailsUrl = "",
            .state = "Be right back",
            .stateUrl = "",
            .assets = ActivityAssets{"idle", "Idle", "", "moon", "AFK", ""},
            .buttons = {},
            .type = ActivityType::Playing,
            .statusDisplayType = StatusDisplayType::Name,
            .showElapsedTime = false,
        },
    };

    return config;
}

void ConfigLoader::WriteDefault(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());

    const auto config = MakeDefault();
    json root;
    root["applicationId"] = config.applicationId;
    root["updateIntervalMs"] = config.updateIntervalMs;
    root["activityMode"] = ToConfigString(config.activityMode);
    root["browserDetection"] = {
        {"enabled", config.browserDetection.enabled},
        {"staleAfterMs", config.browserDetection.staleAfterMs},
        {"fallbackPreset", config.browserDetection.fallbackPreset},
        {"supportedSites", config.browserDetection.supportedSites},
    };
    root["discordAuth"] = {
        {"enabled", config.discordAuth.enabled},
        {"useDeviceAuth", config.discordAuth.useDeviceAuth},
        {"autoAuthenticate", config.discordAuth.autoAuthenticate},
        {"autoRefresh", config.discordAuth.autoRefresh},
        {"refreshLeewaySeconds", config.discordAuth.refreshLeewaySeconds},
        {"redirectUri", config.discordAuth.redirectUri},
        {"scopes", config.discordAuth.scopes},
        {"tokenStoragePath", config.discordAuth.tokenStoragePath},
    };
    root["presets"] = json::array();

    for (const auto& preset : config.presets) {
        root["presets"].push_back(SerializePreset(preset));
    }

    std::ofstream stream(path);
    if (!stream) {
        throw std::runtime_error("Failed to write config file: " + path.string());
    }

    stream << root.dump(2) << '\n';
}

std::wstring ToWide(std::string_view value) {
    if (value.empty()) {
        return {};
    }

    const auto required = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }

    std::wstring wide(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), wide.data(), required);
    return wide;
}

std::string ToUtf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }

    const auto required = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }

    std::string utf8(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), utf8.data(), required, nullptr, nullptr);
    return utf8;
}

}  // namespace drpc
