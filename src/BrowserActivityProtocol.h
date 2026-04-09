#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace drpc {

inline constexpr int kBrowserActivitySchemaVersion = 1;
inline constexpr std::size_t kMaxBrowserActivityMessageBytes = 64 * 1024;
inline constexpr wchar_t kBrowserActivityPipeName[] = L"\\\\.\\pipe\\drpc-browser-activity";
inline constexpr char kNativeMessagingHostName[] = "com.drpc.browser_host";

enum class BrowserPlaybackState {
    Idle,
    Playing,
    Paused,
};

struct BrowserActivitySnapshot {
    int schemaVersion = kBrowserActivitySchemaVersion;
    std::string browser;
    std::optional<int> tabId;
    std::string url;
    std::string host;
    std::string pageTitle;
    std::string siteId;
    BrowserPlaybackState playbackState = BrowserPlaybackState::Idle;
    std::string seriesTitle;
    std::string episodeLabel;
    std::optional<double> positionSeconds;
    std::optional<double> durationSeconds;
    std::int64_t sentAtUnixMs = 0;

    std::string IdentityKey() const;
};

bool ParseBrowserActivityMessage(std::string_view message, BrowserActivitySnapshot& snapshot, std::string& error);
std::string SerializeBrowserActivityMessage(const BrowserActivitySnapshot& snapshot);
std::string BrowserPlaybackStateToString(BrowserPlaybackState state);

}  // namespace drpc
