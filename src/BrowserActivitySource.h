#pragma once

#include "BrowserPresenceProjection.h"
#include "Config.h"
#include "Logging.h"
#include "PresenceSource.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace drpc {

class BrowserActivitySource final : public PresenceSource {
public:
    BrowserActivitySource(const AppConfig& config, Logger& logger, std::function<void(bool)> onActivityUpdated = {});
    ~BrowserActivitySource() override;

    SourceActivity Current() const override;
    bool Next() override;
    bool Previous() override;
    bool SupportsManualSelection() const override;
    std::wstring BuildMenuLabel() const override;
    std::wstring SourceStatus() const override;

private:
    BrowserPresenceProjection BuildProjection() const;
    void RunPipeServer();
    void ProcessMessage(std::string message);
    void StopPipeServer();
    void WakePipeServer() const;

    BrowserDetectionConfig config_;
    Logger& logger_;
    std::function<void(bool)> onActivityUpdated_;
    ActivityPreset activeTemplate_;
    ActivityPreset fallbackTemplate_;
    mutable std::mutex mutex_;
    std::optional<BrowserActivitySnapshot> latestSnapshot_;
    std::chrono::steady_clock::time_point lastReceivedAt_{};
    bool hasSeenBrowser_ = false;
    std::atomic<bool> stopRequested_ = false;
    std::thread serverThread_;
};

}  // namespace drpc
