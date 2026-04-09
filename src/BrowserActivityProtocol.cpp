#include "BrowserActivityProtocol.h"

#include <algorithm>
#include <cctype>
#include <sstream>

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

bool ReadOptionalNumber(const json& root, const char* key, std::optional<double>& output, std::string& error) {
    if (!root.contains(key) || root[key].is_null()) {
        output.reset();
        return true;
    }

    if (!root[key].is_number()) {
        error = std::string("Expected numeric field: ") + key;
        return false;
    }

    output = root[key].get<double>();
    return true;
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

std::string BrowserActivitySnapshot::IdentityKey() const {
    std::ostringstream key;
    key << browser << '|'
        << host << '|'
        << siteId << '|'
        << seriesTitle << '|'
        << episodeLabel << '|'
        << pageTitle << '|'
        << BrowserPlaybackStateToString(playbackState);
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

    if (!readRequiredString("browser", snapshot.browser) ||
        !readRequiredString("url", snapshot.url) ||
        !readRequiredString("host", snapshot.host) ||
        !readRequiredString("pageTitle", snapshot.pageTitle)) {
        return false;
    }

    snapshot.siteId = Trim(root.value("siteId", ""));
    snapshot.seriesTitle = Trim(root.value("seriesTitle", ""));
    snapshot.episodeLabel = Trim(root.value("episodeLabel", ""));

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

    if (!ReadOptionalNumber(root, "positionSeconds", snapshot.positionSeconds, error) ||
        !ReadOptionalNumber(root, "durationSeconds", snapshot.durationSeconds, error)) {
        return false;
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
    root["seriesTitle"] = snapshot.seriesTitle;
    root["episodeLabel"] = snapshot.episodeLabel;
    root["positionSeconds"] = snapshot.positionSeconds.has_value() ? json(snapshot.positionSeconds.value()) : json(nullptr);
    root["durationSeconds"] = snapshot.durationSeconds.has_value() ? json(snapshot.durationSeconds.value()) : json(nullptr);
    root["sentAtUnixMs"] = snapshot.sentAtUnixMs;
    return root.dump();
}

}  // namespace drpc
