#pragma once

#include "BrowserActivitySource.h"
#include "Config.h"
#include "DiscordPresenceService.h"
#include "Logging.h"
#include "PresenceSource.h"

#include <filesystem>
#include <memory>

#include <windows.h>
#include <shellapi.h>

namespace drpc {

class TrayApplication {
public:
    explicit TrayApplication(HINSTANCE instance);
    int Run();

private:
    static constexpr UINT kTrayIconId = 1001;
    static constexpr UINT kWindowMessageTrayIcon = WM_APP + 1;
    static constexpr UINT kWindowMessageSourceUpdated = WM_APP + 2;
    static constexpr UINT_PTR kTimerPublish = 2001;
    static constexpr UINT_PTR kTimerPumpCallbacks = 2002;

    static constexpr UINT kMenuNextPreset = 3001;
    static constexpr UINT kMenuPreviousPreset = 3002;
    static constexpr UINT kMenuPauseResume = 3003;
    static constexpr UINT kMenuOpenLogs = 3004;
    static constexpr UINT kMenuQuit = 3005;

    bool Initialize();
    void Shutdown();
    bool CreateMessageWindow();
    void RegisterWindowClass();
    void AddTrayIcon();
    void RemoveTrayIcon();
    void ShowContextMenu(POINT cursorPosition);
    void UpdateTrayTooltip();
    void OpenLogs() const;
    void HandleCommand(WORD commandId);
    void OnTimer(UINT_PTR timerId);
    void PublishCurrentActivity(bool force);

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    HINSTANCE instance_;
    HWND windowHandle_ = nullptr;
    NOTIFYICONDATAW notifyIconData_{};
    std::filesystem::path executableDirectory_;
    std::filesystem::path configPath_;
    std::filesystem::path workspaceConfigPath_;
    std::filesystem::path logPath_;
    AppConfig config_;
    std::unique_ptr<Logger> logger_;
    std::unique_ptr<PresenceSource> source_;
    std::unique_ptr<DiscordPresenceService> presenceService_;
};

}  // namespace drpc
