#include "BrowserActivityProtocol.h"
#include "BrowserPresenceProjection.h"
#include "Config.h"
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>

namespace {

void Expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "Test failed: " << message << '\n';
        std::exit(1);
    }
}

void TestParseValidMessage() {
    const std::string payload = R"({
        "schemaVersion": 2,
        "browser": "chrome",
        "tabId": 42,
        "url": "https://www.9animetv.to/watch/example",
        "host": "www.9animetv.to",
        "pageTitle": "Example Show",
        "siteId": "9anime",
        "playbackState": "playing",
        "activityDisposition": "publish",
        "activityCard": {
            "name": "Example Show",
            "details": "Example Show",
            "state": "Episode 3",
            "type": "watching",
            "statusDisplayType": "details",
            "showElapsedTime": true,
            "startedAtUnixSeconds": 1710000000,
            "endAtUnixSeconds": 1710001200,
            "assets": {
                "largeImage": "https://cdn.example.com/poster.jpg",
                "largeText": "Episode 3",
                "largeUrl": "https://www.9animetv.to/watch/example"
            },
            "buttons": [
                {
                    "label": "Watch Anime",
                    "url": "https://www.9animetv.to/watch/example"
                }
            ]
        },
        "sentAtUnixMs": 1710000000123
    })";

    drpc::BrowserActivitySnapshot snapshot;
    std::string error;
    Expect(drpc::ParseBrowserActivityMessage(payload, snapshot, error), "expected valid browser payload");
    Expect(snapshot.browser == "chrome", "browser should parse");
    Expect(snapshot.siteId == "9anime", "siteId should parse");
    Expect(snapshot.playbackState == drpc::BrowserPlaybackState::Playing, "playbackState should parse");
    Expect(snapshot.activityDisposition == drpc::BrowserActivityDisposition::Publish, "activityDisposition should parse");
    Expect(snapshot.activityCard.has_value(), "activityCard should parse");
    Expect(snapshot.activityCard->details == "Example Show", "activityCard details should parse");
    Expect(snapshot.activityCard->type == drpc::ActivityType::Watching, "activityCard type should parse");
    Expect(snapshot.activityCard->assets.largeImage == "https://cdn.example.com/poster.jpg", "activityCard largeImage should parse");
}

void TestActivityTypeHelpers() {
    Expect(drpc::ParseActivityType("playing") == drpc::ActivityType::Playing, "playing should parse");
    Expect(drpc::ParseActivityType("watching") == drpc::ActivityType::Watching, "watching should parse");
    Expect(drpc::ParseActivityType("listening") == drpc::ActivityType::Listening, "listening should parse");
    Expect(drpc::ParseActivityType("streaming") == drpc::ActivityType::Streaming, "streaming should parse");
    Expect(drpc::ParseActivityType("competing") == drpc::ActivityType::Competing, "competing should parse");
    Expect(drpc::ParseActivityType("unknown", drpc::ActivityType::Watching) == drpc::ActivityType::Watching, "unknown should use fallback");
    Expect(drpc::ActivityTypeToString(drpc::ActivityType::Playing) == "playing", "playing should stringify");
    Expect(drpc::ActivityTypeToString(drpc::ActivityType::Watching) == "watching", "watching should stringify");
    Expect(drpc::ActivityTypeToString(drpc::ActivityType::Listening) == "listening", "listening should stringify");
}

void TestRejectInvalidSchemaVersion() {
    const std::string payload = R"({
        "schemaVersion": 1,
        "browser": "chrome",
        "url": "https://example.com",
        "host": "example.com",
        "pageTitle": "Example",
        "playbackState": "idle",
        "activityDisposition": "clear",
        "sentAtUnixMs": 1710000000123
    })";

    drpc::BrowserActivitySnapshot snapshot;
    std::string error;
    Expect(!drpc::ParseBrowserActivityMessage(payload, snapshot, error), "expected unsupported schema version to fail");
}

void TestProjectPublishedCard() {
    drpc::BrowserActivitySnapshot snapshot;
    snapshot.browser = "chrome";
    snapshot.url = "https://www.9animetv.to/watch/example";
    snapshot.host = "www.9animetv.to";
    snapshot.pageTitle = "Example Show";
    snapshot.siteId = "9anime";
    snapshot.playbackState = drpc::BrowserPlaybackState::Playing;
    snapshot.activityDisposition = drpc::BrowserActivityDisposition::Publish;
    snapshot.activityCard = drpc::ActivityPreset{
        .name = "Example Show",
        .details = "Example Show",
        .state = "Episode 3",
        .assets = drpc::ActivityAssets{"https://cdn.example.com/poster.jpg", "Episode 3", "https://www.9animetv.to/watch/example", "", "", ""},
        .type = drpc::ActivityType::Watching,
        .statusDisplayType = drpc::StatusDisplayType::Details,
        .showElapsedTime = true,
        .startedAtUnixSeconds = 1710000000,
        .endAtUnixSeconds = 1710001200,
    };
    snapshot.sentAtUnixMs = 1710000000123;

    drpc::BrowserDetectionConfig config;
    drpc::ActivityPreset activeTemplate{.name = "Watching Video"};
    drpc::ActivityPreset fallbackTemplate{.name = "Idle", .showElapsedTime = false};

    const auto projection = drpc::ProjectBrowserPresence(snapshot, true, true, config, activeTemplate, fallbackTemplate);
    Expect(projection.activity.disposition == drpc::SourceActivityDisposition::Publish, "published card should publish");
    Expect(projection.activity.preset.has_value(), "published card should preserve preset");
    Expect(projection.activity.preset->details == "Example Show", "published card should use activityCard details");
    Expect(projection.activity.preset->type == drpc::ActivityType::Watching, "published card should preserve watching type");
    Expect(projection.activity.preset->endAtUnixSeconds.has_value(), "published card should keep timestamps");
    Expect(projection.sourceStatus == L"Browser connected", "published card should be connected");
}

void TestProjectStickyCard() {
    drpc::BrowserActivitySnapshot snapshot;
    snapshot.browser = "chrome";
    snapshot.url = "https://www.9animetv.to/watch/example";
    snapshot.host = "www.9animetv.to";
    snapshot.pageTitle = "Example Show";
    snapshot.siteId = "9anime";
    snapshot.playbackState = drpc::BrowserPlaybackState::Paused;
    snapshot.activityDisposition = drpc::BrowserActivityDisposition::Sticky;
    snapshot.activityCard = drpc::ActivityPreset{
        .name = "Example Show",
        .details = "Example Show",
        .state = "Episode 3",
        .type = drpc::ActivityType::Watching,
    };
    snapshot.sentAtUnixMs = 1710000000123;

    drpc::BrowserDetectionConfig config;
    drpc::ActivityPreset activeTemplate{.name = "Watching Video"};
    drpc::ActivityPreset fallbackTemplate{.name = "Idle", .showElapsedTime = false};

    const auto projection = drpc::ProjectBrowserPresence(snapshot, true, true, config, activeTemplate, fallbackTemplate);
    Expect(projection.activity.disposition == drpc::SourceActivityDisposition::Publish, "sticky card should still publish");
    Expect(projection.sourceStatus == L"Browser connected (sticky)", "sticky card should mark sticky status");
}

void TestProjectClearDisposition() {
    drpc::BrowserActivitySnapshot snapshot;
    snapshot.browser = "edge";
    snapshot.url = "https://example.com/video";
    snapshot.host = "example.com";
    snapshot.pageTitle = "Some Stream";
    snapshot.playbackState = drpc::BrowserPlaybackState::Idle;
    snapshot.activityDisposition = drpc::BrowserActivityDisposition::Clear;
    snapshot.sentAtUnixMs = 1710000000123;

    drpc::BrowserDetectionConfig config;
    drpc::ActivityPreset activeTemplate{.name = "Watching Video"};
    drpc::ActivityPreset fallbackTemplate{.name = "Idle", .showElapsedTime = false};

    const auto projection = drpc::ProjectBrowserPresence(snapshot, true, true, config, activeTemplate, fallbackTemplate);
    Expect(projection.activity.disposition == drpc::SourceActivityDisposition::Clear, "clear disposition should clear presence");
    Expect(!projection.activity.preset.has_value(), "clear disposition should not expose a preset");
    Expect(projection.sourceStatus == L"No matched browser activity", "clear disposition should explain hidden activity");
}

void TestProjectUnsupportedSiteFilter() {
    drpc::BrowserActivitySnapshot snapshot;
    snapshot.browser = "edge";
    snapshot.url = "https://www.9animetv.to/watch/example";
    snapshot.host = "www.9animetv.to";
    snapshot.pageTitle = "Example Show";
    snapshot.siteId = "9anime";
    snapshot.playbackState = drpc::BrowserPlaybackState::Playing;
    snapshot.activityDisposition = drpc::BrowserActivityDisposition::Publish;
    snapshot.activityCard = drpc::ActivityPreset{
        .name = "Example Show",
        .details = "Example Show",
        .state = "Episode 3",
        .type = drpc::ActivityType::Watching,
    };
    snapshot.sentAtUnixMs = 1710000000123;

    drpc::BrowserDetectionConfig config;
    config.supportedSites = {"crunchyroll"};
    drpc::ActivityPreset activeTemplate{.name = "Watching Video"};
    drpc::ActivityPreset fallbackTemplate{.name = "Idle", .showElapsedTime = false};

    const auto projection = drpc::ProjectBrowserPresence(snapshot, true, true, config, activeTemplate, fallbackTemplate);
    Expect(projection.activity.disposition == drpc::SourceActivityDisposition::Clear, "filtered site should clear presence");
    Expect(projection.sourceStatus == L"Browser site filtered", "filtered site should be explained");
}

void TestProjectStaleSnapshot() {
    drpc::BrowserActivitySnapshot snapshot;
    snapshot.browser = "edge";
    snapshot.url = "https://www.9animetv.to/watch/example";
    snapshot.host = "www.9animetv.to";
    snapshot.pageTitle = "Example Show";
    snapshot.siteId = "9anime";
    snapshot.playbackState = drpc::BrowserPlaybackState::Playing;
    snapshot.activityDisposition = drpc::BrowserActivityDisposition::Sticky;
    snapshot.activityCard = drpc::ActivityPreset{
        .name = "Example Show",
        .details = "Example Show",
        .state = "Episode 3",
        .type = drpc::ActivityType::Watching,
    };
    snapshot.sentAtUnixMs = 1710000000123;

    drpc::BrowserDetectionConfig config;
    drpc::ActivityPreset activeTemplate{.name = "Watching Video"};
    drpc::ActivityPreset fallbackTemplate{.name = "Idle", .showElapsedTime = false};

    const auto projection = drpc::ProjectBrowserPresence(snapshot, true, false, config, activeTemplate, fallbackTemplate);
    Expect(projection.activity.disposition == drpc::SourceActivityDisposition::Clear, "stale snapshot should clear");
    Expect(projection.sourceStatus == L"Browser stale", "stale snapshot should mark browser stale");
}

void TestParseBrowserActivityDefaultsToWatchingType() {
    const std::string payload = R"({
        "schemaVersion": 2,
        "browser": "chrome",
        "url": "https://example.com/watch",
        "host": "example.com",
        "pageTitle": "Example",
        "playbackState": "playing",
        "activityDisposition": "publish",
        "activityCard": {
            "name": "Example",
            "details": "Episode 1",
            "state": "Now"
        },
        "sentAtUnixMs": 1710000000123
    })";

    drpc::BrowserActivitySnapshot snapshot;
    std::string error;
    Expect(drpc::ParseBrowserActivityMessage(payload, snapshot, error), "expected browser payload without type to parse");
    Expect(snapshot.activityCard.has_value(), "activityCard should exist");
    Expect(snapshot.activityCard->type == drpc::ActivityType::Watching, "browser activityCard should default to watching");
}

void TestIdentityKeyTracksActivityCardMetadata() {
    drpc::BrowserActivitySnapshot base;
    base.browser = "chrome";
    base.url = "https://example.com/watch";
    base.host = "example.com";
    base.pageTitle = "Example";
    base.siteId = "example";
    base.playbackState = drpc::BrowserPlaybackState::Playing;
    base.activityDisposition = drpc::BrowserActivityDisposition::Publish;
    base.activityCard = drpc::ActivityPreset{
        .name = "Watching Example",
        .details = "Watching Example",
        .detailsUrl = "https://example.com/watch",
        .state = "Episode 1",
        .stateUrl = "https://example.com/episodes/1",
        .assets = drpc::ActivityAssets{"poster", "Poster", "https://example.com/poster", "play", "Playing", "https://example.com/status"},
        .buttons = {drpc::ActivityButton{"Watch", "https://example.com/watch"}},
        .type = drpc::ActivityType::Watching,
        .statusDisplayType = drpc::StatusDisplayType::Details,
        .showElapsedTime = true,
        .startedAtUnixSeconds = 1710000000,
        .endAtUnixSeconds = 1710001200,
    };

    auto updatedType = base;
    updatedType.activityCard->type = drpc::ActivityType::Listening;
    Expect(base.IdentityKey() != updatedType.IdentityKey(), "identity key should change when activity type changes");

    auto updatedButton = base;
    updatedButton.activityCard->buttons[0].url = "https://example.com/trailer";
    Expect(base.IdentityKey() != updatedButton.IdentityKey(), "identity key should change when activity buttons change");

    auto updatedAssets = base;
    updatedAssets.activityCard->assets.smallText = "Paused";
    Expect(base.IdentityKey() != updatedAssets.IdentityKey(), "identity key should change when activity assets change");
}

}  // namespace

int main() {
    TestActivityTypeHelpers();
    TestParseValidMessage();
    TestRejectInvalidSchemaVersion();
    TestParseBrowserActivityDefaultsToWatchingType();
    TestIdentityKeyTracksActivityCardMetadata();
    TestProjectPublishedCard();
    TestProjectStickyCard();
    TestProjectClearDisposition();
    TestProjectUnsupportedSiteFilter();
    TestProjectStaleSnapshot();

    std::cout << "All browser activity tests passed.\n";
    return 0;
}
