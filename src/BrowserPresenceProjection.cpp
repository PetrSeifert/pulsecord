#include "BrowserPresenceProjection.h"

#include "Config.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace drpc {
namespace {

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool IsSupportedSite(const BrowserDetectionConfig& config, std::string_view siteId) {
    if (siteId.empty()) {
        return true;
    }
    if (config.supportedSites.empty()) {
        return true;
    }

    const auto normalized = ToLower(std::string(siteId));
    return std::any_of(config.supportedSites.begin(), config.supportedSites.end(), [&](const std::string& candidate) {
        return ToLower(candidate) == normalized;
    });
}

std::wstring BuildLabel(const BrowserActivitySnapshot& snapshot) {
    if (snapshot.activityCard.has_value()) {
        if (!snapshot.activityCard->details.empty()) {
            return ToWide(snapshot.activityCard->details);
        }
        if (!snapshot.activityCard->name.empty()) {
            return ToWide(snapshot.activityCard->name);
        }
        if (!snapshot.activityCard->state.empty()) {
            return ToWide(snapshot.activityCard->state);
        }
    }

    if (!snapshot.pageTitle.empty()) {
        return ToWide(snapshot.pageTitle);
    }
    if (!snapshot.siteId.empty()) {
        return ToWide(snapshot.siteId);
    }
    if (!snapshot.host.empty()) {
        return ToWide(snapshot.host);
    }

    return L"Browser activity";
}

BrowserPresenceProjection MakeProjection(SourceActivity activity, std::wstring sourceStatus) {
    return BrowserPresenceProjection{
        .activity = std::move(activity),
        .sourceStatus = std::move(sourceStatus),
    };
}

BrowserPresenceProjection MakeClearProjection(std::string identity, std::wstring label, std::wstring sourceStatus) {
    return MakeProjection(SourceActivity{
                              .preset = std::nullopt,
                              .identity = std::move(identity),
                              .label = std::move(label),
                              .disposition = SourceActivityDisposition::Clear,
                          },
                          std::move(sourceStatus));
}

}  // namespace

BrowserPresenceProjection ProjectBrowserPresence(const std::optional<BrowserActivitySnapshot>& snapshot,
                                                bool hasSeenBrowser,
                                                bool isFresh,
                                                const BrowserDetectionConfig& config,
                                                const ActivityPreset&,
                                                const ActivityPreset&) {
    if (!snapshot.has_value()) {
        const auto status = hasSeenBrowser ? L"Browser disconnected" : L"Waiting for browser";
        return MakeClearProjection("browser:waiting", L"Waiting for browser", status);
    }

    const auto label = BuildLabel(snapshot.value());
    if (!isFresh) {
        return MakeClearProjection("browser:stale:" + snapshot->IdentityKey(), label, L"Browser stale");
    }

    if (!IsSupportedSite(config, snapshot->siteId)) {
        return MakeClearProjection("browser:filtered:" + snapshot->IdentityKey(), label, L"Browser site filtered");
    }

    if (snapshot->activityDisposition == BrowserActivityDisposition::Clear || !snapshot->activityCard.has_value()) {
        return MakeClearProjection("browser:clear:" + snapshot->IdentityKey(), label, L"No matched browser activity");
    }

    return MakeProjection(SourceActivity{
                              .preset = snapshot->activityCard,
                              .identity = (snapshot->activityDisposition == BrowserActivityDisposition::Sticky ? "browser:sticky:" : "browser:active:") + snapshot->IdentityKey(),
                              .label = label,
                              .disposition = SourceActivityDisposition::Publish,
                          },
                          snapshot->activityDisposition == BrowserActivityDisposition::Sticky ? L"Browser connected (sticky)" : L"Browser connected");
}

}  // namespace drpc
