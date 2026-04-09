#include "TrayApplication.h"

#include <cwchar>
#include <stdexcept>

namespace drpc {
namespace {

constexpr std::string_view kPlaceholderApplicationId = "PASTE_YOUR_APPLICATION_ID_HERE";

std::filesystem::path GetExecutableDirectory() {
    wchar_t buffer[MAX_PATH]{};
    const auto length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0) {
        throw std::runtime_error("GetModuleFileNameW failed.");
    }

    return std::filesystem::path(buffer).parent_path();
}

void CopyTooltip(std::wstring_view text, wchar_t (&destination)[128]) {
    wcsncpy_s(destination, _countof(destination), text.data(), _TRUNCATE);
}

bool HasConfiguredApplicationId(const AppConfig& config) {
    return !config.applicationId.empty() && config.applicationId != kPlaceholderApplicationId;
}

}  // namespace

TrayApplication::TrayApplication(HINSTANCE instance) : instance_(instance) {
}

int TrayApplication::Run() {
    if (!Initialize()) {
        return 1;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    Shutdown();
    return static_cast<int>(message.wParam);
}

bool TrayApplication::Initialize() {
    executableDirectory_ = GetExecutableDirectory();
    configPath_ = executableDirectory_ / "config.json";
    workspaceConfigPath_ = executableDirectory_.parent_path() / "config.json";
    logPath_ = executableDirectory_ / "logs" / "drpc.log";

    logger_ = std::make_unique<Logger>(logPath_);
    logger_->Info("Starting drpc tray app.");

    config_ = ConfigLoader::LoadOrCreate(configPath_);
    if (workspaceConfigPath_ != configPath_ && std::filesystem::exists(workspaceConfigPath_)) {
        const auto workspaceConfig = ConfigLoader::LoadOrCreate(workspaceConfigPath_);
        if (!HasConfiguredApplicationId(config_) && HasConfiguredApplicationId(workspaceConfig)) {
            config_ = workspaceConfig;
            logger_->Info("Using workspace config.json because the executable-local config still has the placeholder application ID.");
        }
    }
    source_ = std::make_unique<MockPresenceSource>(config_.presets);
    presenceService_ = std::make_unique<DiscordPresenceService>(
        CreateDiscordPresenceBackend(),
        *source_,
        *logger_,
        config_.applicationId);

    RegisterWindowClass();
    if (!CreateMessageWindow()) {
        logger_->Error("Failed to create tray window.");
        return false;
    }

    AddTrayIcon();
    UpdateTrayTooltip();

    SetTimer(windowHandle_, kTimerPublish, config_.updateIntervalMs, nullptr);
    SetTimer(windowHandle_, kTimerPumpCallbacks, 100, nullptr);

    presenceService_->Initialize();
    UpdateTrayTooltip();
    logger_->Info("drpc initialized successfully.");
    return true;
}

void TrayApplication::Shutdown() {
    if (presenceService_) {
        presenceService_->Clear();
    }

    if (windowHandle_) {
        KillTimer(windowHandle_, kTimerPublish);
        KillTimer(windowHandle_, kTimerPumpCallbacks);
    }

    RemoveTrayIcon();

    if (logger_) {
        logger_->Info("Shutting down drpc tray app.");
    }
}

bool TrayApplication::CreateMessageWindow() {
    windowHandle_ = CreateWindowExW(
        0,
        L"drpc.HiddenWindow",
        L"drpc",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        nullptr,
        nullptr,
        instance_,
        this);

    return windowHandle_ != nullptr;
}

void TrayApplication::RegisterWindowClass() {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &TrayApplication::WindowProc;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = L"drpc.HiddenWindow";
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&windowClass);
}

void TrayApplication::AddTrayIcon() {
    notifyIconData_ = {};
    notifyIconData_.cbSize = sizeof(notifyIconData_);
    notifyIconData_.hWnd = windowHandle_;
    notifyIconData_.uID = kTrayIconId;
    notifyIconData_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    notifyIconData_.uCallbackMessage = kWindowMessageTrayIcon;
    notifyIconData_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    CopyTooltip(L"drpc", notifyIconData_.szTip);

    Shell_NotifyIconW(NIM_ADD, &notifyIconData_);
}

void TrayApplication::RemoveTrayIcon() {
    if (notifyIconData_.cbSize != 0) {
        Shell_NotifyIconW(NIM_DELETE, &notifyIconData_);
    }
}

void TrayApplication::ShowContextMenu(POINT cursorPosition) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }

    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, presenceService_->BuildPresetLabel().c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuPreviousPreset, L"Previous preset");
    AppendMenuW(menu, MF_STRING, kMenuNextPreset, L"Next preset");
    AppendMenuW(menu, MF_STRING, kMenuPauseResume, presenceService_->IsPaused() ? L"Resume updates" : L"Pause updates");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuOpenLogs, L"Open logs");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuQuit, L"Quit");

    SetForegroundWindow(windowHandle_);
    TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, cursorPosition.x, cursorPosition.y, 0, windowHandle_, nullptr);
    DestroyMenu(menu);
}

void TrayApplication::UpdateTrayTooltip() {
    const auto tooltip = presenceService_ ? presenceService_->BuildStatusText() : L"drpc";
    notifyIconData_.uFlags = NIF_TIP;
    CopyTooltip(tooltip, notifyIconData_.szTip);
    Shell_NotifyIconW(NIM_MODIFY, &notifyIconData_);
}

void TrayApplication::OpenLogs() const {
    ShellExecuteW(nullptr, L"open", logPath_.c_str(), nullptr, executableDirectory_.c_str(), SW_SHOWNORMAL);
}

void TrayApplication::HandleCommand(WORD commandId) {
    switch (commandId) {
    case kMenuNextPreset:
        presenceService_->NextPreset();
        break;
    case kMenuPreviousPreset:
        presenceService_->PreviousPreset();
        break;
    case kMenuPauseResume:
        if (presenceService_->IsPaused()) {
            presenceService_->Resume();
        } else {
            presenceService_->Pause();
        }
        break;
    case kMenuOpenLogs:
        OpenLogs();
        break;
    case kMenuQuit:
        DestroyWindow(windowHandle_);
        return;
    default:
        return;
    }

    UpdateTrayTooltip();
}

void TrayApplication::OnTimer(UINT_PTR timerId) {
    if (!presenceService_) {
        return;
    }

    switch (timerId) {
    case kTimerPublish:
        presenceService_->PublishCurrent(false);
        UpdateTrayTooltip();
        break;
    case kTimerPumpCallbacks:
        presenceService_->PumpCallbacks();
        break;
    default:
        break;
    }
}

LRESULT CALLBACK TrayApplication::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* self = static_cast<TrayApplication*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }

    auto* self = reinterpret_cast<TrayApplication*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    return self->HandleMessage(message, wParam, lParam);
}

LRESULT TrayApplication::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_COMMAND:
        HandleCommand(LOWORD(wParam));
        return 0;
    case WM_TIMER:
        OnTimer(static_cast<UINT_PTR>(wParam));
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    if (message == kWindowMessageTrayIcon) {
        switch (LOWORD(lParam)) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU: {
            POINT cursorPosition{};
            GetCursorPos(&cursorPosition);
            ShowContextMenu(cursorPosition);
            return 0;
        }
        case WM_LBUTTONDBLCLK:
            HandleCommand(kMenuNextPreset);
            return 0;
        default:
            return 0;
        }
    }

    return DefWindowProcW(windowHandle_, message, wParam, lParam);
}

}  // namespace drpc
