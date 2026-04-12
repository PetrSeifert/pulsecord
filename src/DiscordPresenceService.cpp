#include "DiscordPresenceService.h"

#include <algorithm>
#include <ctime>
#include <sstream>

#if defined(DRPC_WITH_DISCORD_SDK)
#define DISCORDPP_IMPLEMENTATION
#include <discordpp.h>
#endif

namespace drpc {
namespace {

std::optional<std::uint64_t> ParseApplicationId(const std::string& value) {
    if (value.empty()) {
        return std::nullopt;
    }

    try {
        return std::stoull(value);
    } catch (...) {
        return std::nullopt;
    }
}

bool HasAssets(const ActivityAssets& assets) {
    return !assets.largeImage.empty() || !assets.largeText.empty() || !assets.largeUrl.empty() ||
           !assets.smallImage.empty() || !assets.smallText.empty() || !assets.smallUrl.empty();
}

discordpp::ActivityTypes ToDiscordActivityType(ActivityType type) {
    switch (type) {
    case ActivityType::Streaming:
        return discordpp::ActivityTypes::Streaming;
    case ActivityType::Listening:
        return discordpp::ActivityTypes::Listening;
    case ActivityType::Watching:
        return discordpp::ActivityTypes::Watching;
    case ActivityType::Competing:
        return discordpp::ActivityTypes::Competing;
    case ActivityType::Playing:
    default:
        return discordpp::ActivityTypes::Playing;
    }
}

class NoopDiscordPresenceBackend final : public IDiscordPresenceBackend {
public:
    bool Initialize(const std::string& applicationId, Logger& logger) override {
        if (applicationId.empty() || applicationId == "1491798009942507712") {
            logger.Warn("Discord backend is disabled because applicationId is not configured yet.");
        } else {
            logger.Warn("Discord Social SDK is not available in this build. Presence updates will be logged only.");
        }
        return false;
    }

    void Publish(const ActivityPreset& preset, Logger& logger) override {
        std::ostringstream line;
        line << "No-op publish for preset \"" << preset.name << "\""
             << " details=\"" << preset.details << "\""
             << " state=\"" << preset.state << "\""
             << " statusDisplayType=" << StatusDisplayTypeToString(preset.statusDisplayType);
        logger.Info(line.str());
    }

    void Clear(Logger& logger) override {
        logger.Info("No-op clear rich presence.");
    }

    void PumpCallbacks(Logger&) override {
    }

    std::wstring StatusText() const override {
        return L"SDK missing";
    }

    bool IsAvailable() const override {
        return false;
    }
};

#if defined(DRPC_WITH_DISCORD_SDK)
class DiscordSocialSdkPresenceBackend final : public IDiscordPresenceBackend {
public:
    bool Initialize(const std::string& applicationId, Logger& logger) override {
        const auto parsedId = ParseApplicationId(applicationId);
        if (!parsedId.has_value()) {
            logger.Error("Discord applicationId is missing or invalid. Expected a numeric Discord application ID.");
            return false;
        }

        try {
            client_ = std::make_shared<discordpp::Client>();
            client_->SetApplicationId(parsedId.value());
            available_ = true;
            logger.Info("Discord Social SDK backend initialized for direct Rich Presence.");
            return true;
        } catch (const std::exception& ex) {
            logger.Error(std::string("Failed to initialize Discord Social SDK client: ") + ex.what());
            available_ = false;
            return false;
        } catch (...) {
            logger.Error("Failed to initialize Discord Social SDK client with an unknown error.");
            available_ = false;
            return false;
        }
    }

    void Publish(const ActivityPreset& preset, Logger& logger) override {
        if (!available_ || !client_) {
            logger.Warn("Skipped publish because the Discord backend is not ready.");
            return;
        }

        discordpp::Activity activity;
        activity.SetType(ToDiscordActivityType(preset.type));
        if (!preset.details.empty()) {
            activity.SetDetails(preset.details);
        }
        if (!preset.state.empty()) {
            activity.SetState(preset.state);
        }

        if (!preset.detailsUrl.empty()) {
            activity.SetDetailsUrl(preset.detailsUrl);
        }
        if (!preset.stateUrl.empty()) {
            activity.SetStateUrl(preset.stateUrl);
        }

        if (HasAssets(preset.assets)) {
            discordpp::ActivityAssets assets;
            if (!preset.assets.largeImage.empty()) {
                assets.SetLargeImage(preset.assets.largeImage);
            }
            if (!preset.assets.largeText.empty()) {
                assets.SetLargeText(preset.assets.largeText);
            }
            if (!preset.assets.largeUrl.empty()) {
                assets.SetLargeUrl(preset.assets.largeUrl);
            }
            if (!preset.assets.smallImage.empty()) {
                assets.SetSmallImage(preset.assets.smallImage);
            }
            if (!preset.assets.smallText.empty()) {
                assets.SetSmallText(preset.assets.smallText);
            }
            if (!preset.assets.smallUrl.empty()) {
                assets.SetSmallUrl(preset.assets.smallUrl);
            }
            activity.SetAssets(assets);
        }

        switch (preset.statusDisplayType) {
        case StatusDisplayType::State:
            activity.SetStatusDisplayType(discordpp::StatusDisplayTypes::State);
            break;
        case StatusDisplayType::Details:
            activity.SetStatusDisplayType(discordpp::StatusDisplayTypes::Details);
            break;
        case StatusDisplayType::Name:
        default:
            activity.SetStatusDisplayType(discordpp::StatusDisplayTypes::Name);
            break;
        }

        if (preset.showElapsedTime && preset.startedAtUnixSeconds.has_value()) {
            discordpp::ActivityTimestamps timestamps;
            timestamps.SetStart(static_cast<std::uint64_t>(preset.startedAtUnixSeconds.value()) * 1000ULL);
            if (preset.endAtUnixSeconds.has_value()) {
                timestamps.SetEnd(static_cast<std::uint64_t>(preset.endAtUnixSeconds.value()) * 1000ULL);
            }
            activity.SetTimestamps(timestamps);
        }

        for (const auto& button : preset.buttons) {
            discordpp::ActivityButton activityButton;
            activityButton.SetLabel(button.label);
            activityButton.SetUrl(button.url);
            activity.AddButton(activityButton);
        }

        client_->UpdateRichPresence(activity, [&logger, presetName = preset.name](const discordpp::ClientResult& result) {
            if (result.Successful()) {
                logger.Info("Published Discord Rich Presence preset: " + presetName);
            } else {
                std::ostringstream line;
                line << "Discord Rich Presence update failed for preset: " << presetName;
                const auto error = result.Error();
                if (!error.empty()) {
                    line << " error=\"" << error << '"';
                }
                if (result.ErrorCode() != 0) {
                    line << " errorCode=" << result.ErrorCode();
                }
                if (result.Retryable()) {
                    line << " retryable=true";
                }
                logger.Error(line.str());
            }
        });
    }

    void Clear(Logger& logger) override {
        if (!available_ || !client_) {
            return;
        }

        client_->ClearRichPresence();
        logger.Info("Cleared Discord Rich Presence.");
    }

    void PumpCallbacks(Logger&) override {
        discordpp::RunCallbacks();
    }

    std::wstring StatusText() const override {
        return available_ ? L"SDK active" : L"SDK unavailable";
    }

    bool IsAvailable() const override {
        return available_;
    }

private:
    std::shared_ptr<discordpp::Client> client_;
    bool available_ = false;
};
#endif

}  // namespace

DiscordPresenceService::DiscordPresenceService(std::unique_ptr<IDiscordPresenceBackend> backend,
                                               PresenceSource& source,
                                               Logger& logger,
                                               std::string applicationId)
    : backend_(std::move(backend)), source_(source), logger_(logger), applicationId_(std::move(applicationId)) {
}

void DiscordPresenceService::Initialize() {
    initialized_ = backend_->Initialize(applicationId_, logger_);
    presetStartedAt_ = std::chrono::system_clock::now();
    PublishCurrent(true);
}

void DiscordPresenceService::PublishCurrent(bool force) {
    PublishActivity(source_.Current(), force);
}

void DiscordPresenceService::NextPreset() {
    if (source_.Next()) {
        PublishCurrent(true);
    }
}

void DiscordPresenceService::PreviousPreset() {
    if (source_.Previous()) {
        PublishCurrent(true);
    }
}

void DiscordPresenceService::Pause() {
    if (paused_) {
        return;
    }

    paused_ = true;
    Clear();
    logger_.Info("Presence updates paused.");
}

void DiscordPresenceService::Resume() {
    if (!paused_) {
        return;
    }

    paused_ = false;
    presetStartedAt_ = std::chrono::system_clock::now();
    logger_.Info("Presence updates resumed.");
    PublishCurrent(true);
}

void DiscordPresenceService::Clear() {
    backend_->Clear(logger_);
    lastPublishedWasClear_ = true;
    lastPublishedIdentity_ = "manual-clear";
    presetStartedAt_.reset();
}

void DiscordPresenceService::PumpCallbacks() {
    backend_->PumpCallbacks(logger_);
}

bool DiscordPresenceService::IsPaused() const {
    return paused_;
}

std::wstring DiscordPresenceService::BuildStatusText() const {
    std::wostringstream status;
    status << L"drpc | ";
    if (paused_) {
        status << L"Paused";
    } else {
        const auto activity = source_.Current();
        status << (activity.label.empty() ? L"Activity" : activity.label);
    }

    const auto sourceStatus = source_.SourceStatus();
    if (!sourceStatus.empty()) {
        status << L" | " << sourceStatus;
    }
    status << L" | " << backend_->StatusText();
    return status.str();
}

std::wstring DiscordPresenceService::BuildPresetLabel() const {
    return source_.BuildMenuLabel();
}

void DiscordPresenceService::PublishActivity(const SourceActivity& activity, bool force) {
    if (paused_) {
        return;
    }

    if (activity.disposition == SourceActivityDisposition::Clear || !activity.preset.has_value()) {
        if (force || !lastPublishedWasClear_ || !lastPublishedIdentity_.has_value() || lastPublishedIdentity_.value() != activity.identity) {
            backend_->Clear(logger_);
        }
        lastPublishedWasClear_ = true;
        lastPublishedIdentity_ = activity.identity;
        presetStartedAt_.reset();
        return;
    }

    if (!force && !lastPublishedWasClear_ && lastPublishedIdentity_.has_value() && lastPublishedIdentity_.value() == activity.identity) {
        return;
    }

    if (force || lastPublishedWasClear_ || !lastPublishedIdentity_.has_value() || lastPublishedIdentity_.value() != activity.identity) {
        presetStartedAt_ = std::chrono::system_clock::now();
    }

    ActivityPreset snapshot = activity.preset.value();
    if (snapshot.showElapsedTime) {
        if (!snapshot.startedAtUnixSeconds.has_value() && presetStartedAt_.has_value()) {
            snapshot.startedAtUnixSeconds = std::chrono::system_clock::to_time_t(presetStartedAt_.value());
        }
    } else {
        snapshot.startedAtUnixSeconds.reset();
        snapshot.endAtUnixSeconds.reset();
    }

    snapshot = SanitizeDiscordActivityPreset(snapshot);
    backend_->Publish(snapshot, logger_);
    lastPublishedWasClear_ = false;
    lastPublishedIdentity_ = activity.identity;
}

std::unique_ptr<IDiscordPresenceBackend> CreateDiscordPresenceBackend() {
#if defined(DRPC_WITH_DISCORD_SDK)
    return std::make_unique<DiscordSocialSdkPresenceBackend>();
#else
    return std::make_unique<NoopDiscordPresenceBackend>();
#endif
}

}  // namespace drpc
