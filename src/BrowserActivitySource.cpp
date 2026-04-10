#include "BrowserActivitySource.h"

#include "BrowserActivityProtocol.h"
#include "Config.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <thread>

#include <windows.h>

namespace drpc {
namespace {

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

const ActivityPreset& FindPresetOrDefault(const std::vector<ActivityPreset>& presets,
                                          std::string_view preferredName,
                                          std::string_view fallbackName) {
    const auto matches = [](const ActivityPreset& preset, std::string_view expected) {
        return ToLower(preset.name) == ToLower(std::string(expected));
    };

    const auto preferred = std::find_if(presets.begin(), presets.end(), [&](const ActivityPreset& preset) {
        return matches(preset, preferredName);
    });
    if (preferred != presets.end()) {
        return *preferred;
    }

    const auto fallback = std::find_if(presets.begin(), presets.end(), [&](const ActivityPreset& preset) {
        return matches(preset, fallbackName);
    });
    if (fallback != presets.end()) {
        return *fallback;
    }

    return presets.front();
}

std::string TrimMessage(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), value.end());
    return value;
}

}  // namespace

BrowserActivitySource::BrowserActivitySource(const AppConfig& config, Logger& logger, std::function<void(bool)> onActivityUpdated)
    : config_(config.browserDetection),
      logger_(logger),
      onActivityUpdated_(std::move(onActivityUpdated)),
      activeTemplate_(FindPresetOrDefault(config.presets, "Watching Video", config.browserDetection.fallbackPreset)),
      fallbackTemplate_(FindPresetOrDefault(config.presets, config.browserDetection.fallbackPreset, "Idle")) {
    if (config_.enabled) {
        serverThread_ = std::thread(&BrowserActivitySource::RunPipeServer, this);
    } else {
        logger_.Warn("Browser activity detection is disabled in config; using fallback browser status only.");
    }
}

BrowserActivitySource::~BrowserActivitySource() {
    StopPipeServer();
}

SourceActivity BrowserActivitySource::Current() const {
    return BuildProjection().activity;
}

bool BrowserActivitySource::Next() {
    return false;
}

bool BrowserActivitySource::Previous() {
    return false;
}

bool BrowserActivitySource::SupportsManualSelection() const {
    return false;
}

std::wstring BrowserActivitySource::BuildMenuLabel() const {
    const auto projection = BuildProjection();
    return projection.activity.label.empty() ? L"Browser activity" : L"Browser: " + projection.activity.label;
}

std::wstring BrowserActivitySource::SourceStatus() const {
    if (!config_.enabled) {
        return L"Browser disabled";
    }

    return BuildProjection().sourceStatus;
}

BrowserPresenceProjection BrowserActivitySource::BuildProjection() const {
    std::optional<BrowserActivitySnapshot> snapshot;
    bool hasSeenBrowser = false;
    bool isFresh = false;

    {
        std::scoped_lock lock(mutex_);
        snapshot = latestSnapshot_;
        hasSeenBrowser = hasSeenBrowser_;
        if (latestSnapshot_.has_value()) {
            const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - lastReceivedAt_);
            isFresh = age <= std::chrono::milliseconds(config_.staleAfterMs);
        }
    }

    return ProjectBrowserPresence(snapshot, hasSeenBrowser, isFresh, config_, activeTemplate_, fallbackTemplate_);
}

void BrowserActivitySource::RunPipeServer() {
    while (!stopRequested_) {
        HANDLE pipe = CreateNamedPipeW(
            kBrowserActivityPipeName,
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            0,
            static_cast<DWORD>(kMaxBrowserActivityMessageBytes),
            0,
            nullptr);

        if (pipe == INVALID_HANDLE_VALUE) {
            logger_.Error("Failed to create browser activity named pipe.");
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        const auto connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!connected) {
            CloseHandle(pipe);
            if (!stopRequested_) {
                logger_.Warn("Browser activity pipe connection failed.");
            }
            continue;
        }

        std::string message;
        std::array<char, 4096> buffer{};

        while (!stopRequested_) {
            DWORD bytesRead = 0;
            if (!ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) {
                const auto error = GetLastError();
                if (error != ERROR_BROKEN_PIPE && error != ERROR_OPERATION_ABORTED) {
                    logger_.Warn("Browser activity pipe read failed.");
                }
                break;
            }

            if (bytesRead == 0) {
                break;
            }

            message.append(buffer.data(), bytesRead);
            if (message.size() > kMaxBrowserActivityMessageBytes) {
                logger_.Warn("Discarded oversized browser activity message from the named pipe.");
                message.clear();
                break;
            }
        }

        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);

        message = TrimMessage(std::move(message));
        if (!message.empty() && !stopRequested_) {
            ProcessMessage(std::move(message));
        }
    }
}

void BrowserActivitySource::ProcessMessage(std::string message) {
    BrowserActivitySnapshot snapshot;
    std::string error;
    if (!ParseBrowserActivityMessage(message, snapshot, error)) {
        logger_.Warn("Rejected browser activity payload: " + error);
        return;
    }

    logger_.Info("Accepted browser activity update from " + snapshot.browser + " for host " + snapshot.host + ".");
    const auto incomingIdentity = snapshot.IdentityKey();
    bool shouldForcePublish = true;

    {
        std::scoped_lock lock(mutex_);
        if (latestSnapshot_.has_value()) {
            shouldForcePublish = latestSnapshot_->IdentityKey() != incomingIdentity;
        }
        latestSnapshot_ = std::move(snapshot);
        lastReceivedAt_ = std::chrono::steady_clock::now();
        hasSeenBrowser_ = true;
    }

    if (onActivityUpdated_) {
        onActivityUpdated_(shouldForcePublish);
    }
}

void BrowserActivitySource::StopPipeServer() {
    if (!serverThread_.joinable()) {
        return;
    }

    stopRequested_ = true;
    CancelSynchronousIo(serverThread_.native_handle());
    WakePipeServer();
    serverThread_.join();
}

void BrowserActivitySource::WakePipeServer() const {
    HANDLE pipe = CreateFileW(
        kBrowserActivityPipeName,
        GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);

    if (pipe == INVALID_HANDLE_VALUE) {
        return;
    }

    const std::string noop = "\n";
    DWORD written = 0;
    WriteFile(pipe, noop.data(), static_cast<DWORD>(noop.size()), &written, nullptr);
    CloseHandle(pipe);
}

}  // namespace drpc
