#include "TrayApplication.h"

#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

#include <windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    try {
        drpc::TrayApplication app(instance);
        return app.Run();
    } catch (const std::exception& ex) {
        const auto crashLogPath = std::filesystem::temp_directory_path() / "drpc-startup-crash.log";
        std::ofstream stream(crashLogPath, std::ios::out | std::ios::app);
        if (stream) {
            stream << ex.what() << '\n';
        }

        const auto message = std::string("drpc failed to start:\n") + ex.what() +
            "\n\nCrash log: " + crashLogPath.string();
        MessageBoxA(nullptr, message.c_str(), "drpc startup failure", MB_OK | MB_ICONERROR);
        return 1;
    } catch (...) {
        MessageBoxA(nullptr, "drpc failed to start with an unknown exception.", "drpc startup failure", MB_OK | MB_ICONERROR);
        return 1;
    }
}
