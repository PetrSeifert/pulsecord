#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace drpc {

enum class ActivityType {
    Playing,
    Watching,
};

enum class StatusDisplayType {
    Name,
    State,
    Details,
};

struct ActivityButton {
    std::string label;
    std::string url;
};

struct ActivityAssets {
    std::string largeImage;
    std::string largeText;
    std::string largeUrl;
    std::string smallImage;
    std::string smallText;
    std::string smallUrl;
};

struct ActivityPreset {
    std::string name;
    std::string details;
    std::string detailsUrl;
    std::string state;
    std::string stateUrl;
    ActivityAssets assets;
    std::vector<ActivityButton> buttons;
    ActivityType type = ActivityType::Playing;
    StatusDisplayType statusDisplayType = StatusDisplayType::Name;
    bool showElapsedTime = true;
    std::optional<std::int64_t> startedAtUnixSeconds;
    std::optional<std::int64_t> endAtUnixSeconds;
};

}  // namespace drpc
