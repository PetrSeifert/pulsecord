#include "TrayApplication.h"

#include <windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    drpc::TrayApplication app(instance);
    return app.Run();
}
