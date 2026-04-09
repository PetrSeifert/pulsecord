#include "DiscordPresenceService.h"

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

std::string StatusDisplayTypeToString(StatusDisplayType type) {
    switch (type) {
    case StatusDisplayType::State:
        return "state";
    case StatusDisplayType::Details:
        return "details";
    case StatusDisplayType::Name:
    default:
        return "name";
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
        activity.SetType(discordpp::ActivityTypes::Playing);
        activity.SetDetails(preset.details);
        activity.SetState(preset.state);

        if (!preset.detailsUrl.empty()) {
            activity.SetDetailsUrl(preset.detailsUrl);
        }
        if (!preset.stateUrl.empty()) {
            activity.SetStateUrl(preset.stateUrl);
        }

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
            timestamps.SetStart(static_cast<std::time_t>(preset.startedAtUnixSeconds.value()));
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
                logger.Error("Discord Rich Presence update failed for preset: " + presetName);
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
    PublishPreset(source_.Current(), force);
}

void DiscordPresenceService::NextPreset() {
    PublishPreset(source_.Next(), true);
}

void DiscordPresenceService::PreviousPreset() {
    PublishPreset(source_.Previous(), true);
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
        status << ToWide(source_.Current().name);
    }
    status << L" | " << backend_->StatusText();
    return status.str();
}

std::wstring DiscordPresenceService::BuildPresetLabel() const {
    std::wostringstream label;
    label << L"Preset " << (source_.CurrentIndex() + 1) << L"/" << source_.Count() << L": " << ToWide(source_.Current().name);
    return label.str();
}

void DiscordPresenceService::PublishPreset(const ActivityPreset& preset, bool force) {
    if (paused_) {
        return;
    }

    const auto currentIndex = source_.CurrentIndex();
    if (force || !lastPublishedIndex_.has_value() || lastPublishedIndex_.value() != currentIndex) {
        presetStartedAt_ = std::chrono::system_clock::now();
    }

    ActivityPreset snapshot = preset;
    if (snapshot.showElapsedTime && presetStartedAt_.has_value()) {
        snapshot.startedAtUnixSeconds = std::chrono::system_clock::to_time_t(presetStartedAt_.value());
    } else {
        snapshot.startedAtUnixSeconds.reset();
    }

    backend_->Publish(snapshot, logger_);
    lastPublishedIndex_ = currentIndex;
}

std::unique_ptr<IDiscordPresenceBackend> CreateDiscordPresenceBackend() {
#if defined(DRPC_WITH_DISCORD_SDK)
    return std::make_unique<DiscordSocialSdkPresenceBackend>();
#else
    return std::make_unique<NoopDiscordPresenceBackend>();
#endif
}

}  // namespace drpc
