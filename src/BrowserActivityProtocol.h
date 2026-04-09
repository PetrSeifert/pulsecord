#pragma once

#include "ActivityPreset.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace drpc {

inline constexpr int kBrowserActivitySchemaVersion = 2;
inline constexpr std::size_t kMaxBrowserActivityMessageBytes = 64 * 1024;
inline constexpr wchar_t kBrowserActivityPipeName[] = L"\\\\.\\pipe\\drpc-browser-activity";
inline constexpr char kNativeMessagingHostName[] = "com.drpc.browser_host";

enum class BrowserPlaybackState {
    Idle,
    Playing,
    Paused,
};

enum class BrowserActivityDisposition {
    Publish,
    Sticky,
    Clear,
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
    BrowserActivityDisposition activityDisposition = BrowserActivityDisposition::Clear;
    std::optional<ActivityPreset> activityCard;
    std::int64_t sentAtUnixMs = 0;

    std::string IdentityKey() const;
};

bool ParseBrowserActivityMessage(std::string_view message, BrowserActivitySnapshot& snapshot, std::string& error);
std::string SerializeBrowserActivityMessage(const BrowserActivitySnapshot& snapshot);
std::string BrowserPlaybackStateToString(BrowserPlaybackState state);
std::string BrowserActivityDispositionToString(BrowserActivityDisposition disposition);

}  // namespace drpc
