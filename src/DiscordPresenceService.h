#pragma once

#include "ActivityPreset.h"
#include "Config.h"
#include "Logging.h"
#include "PresenceSource.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace drpc {

class IDiscordPresenceBackend {
public:
    virtual ~IDiscordPresenceBackend() = default;

    virtual bool Initialize(const AppConfig& config,
                            const std::filesystem::path& authStoragePath,
                            Logger& logger) = 0;
    virtual void Publish(const ActivityPreset& preset, Logger& logger) = 0;
    virtual void Clear(Logger& logger) = 0;
    virtual void PumpCallbacks(Logger& logger) = 0;
    virtual void Authenticate(Logger& logger) = 0;
    virtual void ResetAuthentication(Logger& logger) = 0;
    virtual std::wstring StatusText() const = 0;
    virtual bool IsAvailable() const = 0;
    virtual bool IsAuthenticated() const = 0;
};

class DiscordPresenceService {
public:
    DiscordPresenceService(std::unique_ptr<IDiscordPresenceBackend> backend,
                           PresenceSource& source,
                           Logger& logger,
                           AppConfig config,
                           std::filesystem::path authStoragePath);

    void Initialize();
    void PublishCurrent(bool force);
    void NextPreset();
    void PreviousPreset();
    void Pause();
    void Resume();
    void Clear();
    void PumpCallbacks();
    void Authenticate();
    void ResetAuthentication();

    bool IsPaused() const;
    bool IsAuthenticated() const;
    std::wstring BuildStatusText() const;
    std::wstring BuildPresetLabel() const;

private:
    void PublishActivity(const SourceActivity& activity, bool force);

    std::unique_ptr<IDiscordPresenceBackend> backend_;
    PresenceSource& source_;
    Logger& logger_;
    AppConfig config_;
    std::filesystem::path authStoragePath_;
    bool initialized_ = false;
    bool paused_ = false;
    bool lastPublishedWasClear_ = false;
    std::optional<std::string> lastPublishedIdentity_;
    std::optional<std::chrono::system_clock::time_point> presetStartedAt_;
};

std::unique_ptr<IDiscordPresenceBackend> CreateDiscordPresenceBackend();

}  // namespace drpc
