#include "DiscordPresenceService.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include <windows.h>
#include <wincrypt.h>

#if defined(DRPC_WITH_DISCORD_SDK)
#define DISCORDPP_IMPLEMENTATION
#include <discordpp.h>
#endif

namespace drpc {
namespace {

using json = nlohmann::json;

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

std::int64_t CurrentUnixSeconds() {
    return static_cast<std::int64_t>(std::time(nullptr));
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
    bool Initialize(const AppConfig& config,
                    const std::filesystem::path&,
                    Logger& logger) override {
        if (config.applicationId.empty() || config.applicationId == "1491798009942507712") {
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

    void Authenticate(Logger& logger) override {
        logger.Warn("Discord authentication is unavailable because the SDK is not built in.");
    }

    void ResetAuthentication(Logger& logger) override {
        logger.Warn("Discord authentication reset is unavailable because the SDK is not built in.");
    }

    std::wstring StatusText() const override {
        return L"SDK missing";
    }

    bool IsAvailable() const override {
        return false;
    }

    bool IsAuthenticated() const override {
        return false;
    }
};

#if defined(DRPC_WITH_DISCORD_SDK)
struct StoredDiscordToken {
    std::string accessToken;
    std::string refreshToken;
    std::string scopes;
    discordpp::AuthorizationTokenType tokenType = discordpp::AuthorizationTokenType::Bearer;
    std::int64_t expiresAtUnixSeconds = 0;
};

std::string TokenTypeToString(discordpp::AuthorizationTokenType tokenType) {
    switch (tokenType) {
    case discordpp::AuthorizationTokenType::Bearer:
        return "Bearer";
    case discordpp::AuthorizationTokenType::User:
    default:
        return "User";
    }
}

discordpp::AuthorizationTokenType ParseTokenType(std::string_view value) {
    if (value == "User") {
        return discordpp::AuthorizationTokenType::User;
    }
    return discordpp::AuthorizationTokenType::Bearer;
}

std::string BuildClientResultSummary(const discordpp::ClientResult& result) {
    std::ostringstream line;
    if (!result.Error().empty()) {
        line << " error=\"" << result.Error() << '"';
    }
    if (result.ErrorCode() != 0) {
        line << " errorCode=" << result.ErrorCode();
    }
    if (result.Status() != discordpp::HttpStatusCode::None) {
        line << " httpStatus=" << static_cast<int>(result.Status());
    }
    if (result.Type() != discordpp::ErrorType::None) {
        line << " type=" << static_cast<int>(result.Type());
    }
    if (result.Retryable()) {
        line << " retryable=true";
    }
    return line.str();
}

std::optional<StoredDiscordToken> LoadStoredDiscordToken(const std::filesystem::path& path, Logger& logger) {
    if (path.empty() || !std::filesystem::exists(path)) {
        return std::nullopt;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        logger.Warn("Failed to open Discord auth token file: " + path.string());
        return std::nullopt;
    }

    try {
        const std::string encryptedBytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        std::string tokenJson = encryptedBytes;

        DATA_BLOB inputBlob{};
        inputBlob.pbData = reinterpret_cast<BYTE*>(tokenJson.data());
        inputBlob.cbData = static_cast<DWORD>(tokenJson.size());
        DATA_BLOB outputBlob{};
        if (CryptUnprotectData(&inputBlob, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &outputBlob)) {
            tokenJson.assign(reinterpret_cast<char*>(outputBlob.pbData), outputBlob.cbData);
            LocalFree(outputBlob.pbData);
        }

        json root = json::parse(tokenJson);

        StoredDiscordToken token;
        token.accessToken = root.value("accessToken", "");
        token.refreshToken = root.value("refreshToken", "");
        token.scopes = root.value("scopes", "");
        token.tokenType = ParseTokenType(root.value("tokenType", "Bearer"));
        token.expiresAtUnixSeconds = root.value("expiresAtUnixSeconds", 0LL);
        if (token.accessToken.empty()) {
            return std::nullopt;
        }

        logger.Info("Loaded Discord auth token from " + path.string() + ".");
        return token;
    } catch (const std::exception& ex) {
        logger.Warn(std::string("Failed to parse Discord auth token file: ") + ex.what());
        return std::nullopt;
    }
}

void SaveStoredDiscordToken(const std::filesystem::path& path, const StoredDiscordToken& token, Logger& logger) {
    if (path.empty()) {
        return;
    }

    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    json root;
    root["accessToken"] = token.accessToken;
    root["refreshToken"] = token.refreshToken;
    root["scopes"] = token.scopes;
    root["tokenType"] = TokenTypeToString(token.tokenType);
    root["expiresAtUnixSeconds"] = token.expiresAtUnixSeconds;

    const auto serialized = root.dump(2);
    DATA_BLOB inputBlob{};
    inputBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(serialized.data()));
    inputBlob.cbData = static_cast<DWORD>(serialized.size());
    DATA_BLOB outputBlob{};
    if (!CryptProtectData(&inputBlob, L"drpc.discordAuth", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &outputBlob)) {
        logger.Warn("Failed to encrypt Discord auth token file: " + path.string());
        return;
    }

    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        LocalFree(outputBlob.pbData);
        logger.Warn("Failed to write Discord auth token file: " + path.string());
        return;
    }

    stream.write(reinterpret_cast<const char*>(outputBlob.pbData), outputBlob.cbData);
    LocalFree(outputBlob.pbData);
    logger.Info("Stored Discord auth token at " + path.string() + ".");
}

void ClearStoredDiscordToken(const std::filesystem::path& path, Logger& logger) {
    if (path.empty() || !std::filesystem::exists(path)) {
        return;
    }

    std::error_code error;
    std::filesystem::remove(path, error);
    if (error) {
        logger.Warn("Failed to remove Discord auth token file: " + path.string());
        return;
    }

    logger.Info("Removed Discord auth token file: " + path.string() + ".");
}

std::string BuildUserLabel(const discordpp::UserHandle& user) {
    const auto displayName = user.DisplayName();
    if (!displayName.empty()) {
        return displayName;
    }

    const auto globalName = user.GlobalName();
    if (globalName.has_value() && !globalName.value().empty()) {
        return globalName.value();
    }

    return user.Username();
}

class DiscordSocialSdkPresenceBackend final : public IDiscordPresenceBackend {
public:
    bool Initialize(const AppConfig& config,
                    const std::filesystem::path& authStoragePath,
                    Logger& logger) override {
        config_ = config;
        authStoragePath_ = authStoragePath;

        const auto parsedId = ParseApplicationId(config.applicationId);
        if (!parsedId.has_value()) {
            logger.Error("Discord applicationId is missing or invalid. Expected a numeric Discord application ID.");
            statusText_ = L"Invalid app ID";
            return false;
        }

        applicationId_ = parsedId.value();
        directMode_ = !config_.discordAuth.enabled;

        if (!CreateClient(logger)) {
            return false;
        }

        if (directMode_) {
            authenticated_ = true;
            statusText_ = L"SDK direct";
            logger.Info("Discord Social SDK backend initialized in direct Rich Presence mode.");
            return true;
        }

        logger.Info("Discord Social SDK backend initialized in authenticated mode.");
        storedToken_ = LoadStoredDiscordToken(authStoragePath_, logger);
        if (storedToken_.has_value()) {
            if (ShouldRefreshToken(storedToken_.value())) {
                RefreshAuthenticationToken(logger);
            } else {
                ApplyToken(storedToken_.value(), logger);
            }
        } else {
            statusText_ = L"Sign-in required";
            if (config_.discordAuth.autoAuthenticate) {
                StartAuthentication(logger);
            }
        }

        return true;
    }

    void Publish(const ActivityPreset& preset, Logger& logger) override {
        if (!client_) {
            logger.Warn("Skipped publish because the Discord backend is not ready.");
            return;
        }

        if (!directMode_ && (!authenticated_ || client_->GetStatus() != discordpp::Client::Status::Ready)) {
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
                logger.Error("Discord Rich Presence update failed for preset: " + presetName + BuildClientResultSummary(result));
            }
        });
    }

    void Clear(Logger& logger) override {
        if (!client_) {
            return;
        }

        if (!directMode_ && (!authenticated_ || client_->GetStatus() != discordpp::Client::Status::Ready)) {
            return;
        }

        client_->ClearRichPresence();
        logger.Info("Cleared Discord Rich Presence.");
    }

    void PumpCallbacks(Logger&) override {
        discordpp::RunCallbacks();
    }

    void Authenticate(Logger& logger) override {
        if (directMode_) {
            logger.Info("Ignoring explicit authentication request because direct Rich Presence mode is enabled.");
            return;
        }

        StartAuthentication(logger);
    }

    void ResetAuthentication(Logger& logger) override {
        if (directMode_) {
            logger.Info("Ignoring authentication reset because direct Rich Presence mode is enabled.");
            return;
        }

        logger.Info("Resetting Discord authentication state.");
        authenticated_ = false;
        authInFlight_ = false;
        currentUserName_.clear();
        storedToken_.reset();
        codeVerifier_.reset();
        ClearStoredDiscordToken(authStoragePath_, logger);
        CreateClient(logger);
        statusText_ = L"Sign-in required";
        StartAuthentication(logger);
    }

    std::wstring StatusText() const override {
        return statusText_;
    }

    bool IsAvailable() const override {
        return available_;
    }

    bool IsAuthenticated() const override {
        return authenticated_;
    }

private:
    bool CreateClient(Logger& logger) {
        try {
            client_ = std::make_shared<discordpp::Client>();
            client_->SetApplicationId(applicationId_);
            client_->AddLogCallback(
                [&logger](std::string message, discordpp::LoggingSeverity severity) {
                    switch (severity) {
                    case discordpp::LoggingSeverity::Error:
                        logger.Error("Discord SDK: " + message);
                        break;
                    case discordpp::LoggingSeverity::Warning:
                        logger.Warn("Discord SDK: " + message);
                        break;
                    case discordpp::LoggingSeverity::Info:
                    case discordpp::LoggingSeverity::Verbose:
                    case discordpp::LoggingSeverity::None:
                    default:
                        logger.Info("Discord SDK: " + message);
                        break;
                    }
                },
                discordpp::LoggingSeverity::Warning);
            client_->SetStatusChangedCallback(
                [this, &logger](discordpp::Client::Status status, discordpp::Client::Error error, int32_t errorDetail) {
                    HandleClientStatusChanged(status, error, errorDetail, logger);
                });
            client_->SetTokenExpirationCallback([this, &logger]() {
                logger.Info("Discord SDK requested token refresh.");
                RefreshAuthenticationToken(logger);
            });
            available_ = true;
            authenticated_ = directMode_;
            currentUserName_.clear();
            return true;
        } catch (const std::exception& ex) {
            logger.Error(std::string("Failed to initialize Discord Social SDK client: ") + ex.what());
            statusText_ = L"SDK unavailable";
            available_ = false;
            client_.reset();
            return false;
        } catch (...) {
            logger.Error("Failed to initialize Discord Social SDK client with an unknown error.");
            statusText_ = L"SDK unavailable";
            available_ = false;
            client_.reset();
            return false;
        }
    }

    std::string EffectiveScopes() const {
        if (!config_.discordAuth.scopes.empty()) {
            return config_.discordAuth.scopes;
        }

        return discordpp::Client::GetDefaultPresenceScopes();
    }

    bool ShouldRefreshToken(const StoredDiscordToken& token) const {
        if (!config_.discordAuth.autoRefresh) {
            return false;
        }

        if (token.expiresAtUnixSeconds <= 0) {
            return !token.refreshToken.empty();
        }

        const auto refreshAfter = CurrentUnixSeconds() + static_cast<std::int64_t>(config_.discordAuth.refreshLeewaySeconds);
        return token.expiresAtUnixSeconds <= refreshAfter && !token.refreshToken.empty();
    }

    void StartAuthentication(Logger& logger) {
        if (!client_ || authInFlight_) {
            return;
        }

        if (config_.discordAuth.useDeviceAuth) {
            StartDeviceAuthentication(logger);
            return;
        }

        StartDesktopAuthentication(logger);
    }

    void StartDesktopAuthentication(Logger& logger) {
        if (!client_) {
            return;
        }

        logger.Info("Starting Discord OAuth authorization flow.");
        authInFlight_ = true;
        authenticated_ = false;
        statusText_ = L"Authorizing Discord";

        codeVerifier_ = client_->CreateAuthorizationCodeVerifier();
        discordpp::AuthorizationArgs args;
        args.SetClientId(applicationId_);
        args.SetScopes(EffectiveScopes());
        args.SetCodeChallenge(std::make_optional(codeVerifier_.value().Challenge()));

        client_->Authorize(args, [this, &logger](const discordpp::ClientResult& result, std::string code, std::string redirectUri) {
            if (!result.Successful()) {
                authInFlight_ = false;
                statusText_ = L"Sign-in failed";
                logger.Error("Discord authorization failed." + BuildClientResultSummary(result));
                return;
            }

            if (!config_.discordAuth.redirectUri.empty() && redirectUri != config_.discordAuth.redirectUri) {
                logger.Warn("Discord SDK returned redirect URI \"" + redirectUri +
                            "\" which differs from configured discordAuth.redirectUri \"" +
                            config_.discordAuth.redirectUri + "\".");
            }

            logger.Info("Discord authorization code received. Exchanging for tokens.");
            statusText_ = L"Exchanging token";
            client_->GetToken(
                applicationId_,
                code,
                codeVerifier_.value().Verifier(),
                redirectUri,
                [this, &logger](const discordpp::ClientResult& tokenResult,
                                std::string accessToken,
                                std::string refreshToken,
                                discordpp::AuthorizationTokenType tokenType,
                                int32_t expiresIn,
                                std::string scopes) {
                    HandleTokenExchange(tokenResult,
                                        std::move(accessToken),
                                        std::move(refreshToken),
                                        tokenType,
                                        expiresIn,
                                        std::move(scopes),
                                        logger);
                });
        });
    }

    void StartDeviceAuthentication(Logger& logger) {
        if (!client_) {
            return;
        }

        logger.Info("Starting Discord device authorization flow.");
        authInFlight_ = true;
        authenticated_ = false;
        statusText_ = L"Waiting for Discord sign-in";

        discordpp::DeviceAuthorizationArgs args;
        args.SetClientId(applicationId_);
        args.SetScopes(EffectiveScopes());
        client_->GetTokenFromDevice(
            args,
            [this, &logger](const discordpp::ClientResult& result,
                            std::string accessToken,
                            std::string refreshToken,
                            discordpp::AuthorizationTokenType tokenType,
                            int32_t expiresIn,
                            std::string scopes) {
                HandleTokenExchange(result,
                                    std::move(accessToken),
                                    std::move(refreshToken),
                                    tokenType,
                                    expiresIn,
                                    std::move(scopes),
                                    logger);
            });
    }

    void RefreshAuthenticationToken(Logger& logger) {
        if (directMode_ || !client_) {
            return;
        }

        if (!storedToken_.has_value() || storedToken_->refreshToken.empty()) {
            logger.Warn("No refresh token available. Restarting Discord sign-in.");
            ClearStoredDiscordToken(authStoragePath_, logger);
            storedToken_.reset();
            authInFlight_ = false;
            statusText_ = L"Sign-in required";
            if (config_.discordAuth.autoAuthenticate) {
                StartAuthentication(logger);
            }
            return;
        }

        logger.Info("Refreshing Discord access token.");
        authInFlight_ = true;
        authenticated_ = false;
        statusText_ = L"Refreshing login";
        client_->RefreshToken(
            applicationId_,
            storedToken_->refreshToken,
            [this, &logger](const discordpp::ClientResult& result,
                            std::string accessToken,
                            std::string refreshToken,
                            discordpp::AuthorizationTokenType tokenType,
                            int32_t expiresIn,
                            std::string scopes) {
                HandleTokenExchange(result,
                                    std::move(accessToken),
                                    std::move(refreshToken),
                                    tokenType,
                                    expiresIn,
                                    std::move(scopes),
                                    logger);
            });
    }

    void HandleTokenExchange(const discordpp::ClientResult& result,
                             std::string accessToken,
                             std::string refreshToken,
                             discordpp::AuthorizationTokenType tokenType,
                             int32_t expiresIn,
                             std::string scopes,
                             Logger& logger) {
        if (!result.Successful()) {
            authInFlight_ = false;
            authenticated_ = false;
            statusText_ = L"Sign-in failed";
            logger.Error("Discord token exchange failed." + BuildClientResultSummary(result));
            return;
        }

        StoredDiscordToken token;
        token.accessToken = std::move(accessToken);
        token.refreshToken = std::move(refreshToken);
        token.scopes = std::move(scopes);
        token.tokenType = tokenType;
        token.expiresAtUnixSeconds = CurrentUnixSeconds() + std::max(expiresIn, 0);

        storedToken_ = token;
        SaveStoredDiscordToken(authStoragePath_, token, logger);
        ApplyToken(token, logger);
    }

    void ApplyToken(const StoredDiscordToken& token, Logger& logger) {
        if (!client_) {
            authInFlight_ = false;
            return;
        }

        logger.Info("Applying Discord bearer token to SDK client.");
        statusText_ = L"Connecting Discord";
        client_->UpdateToken(token.tokenType, token.accessToken, [this, &logger](const discordpp::ClientResult& result) {
            if (!result.Successful()) {
                authInFlight_ = false;
                authenticated_ = false;
                statusText_ = L"Token rejected";
                logger.Error("Discord token update failed." + BuildClientResultSummary(result));
                ClearStoredDiscordToken(authStoragePath_, logger);
                storedToken_.reset();
                return;
            }

            logger.Info("Discord token accepted. Connecting client session.");
            statusText_ = L"Connecting Discord";
            client_->Connect();
        });
    }

    void HandleClientStatusChanged(discordpp::Client::Status status,
                                   discordpp::Client::Error error,
                                   int32_t errorDetail,
                                   Logger& logger) {
        std::ostringstream line;
        line << "Discord client status changed to " << discordpp::Client::StatusToString(status)
             << " error=" << discordpp::Client::ErrorToString(error)
             << " errorDetail=" << errorDetail;
        logger.Info(line.str());

        switch (status) {
        case discordpp::Client::Status::Ready:
            authInFlight_ = false;
            authenticated_ = true;
            UpdateCurrentUser();
            if (!currentUserName_.empty()) {
                statusText_ = L"Linked: " + ToWide(currentUserName_);
            } else {
                statusText_ = L"Linked";
            }
            break;
        case discordpp::Client::Status::Connecting:
        case discordpp::Client::Status::Connected:
        case discordpp::Client::Status::HttpWait:
            statusText_ = L"Connecting Discord";
            break;
        case discordpp::Client::Status::Reconnecting:
            authenticated_ = false;
            statusText_ = L"Reconnecting";
            break;
        case discordpp::Client::Status::Disconnecting:
            authenticated_ = false;
            statusText_ = L"Disconnecting";
            break;
        case discordpp::Client::Status::Disconnected:
        default:
            authenticated_ = false;
            currentUserName_.clear();
            if (authInFlight_) {
                statusText_ = L"Authorizing Discord";
            } else if (storedToken_.has_value()) {
                statusText_ = L"Disconnected";
            } else {
                statusText_ = L"Sign-in required";
            }
            break;
        }
    }

    void UpdateCurrentUser() {
        if (!client_) {
            return;
        }

        const auto user = client_->GetCurrentUserV2();
        if (!user.has_value()) {
            currentUserName_.clear();
            return;
        }

        currentUserName_ = BuildUserLabel(user.value());
    }

    AppConfig config_;
    std::filesystem::path authStoragePath_;
    std::shared_ptr<discordpp::Client> client_;
    std::uint64_t applicationId_ = 0;
    std::optional<StoredDiscordToken> storedToken_;
    std::optional<discordpp::AuthorizationCodeVerifier> codeVerifier_;
    std::wstring statusText_ = L"Initializing";
    std::string currentUserName_;
    bool available_ = false;
    bool directMode_ = false;
    bool authenticated_ = false;
    bool authInFlight_ = false;
};
#endif

}  // namespace

DiscordPresenceService::DiscordPresenceService(std::unique_ptr<IDiscordPresenceBackend> backend,
                                               PresenceSource& source,
                                               Logger& logger,
                                               AppConfig config,
                                               std::filesystem::path authStoragePath)
    : backend_(std::move(backend)),
      source_(source),
      logger_(logger),
      config_(std::move(config)),
      authStoragePath_(std::move(authStoragePath)) {
}

void DiscordPresenceService::Initialize() {
    initialized_ = backend_->Initialize(config_, authStoragePath_, logger_);
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
    if (paused_) {
        paused_ = false;
    }

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

void DiscordPresenceService::Authenticate() {
    backend_->Authenticate(logger_);
}

void DiscordPresenceService::ResetAuthentication() {
    backend_->ResetAuthentication(logger_);
}

bool DiscordPresenceService::IsPaused() const {
    return paused_;
}

bool DiscordPresenceService::IsAuthenticated() const {
    return backend_->IsAuthenticated();
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
