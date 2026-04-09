#include "BrowserPresenceProjection.h"

#include "Config.h"

#include <algorithm>
#include <cctype>

namespace drpc {
namespace {

inline constexpr std::string_view kNoActiveTabSiteId = "drpc-no-active-tab";
inline constexpr std::string_view kInternalPageSiteId = "drpc-internal-page";

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool IsHttpUrl(std::string_view url) {
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

bool IsSupportedSite(const BrowserDetectionConfig& config, std::string_view siteId) {
    if (siteId.empty() || config.supportedSites.empty()) {
        return false;
    }

    const auto normalized = ToLower(std::string(siteId));
    return std::any_of(config.supportedSites.begin(), config.supportedSites.end(), [&](const std::string& candidate) {
        return ToLower(candidate) == normalized;
    });
}

std::string SiteDisplayName(const BrowserActivitySnapshot& snapshot) {
    const auto siteId = ToLower(snapshot.siteId);
    if (siteId == "crunchyroll") {
        return "Crunchyroll";
    }
    if (siteId == "hidive") {
        return "HIDIVE";
    }
    if (!snapshot.siteId.empty()) {
        return snapshot.siteId;
    }
    if (!snapshot.host.empty()) {
        return snapshot.host;
    }
    return "browser";
}

std::string CleanPageTitle(const BrowserActivitySnapshot& snapshot) {
    if (!snapshot.pageTitle.empty()) {
        return snapshot.pageTitle;
    }
    if (!snapshot.seriesTitle.empty()) {
        return snapshot.seriesTitle;
    }
    return "Watching in browser";
}

ActivityPreset PrepareTemplate(const ActivityPreset& source, bool showElapsedTime) {
    ActivityPreset preset = source;
    preset.showElapsedTime = showElapsedTime;
    return preset;
}

bool IsNoActiveTabSnapshot(const BrowserActivitySnapshot& snapshot) {
    return snapshot.siteId == kNoActiveTabSiteId;
}

bool IsInternalPageSnapshot(const BrowserActivitySnapshot& snapshot) {
    return snapshot.siteId == kInternalPageSiteId || (!snapshot.url.empty() && !IsHttpUrl(snapshot.url));
}

BrowserPresenceProjection MakeFallbackProjection(const ActivityPreset& fallbackTemplate,
                                                 std::string identity,
                                                 std::wstring label,
                                                 std::wstring sourceStatus,
                                                 std::string detailsOverride = {},
                                                 std::string stateOverride = {}) {
    auto preset = PrepareTemplate(fallbackTemplate, false);
    if (!detailsOverride.empty()) {
        preset.details = std::move(detailsOverride);
    }
    if (!stateOverride.empty()) {
        preset.state = std::move(stateOverride);
    }

    return BrowserPresenceProjection{
        .activity = SourceActivity{
            .preset = std::move(preset),
            .identity = std::move(identity),
            .label = std::move(label),
        },
        .sourceStatus = std::move(sourceStatus),
    };
}

}  // namespace

BrowserPresenceProjection ProjectBrowserPresence(const std::optional<BrowserActivitySnapshot>& snapshot,
                                                bool hasSeenBrowser,
                                                bool isFresh,
                                                const BrowserDetectionConfig& config,
                                                const ActivityPreset& activeTemplate,
                                                const ActivityPreset& fallbackTemplate) {
    if (!snapshot.has_value()) {
        return MakeFallbackProjection(
            fallbackTemplate,
            "browser:waiting",
            L"Waiting for browser",
            hasSeenBrowser ? L"Browser disconnected" : L"Waiting for browser");
    }

    if (IsNoActiveTabSnapshot(snapshot.value())) {
        return MakeFallbackProjection(
            fallbackTemplate,
            "browser:no-active-tab",
            L"No active browser page",
            L"No active browser page",
            "No active browser page",
            "No active browser tab");
    }

    if (IsInternalPageSnapshot(snapshot.value())) {
        const auto title = CleanPageTitle(snapshot.value());
        const auto label = ToWide(title);
        return MakeFallbackProjection(
            fallbackTemplate,
            "browser:unsupported-page:" + snapshot->IdentityKey(),
            label.empty() ? L"Unsupported browser page" : label,
            L"Unsupported browser page",
            title,
            "Browser page not supported");
    }

    const auto supported = IsSupportedSite(config, snapshot->siteId);
    const auto siteDisplayName = SiteDisplayName(snapshot.value());
    const auto title = supported && !snapshot->seriesTitle.empty() ? snapshot->seriesTitle : CleanPageTitle(snapshot.value());
    const auto label = ToWide(title);

    if (!isFresh) {
        return MakeFallbackProjection(
            fallbackTemplate,
            "browser:stale:" + snapshot->IdentityKey(),
            label.empty() ? L"Browser stale" : label,
            L"Browser stale",
            title,
            "Browser data is stale");
    }

    if (snapshot->playbackState == BrowserPlaybackState::Paused) {
        return MakeFallbackProjection(
            fallbackTemplate,
            "browser:paused:" + snapshot->IdentityKey(),
            label.empty() ? L"Paused in browser" : label,
            L"Browser paused",
            title,
            "Paused on " + siteDisplayName);
    }

    if (snapshot->playbackState == BrowserPlaybackState::Idle) {
        return MakeFallbackProjection(
            fallbackTemplate,
            "browser:idle:" + snapshot->IdentityKey(),
            label.empty() ? L"Idle in browser" : label,
            L"Browser idle",
            title,
            "Idle on " + siteDisplayName);
    }

    auto preset = PrepareTemplate(activeTemplate, true);
    preset.details = title;

    if (supported && !snapshot->episodeLabel.empty()) {
        preset.state = snapshot->episodeLabel + " on " + siteDisplayName;
    } else {
        preset.state = "Watching on " + siteDisplayName;
    }

    return BrowserPresenceProjection{
        .activity = SourceActivity{
            .preset = std::move(preset),
            .identity = "browser:active:" + snapshot->IdentityKey(),
            .label = label.empty() ? ToWide(siteDisplayName) : label,
        },
        .sourceStatus = supported ? L"Browser connected" : L"Browser connected (generic)",
    };
}

}  // namespace drpc
