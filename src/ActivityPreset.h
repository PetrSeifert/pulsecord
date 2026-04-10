#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace drpc {

enum class ActivityType {
    Playing,
    Watching,
    Listening,
    Streaming,
    CustomStatus,
    Competing,
    HangStatus,
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

ActivityType ParseActivityType(std::string_view value, ActivityType fallback = ActivityType::Playing);
std::string_view ActivityTypeToString(ActivityType value);

StatusDisplayType ParseStatusDisplayType(std::string_view value, StatusDisplayType fallback = StatusDisplayType::Name);
std::string_view StatusDisplayTypeToString(StatusDisplayType value);

}  // namespace drpc
