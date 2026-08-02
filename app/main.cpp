#include "markdownmay/app/startup.hpp"

#include <objbase.h>
#include <windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) {
        MessageBoxW(nullptr, L"无法初始化 Windows 组件。", L"马冬梅",
            MB_OK | MB_ICONERROR);
        return 1;
    }
    int result{};
    try {
        result = markdownmay::app::RunStartup(instance, show_command);
    } catch (...) {
        MessageBoxW(nullptr, L"程序遇到无法恢复的错误。", L"马冬梅",
            MB_OK | MB_ICONERROR);
        result = 3;
    }
    CoUninitialize();
    return result;
}
