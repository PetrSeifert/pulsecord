#include "BrowserActivityProtocol.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

namespace drpc {
namespace {

using json = nlohmann::json;

std::string Trim(std::string_view value) {
    std::string result(value);
    result.erase(result.begin(), std::find_if(result.begin(), result.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    result.erase(std::find_if(result.rbegin(), result.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), result.end());
    return result;
}

bool ReadOptionalInteger(const json& root, const char* key, std::optional<std::int64_t>& output, std::string& error) {
    if (!root.contains(key) || root[key].is_null()) {
        output.reset();
        return true;
    }

    if (!root[key].is_number_integer()) {
        error = std::string("Expected integer field: ") + key;
        return false;
    }

    output = root[key].get<std::int64_t>();
    return true;
}

ActivityType ParseActivityType(const std::string& value) {
    if (value == "watching") {
        return ActivityType::Watching;
    }

    return ActivityType::Playing;
}

std::string ActivityTypeToString(ActivityType value) {
    switch (value) {
    case ActivityType::Watching:
        return "watching";
    case ActivityType::Playing:
    default:
        return "playing";
    }
}

StatusDisplayType ParseStatusDisplayType(const std::string& value) {
    if (value == "state") {
        return StatusDisplayType::State;
    }
    if (value == "details") {
        return StatusDisplayType::Details;
    }
    return StatusDisplayType::Name;
}

std::string StatusDisplayTypeToString(StatusDisplayType value) {
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

bool ParseActivityButton(const json& item, ActivityButton& button, std::string& error) {
    if (!item.is_object()) {
        error = "activityCard.buttons entries must be objects.";
        return false;
    }

    if (!item.contains("label") || !item["label"].is_string() || !item.contains("url") || !item["url"].is_string()) {
        error = "activityCard.buttons entries require string label and url fields.";
        return false;
    }

    button.label = Trim(item["label"].get<std::string>());
    button.url = Trim(item["url"].get<std::string>());
    return true;
}

bool ParseActivityCard(const json& item, ActivityPreset& preset, std::string& error) {
    if (!item.is_object()) {
        error = "activityCard must be an object.";
        return false;
    }

    preset = ActivityPreset{};
    preset.name = Trim(item.value("name", ""));
    preset.details = Trim(item.value("details", ""));
    preset.detailsUrl = Trim(item.value("detailsUrl", ""));
    preset.state = Trim(item.value("state", ""));
    preset.stateUrl = Trim(item.value("stateUrl", ""));
    preset.type = ParseActivityType(item.value("type", "watching"));
    preset.statusDisplayType = ParseStatusDisplayType(item.value("statusDisplayType", "name"));
    preset.showElapsedTime = item.value("showElapsedTime", true);

    if (item.contains("assets")) {
        if (!item["assets"].is_object()) {
            error = "activityCard.assets must be an object.";
            return false;
        }

        const auto& assets = item["assets"];
        preset.assets.largeImage = Trim(assets.value("largeImage", ""));
        preset.assets.largeText = Trim(assets.value("largeText", ""));
        preset.assets.largeUrl = Trim(assets.value("largeUrl", ""));
        preset.assets.smallImage = Trim(assets.value("smallImage", ""));
        preset.assets.smallText = Trim(assets.value("smallText", ""));
        preset.assets.smallUrl = Trim(assets.value("smallUrl", ""));
    }

    if (item.contains("buttons")) {
        if (!item["buttons"].is_array()) {
            error = "activityCard.buttons must be an array.";
            return false;
        }

        for (const auto& buttonJson : item["buttons"]) {
            ActivityButton button;
            if (!ParseActivityButton(buttonJson, button, error)) {
                return false;
            }
            if (!button.label.empty() && !button.url.empty()) {
                preset.buttons.push_back(std::move(button));
            }
        }
    }

    if (!ReadOptionalInteger(item, "startedAtUnixSeconds", preset.startedAtUnixSeconds, error) ||
        !ReadOptionalInteger(item, "endAtUnixSeconds", preset.endAtUnixSeconds, error)) {
        return false;
    }

    return true;
}

json SerializeActivityCard(const ActivityPreset& preset) {
    json card;
    card["name"] = preset.name;
    card["details"] = preset.details;
    card["detailsUrl"] = preset.detailsUrl;
    card["state"] = preset.state;
    card["stateUrl"] = preset.stateUrl;
    card["type"] = ActivityTypeToString(preset.type);
    card["statusDisplayType"] = StatusDisplayTypeToString(preset.statusDisplayType);
    card["showElapsedTime"] = preset.showElapsedTime;
    card["startedAtUnixSeconds"] = preset.startedAtUnixSeconds.has_value() ? json(preset.startedAtUnixSeconds.value()) : json(nullptr);
    card["endAtUnixSeconds"] = preset.endAtUnixSeconds.has_value() ? json(preset.endAtUnixSeconds.value()) : json(nullptr);
    card["assets"] = {
        {"largeImage", preset.assets.largeImage},
        {"largeText", preset.assets.largeText},
        {"largeUrl", preset.assets.largeUrl},
        {"smallImage", preset.assets.smallImage},
        {"smallText", preset.assets.smallText},
        {"smallUrl", preset.assets.smallUrl},
    };

    card["buttons"] = json::array();
    for (const auto& button : preset.buttons) {
        card["buttons"].push_back({
            {"label", button.label},
            {"url", button.url},
        });
    }

    return card;
}

}  // namespace

std::string BrowserPlaybackStateToString(BrowserPlaybackState state) {
    switch (state) {
    case BrowserPlaybackState::Playing:
        return "playing";
    case BrowserPlaybackState::Paused:
        return "paused";
    case BrowserPlaybackState::Idle:
    default:
        return "idle";
    }
}

std::string BrowserActivityDispositionToString(BrowserActivityDisposition disposition) {
    switch (disposition) {
    case BrowserActivityDisposition::Publish:
        return "publish";
    case BrowserActivityDisposition::Sticky:
        return "sticky";
    case BrowserActivityDisposition::Clear:
    default:
        return "clear";
    }
}

std::string BrowserActivitySnapshot::IdentityKey() const {
    std::ostringstream key;
    key << browser << '|'
        << host << '|'
        << siteId << '|'
        << url << '|'
        << pageTitle << '|'
        << BrowserPlaybackStateToString(playbackState) << '|'
        << BrowserActivityDispositionToString(activityDisposition);

    if (activityCard.has_value()) {
        key << '|'
            << activityCard->name << '|'
            << activityCard->details << '|'
            << activityCard->state << '|'
            << (activityCard->startedAtUnixSeconds.has_value() ? std::to_string(activityCard->startedAtUnixSeconds.value()) : "") << '|'
            << (activityCard->endAtUnixSeconds.has_value() ? std::to_string(activityCard->endAtUnixSeconds.value()) : "");
    }

    return key.str();
}

bool ParseBrowserActivityMessage(std::string_view message, BrowserActivitySnapshot& snapshot, std::string& error) {
    if (message.size() > kMaxBrowserActivityMessageBytes) {
        error = "Browser activity message exceeded the maximum size.";
        return false;
    }

    json root;
    try {
        root = json::parse(message);
    } catch (const std::exception& ex) {
        error = std::string("Invalid browser activity JSON: ") + ex.what();
        return false;
    }

    if (!root.is_object()) {
        error = "Browser activity payload must be a JSON object.";
        return false;
    }

    if (!root.contains("schemaVersion") || !root["schemaVersion"].is_number_integer()) {
        error = "Missing integer schemaVersion.";
        return false;
    }

    snapshot = BrowserActivitySnapshot{};
    snapshot.schemaVersion = root["schemaVersion"].get<int>();
    if (snapshot.schemaVersion != kBrowserActivitySchemaVersion) {
        error = "Unsupported browser activity schemaVersion.";
        return false;
    }

    auto readRequiredString = [&](const char* key, std::string& target) -> bool {
        if (!root.contains(key) || !root[key].is_string()) {
            error = std::string("Missing string field: ") + key;
            return false;
        }

        target = Trim(root[key].get<std::string>());
        return true;
    };

    std::string activityDispositionValue;
    if (!readRequiredString("browser", snapshot.browser) ||
        !readRequiredString("url", snapshot.url) ||
        !readRequiredString("host", snapshot.host) ||
        !readRequiredString("pageTitle", snapshot.pageTitle) ||
        !readRequiredString("activityDisposition", activityDispositionValue)) {
        return false;
    }

    snapshot.siteId = Trim(root.value("siteId", ""));

    if (root.contains("tabId") && !root["tabId"].is_null()) {
        if (!root["tabId"].is_number_integer()) {
            error = "tabId must be an integer.";
            return false;
        }
        snapshot.tabId = root["tabId"].get<int>();
    }

    const auto playbackState = root.value("playbackState", "idle");
    if (playbackState == "playing") {
        snapshot.playbackState = BrowserPlaybackState::Playing;
    } else if (playbackState == "paused") {
        snapshot.playbackState = BrowserPlaybackState::Paused;
    } else if (playbackState == "idle") {
        snapshot.playbackState = BrowserPlaybackState::Idle;
    } else {
        error = "Unsupported playbackState value.";
        return false;
    }

    const auto activityDisposition = Trim(activityDispositionValue);
    if (activityDisposition == "publish") {
        snapshot.activityDisposition = BrowserActivityDisposition::Publish;
    } else if (activityDisposition == "sticky") {
        snapshot.activityDisposition = BrowserActivityDisposition::Sticky;
    } else if (activityDisposition == "clear") {
        snapshot.activityDisposition = BrowserActivityDisposition::Clear;
    } else {
        error = "Unsupported activityDisposition value.";
        return false;
    }

    if (root.contains("activityCard") && !root["activityCard"].is_null()) {
        ActivityPreset activityCard;
        if (!ParseActivityCard(root["activityCard"], activityCard, error)) {
            return false;
        }
        snapshot.activityCard = std::move(activityCard);
    } else {
        snapshot.activityCard.reset();
    }

    if (!root.contains("sentAtUnixMs") || !root["sentAtUnixMs"].is_number_integer()) {
        error = "Missing integer sentAtUnixMs.";
        return false;
    }
    snapshot.sentAtUnixMs = root["sentAtUnixMs"].get<std::int64_t>();

    return true;
}

std::string SerializeBrowserActivityMessage(const BrowserActivitySnapshot& snapshot) {
    json root;
    root["schemaVersion"] = snapshot.schemaVersion;
    root["browser"] = snapshot.browser;
    root["tabId"] = snapshot.tabId.has_value() ? json(snapshot.tabId.value()) : json(nullptr);
    root["url"] = snapshot.url;
    root["host"] = snapshot.host;
    root["pageTitle"] = snapshot.pageTitle;
    root["siteId"] = snapshot.siteId;
    root["playbackState"] = BrowserPlaybackStateToString(snapshot.playbackState);
    root["activityDisposition"] = BrowserActivityDispositionToString(snapshot.activityDisposition);
    root["activityCard"] = snapshot.activityCard.has_value() ? SerializeActivityCard(snapshot.activityCard.value()) : json(nullptr);
    root["sentAtUnixMs"] = snapshot.sentAtUnixMs;
    return root.dump();
}

}  // namespace drpc
