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
        "schemaVersion": 1,
        "browser": "chrome",
        "tabId": 42,
        "url": "https://www.crunchyroll.com/watch/episode-1",
        "host": "www.crunchyroll.com",
        "pageTitle": "Example Show Episode 1",
        "siteId": "crunchyroll",
        "playbackState": "playing",
        "seriesTitle": "Example Show",
        "episodeLabel": "Episode 1",
        "positionSeconds": 32.5,
        "durationSeconds": 1460.0,
        "sentAtUnixMs": 1710000000123
    })";

    drpc::BrowserActivitySnapshot snapshot;
    std::string error;
    Expect(drpc::ParseBrowserActivityMessage(payload, snapshot, error), "expected valid browser payload");
    Expect(snapshot.browser == "chrome", "browser should parse");
    Expect(snapshot.siteId == "crunchyroll", "siteId should parse");
    Expect(snapshot.playbackState == drpc::BrowserPlaybackState::Playing, "playbackState should parse");
    Expect(snapshot.positionSeconds.has_value() && snapshot.positionSeconds.value() == 32.5, "positionSeconds should parse");
}

void TestRejectInvalidSchemaVersion() {
    const std::string payload = R"({
        "schemaVersion": 99,
        "browser": "chrome",
        "url": "https://example.com",
        "host": "example.com",
        "pageTitle": "Example",
        "playbackState": "idle",
        "sentAtUnixMs": 1710000000123
    })";

    drpc::BrowserActivitySnapshot snapshot;
    std::string error;
    Expect(!drpc::ParseBrowserActivityMessage(payload, snapshot, error), "expected unsupported schema version to fail");
}

void TestProjectSupportedSite() {
    drpc::BrowserActivitySnapshot snapshot;
    snapshot.browser = "chrome";
    snapshot.url = "https://www.crunchyroll.com/watch/episode-1";
    snapshot.host = "www.crunchyroll.com";
    snapshot.pageTitle = "Example Show Episode 1";
    snapshot.siteId = "crunchyroll";
    snapshot.playbackState = drpc::BrowserPlaybackState::Playing;
    snapshot.seriesTitle = "Example Show";
    snapshot.episodeLabel = "Episode 1";
    snapshot.sentAtUnixMs = 1710000000123;

    drpc::BrowserDetectionConfig config;
    drpc::ActivityPreset activeTemplate{.name = "Watching Video", .details = "Watching a video", .state = "Full screen"};
    drpc::ActivityPreset fallbackTemplate{.name = "Idle", .details = "Away", .state = "AFK", .showElapsedTime = false};

    const auto projection = drpc::ProjectBrowserPresence(snapshot, true, true, config, activeTemplate, fallbackTemplate);
    Expect(projection.activity.preset.details == "Example Show", "supported site should use seriesTitle as details");
    Expect(projection.activity.preset.state == "Episode 1 on Crunchyroll", "supported site should include episode label");
    Expect(projection.sourceStatus == L"Browser connected", "supported site status should be connected");
}

void TestProjectGenericFallback() {
    drpc::BrowserActivitySnapshot snapshot;
    snapshot.browser = "edge";
    snapshot.url = "https://example.com/video";
    snapshot.host = "example.com";
    snapshot.pageTitle = "Some Stream";
    snapshot.playbackState = drpc::BrowserPlaybackState::Playing;
    snapshot.sentAtUnixMs = 1710000000123;

    drpc::BrowserDetectionConfig config;
    config.supportedSites = {"crunchyroll"};
    drpc::ActivityPreset activeTemplate{.name = "Watching Video", .details = "Watching a video", .state = "Full screen"};
    drpc::ActivityPreset fallbackTemplate{.name = "Idle", .details = "Away", .state = "AFK", .showElapsedTime = false};

    const auto projection = drpc::ProjectBrowserPresence(snapshot, true, true, config, activeTemplate, fallbackTemplate);
    Expect(projection.activity.preset.details == "Some Stream", "generic projection should use pageTitle");
    Expect(projection.activity.preset.state == "Watching on example.com", "generic projection should show host");
    Expect(projection.sourceStatus == L"Browser connected (generic)", "generic projection should mark generic status");
}

void TestProjectStaleSnapshot() {
    drpc::BrowserActivitySnapshot snapshot;
    snapshot.browser = "edge";
    snapshot.url = "https://example.com/video";
    snapshot.host = "example.com";
    snapshot.pageTitle = "Some Stream";
    snapshot.playbackState = drpc::BrowserPlaybackState::Playing;
    snapshot.sentAtUnixMs = 1710000000123;

    drpc::BrowserDetectionConfig config;
    drpc::ActivityPreset activeTemplate{.name = "Watching Video", .details = "Watching a video", .state = "Full screen"};
    drpc::ActivityPreset fallbackTemplate{.name = "Idle", .details = "Away", .state = "AFK", .showElapsedTime = false};

    const auto projection = drpc::ProjectBrowserPresence(snapshot, true, false, config, activeTemplate, fallbackTemplate);
    Expect(projection.activity.preset.state == "Browser data is stale", "stale snapshot should use stale status");
    Expect(projection.sourceStatus == L"Browser stale", "stale projection should mark browser stale");
}

void TestProjectNoActiveTab() {
    drpc::BrowserActivitySnapshot snapshot;
    snapshot.browser = "chrome";
    snapshot.pageTitle = "No active browser tab";
    snapshot.siteId = "drpc-no-active-tab";
    snapshot.playbackState = drpc::BrowserPlaybackState::Idle;
    snapshot.sentAtUnixMs = 1710000000123;

    drpc::BrowserDetectionConfig config;
    drpc::ActivityPreset activeTemplate{.name = "Watching Video", .details = "Watching a video", .state = "Full screen"};
    drpc::ActivityPreset fallbackTemplate{.name = "Idle", .details = "Away", .state = "AFK", .showElapsedTime = false};

    const auto projection = drpc::ProjectBrowserPresence(snapshot, true, true, config, activeTemplate, fallbackTemplate);
    Expect(projection.activity.preset.details == "No active browser page", "no-active-tab should use a dedicated detail");
    Expect(projection.activity.preset.state == "No active browser tab", "no-active-tab should use a dedicated state");
    Expect(projection.sourceStatus == L"No active browser page", "no-active-tab should use a dedicated source status");
}

void TestProjectUnsupportedBrowserPage() {
    drpc::BrowserActivitySnapshot snapshot;
    snapshot.browser = "chrome";
    snapshot.url = "chrome://newtab/";
    snapshot.host = "chrome";
    snapshot.pageTitle = "New Tab";
    snapshot.siteId = "drpc-internal-page";
    snapshot.playbackState = drpc::BrowserPlaybackState::Idle;
    snapshot.sentAtUnixMs = 1710000000123;

    drpc::BrowserDetectionConfig config;
    drpc::ActivityPreset activeTemplate{.name = "Watching Video", .details = "Watching a video", .state = "Full screen"};
    drpc::ActivityPreset fallbackTemplate{.name = "Idle", .details = "Away", .state = "AFK", .showElapsedTime = false};

    const auto projection = drpc::ProjectBrowserPresence(snapshot, true, true, config, activeTemplate, fallbackTemplate);
    Expect(projection.activity.preset.details == "New Tab", "internal browser pages should keep the tab title");
    Expect(projection.activity.preset.state == "Browser page not supported", "internal browser pages should explain the limitation");
    Expect(projection.sourceStatus == L"Unsupported browser page", "internal browser pages should use a dedicated status");
}

}  // namespace

int main() {
    TestParseValidMessage();
    TestRejectInvalidSchemaVersion();
    TestProjectSupportedSite();
    TestProjectGenericFallback();
    TestProjectStaleSnapshot();
    TestProjectNoActiveTab();
    TestProjectUnsupportedBrowserPage();

    std::cout << "All browser activity tests passed.\n";
    return 0;
}
