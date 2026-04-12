#include "ActivityPreset.h"

namespace drpc {

bool IsDiscordUrlLengthValid(std::string_view value) {
    return value.empty() || value.size() <= kDiscordUrlMaxLength;
}

ActivityPreset SanitizeDiscordActivityPreset(const ActivityPreset& preset) {
    ActivityPreset sanitized = preset;

    if (!IsDiscordUrlLengthValid(sanitized.detailsUrl)) {
        sanitized.detailsUrl.clear();
    }
    if (!IsDiscordUrlLengthValid(sanitized.stateUrl)) {
        sanitized.stateUrl.clear();
    }
    if (!IsDiscordUrlLengthValid(sanitized.assets.largeUrl)) {
        sanitized.assets.largeUrl.clear();
    }
    if (!IsDiscordUrlLengthValid(sanitized.assets.smallUrl)) {
        sanitized.assets.smallUrl.clear();
    }

    sanitized.buttons.erase(
        std::remove_if(
            sanitized.buttons.begin(),
            sanitized.buttons.end(),
            [](const ActivityButton& button) { return !IsDiscordUrlLengthValid(button.url); }),
        sanitized.buttons.end());

    return sanitized;
}

ActivityType ParseActivityType(std::string_view value, ActivityType fallback) {
    if (value == "playing") {
        return ActivityType::Playing;
    }
    if (value == "watching") {
        return ActivityType::Watching;
    }
    if (value == "listening") {
        return ActivityType::Listening;
    }
    if (value == "streaming") {
        return ActivityType::Streaming;
    }
    if (value == "competing") {
        return ActivityType::Competing;
    }

    return fallback;
}

std::string_view ActivityTypeToString(ActivityType value) {
    switch (value) {
    case ActivityType::Watching:
        return "watching";
    case ActivityType::Listening:
        return "listening";
    case ActivityType::Streaming:
        return "streaming";
    case ActivityType::Competing:
        return "competing";
    case ActivityType::Playing:
    default:
        return "playing";
    }
}

StatusDisplayType ParseStatusDisplayType(std::string_view value, StatusDisplayType fallback) {
    if (value == "name") {
        return StatusDisplayType::Name;
    }
    if (value == "state") {
        return StatusDisplayType::State;
    }
    if (value == "details") {
        return StatusDisplayType::Details;
    }

    return fallback;
}

std::string_view StatusDisplayTypeToString(StatusDisplayType value) {
    switch (value) {
    case StatusDisplayType::State:
        return "state";
    case StatusDisplayType::Details:
        return "details";
    case StatusDisplayType::Name:
    default:
        return "name";
    }
}

}  // namespace drpc
