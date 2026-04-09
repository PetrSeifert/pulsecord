#pragma once

#include "BrowserActivityProtocol.h"
#include "Config.h"
#include "PresenceSource.h"

#include <optional>

namespace drpc {

struct BrowserPresenceProjection {
    SourceActivity activity;
    std::wstring sourceStatus;
};

BrowserPresenceProjection ProjectBrowserPresence(const std::optional<BrowserActivitySnapshot>& snapshot,
                                                bool hasSeenBrowser,
                                                bool isFresh,
                                                const BrowserDetectionConfig& config,
                                                const ActivityPreset& activeTemplate,
                                                const ActivityPreset& fallbackTemplate);

}  // namespace drpc
