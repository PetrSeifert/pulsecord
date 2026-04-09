#include "BrowserActivityProtocol.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <windows.h>

namespace {

std::filesystem::path NativeHostLogPath() {
    const auto* localAppData = std::getenv("LOCALAPPDATA");
    if (localAppData && *localAppData) {
        return std::filesystem::path(localAppData) / "drpc" / "logs" / "native-host.log";
    }

    return std::filesystem::temp_directory_path() / "drpc-native-host.log";
}

void LogLine(const std::string& line) {
    const auto path = NativeHostLogPath();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    std::ofstream stream(path, std::ios::out | std::ios::app);
    if (stream) {
        stream << line << '\n';
    }
}

bool ReadNativeMessage(std::istream& input, std::string& message) {
    std::uint32_t size = 0;
    if (!input.read(reinterpret_cast<char*>(&size), sizeof(size))) {
        LogLine("Native host stdin closed before a message length was read.");
        return false;
    }

    if (size > drpc::kMaxBrowserActivityMessageBytes) {
        LogLine("Rejected native message larger than the supported maximum.");
        std::cerr << "Rejected native message larger than the supported maximum." << std::endl;
        return false;
    }

    message.assign(size, '\0');
    return static_cast<bool>(input.read(message.data(), static_cast<std::streamsize>(size)));
}

bool WriteNativeResponse(std::ostream& output, bool ok, std::string_view error) {
    std::string escapedError;
    escapedError.reserve(error.size());
    for (const auto ch : error) {
        if (ch == '\\' || ch == '"') {
            escapedError.push_back('\\');
        }
        escapedError.push_back(ch);
    }

    const std::string payload = ok
        ? R"({"ok":true})"
        : std::string(R"({"ok":false,"error":")") + escapedError + R"("})";

    const auto size = static_cast<std::uint32_t>(payload.size());
    output.write(reinterpret_cast<const char*>(&size), sizeof(size));
    output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    output.flush();
    return static_cast<bool>(output);
}

bool ForwardToTray(const std::string& payload, std::string& error) {
    HANDLE pipe = CreateFileW(
        drpc::kBrowserActivityPipeName,
        GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);

    if (pipe == INVALID_HANDLE_VALUE) {
        error = "Could not connect to the drpc tray app named pipe.";
        LogLine(error);
        return false;
    }

    DWORD written = 0;
    const auto success = WriteFile(pipe, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr);
    CloseHandle(pipe);

    if (!success || written != payload.size()) {
        error = "Failed to forward the browser snapshot to drpc.";
        LogLine(error);
        return false;
    }

    LogLine("Forwarded browser snapshot to drpc.");
    return true;
}

}  // namespace

int main() {
    std::ios::sync_with_stdio(false);
    LogLine("Native host started.");

    while (true) {
        std::string incoming;
        if (!ReadNativeMessage(std::cin, incoming)) {
            break;
        }

        drpc::BrowserActivitySnapshot snapshot;
        std::string error;
        if (!drpc::ParseBrowserActivityMessage(incoming, snapshot, error)) {
            LogLine("Rejected browser payload: " + error);
            if (!WriteNativeResponse(std::cout, false, error)) {
                return 1;
            }
            continue;
        }

        LogLine("Accepted browser payload from " + snapshot.browser + " for " + snapshot.host + ".");
        const auto serialized = drpc::SerializeBrowserActivityMessage(snapshot);
        if (!ForwardToTray(serialized, error)) {
            if (!WriteNativeResponse(std::cout, false, error)) {
                return 1;
            }
            continue;
        }

        if (!WriteNativeResponse(std::cout, true, {})) {
            return 1;
        }
    }

    LogLine("Native host exiting.");
    return 0;
}
