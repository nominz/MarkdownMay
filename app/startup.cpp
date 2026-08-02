#include "markdownmay/app/startup.hpp"

#include "markdownmay/app/application.hpp"
#include "markdownmay/app/command_line.hpp"
#include "markdownmay/platform/single_instance.hpp"

#include <shellapi.h>

#include <filesystem>
#include <string_view>
#include <utility>
#include <vector>

namespace markdownmay::app {

int RunStartup(HINSTANCE instance, int show_command) {
    int argument_count{};
    LPWSTR* raw_arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (!raw_arguments) return 2;
    std::vector<std::wstring_view> arguments;
    arguments.reserve(static_cast<std::size_t>(argument_count));
    for (int index = 0; index < argument_count; ++index)
        arguments.emplace_back(raw_arguments[index]);
    const auto options = ParseCommandLine(arguments);
    LocalFree(raw_arguments);
    if (!options.is_ok()) {
        MessageBoxW(nullptr, L"启动参数无效。请检查文件路径或命令行选项。",
            L"马冬梅", MB_OK | MB_ICONERROR);
        return 2;
    }

    platform::SingleInstance single_instance;
    const auto acquired = single_instance.acquire();
    if (acquired != ErrorCode::ok) {
        MessageBoxW(nullptr,
            L"无法建立单实例保护，本次将使用独立窗口运行。",
            L"马冬梅", MB_OK | MB_ICONWARNING);
    }
    if (acquired == ErrorCode::ok && !single_instance.is_primary()) {
        if (single_instance.send(options.value().paths) == ErrorCode::ok) return 0;
        MessageBoxW(nullptr,
            L"无法联系已经运行的马冬梅，将打开一个独立窗口以免丢失文件请求。",
            L"马冬梅", MB_OK | MB_ICONWARNING);
    }

    Application application(instance);
    application.enqueue_open_paths(options.value().paths);
    if (acquired == ErrorCode::ok && single_instance.is_primary()) {
        (void)single_instance.start_receiver(
            [&application](std::vector<std::filesystem::path> paths) {
                application.enqueue_open_paths(std::move(paths));
            });
    }
    try {
        const auto result = application.run(show_command);
        single_instance.stop();
        return result;
    } catch (...) {
        single_instance.stop();
        throw;
    }
}

}  // namespace markdownmay::app
