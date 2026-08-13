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
namespace {
std::filesystem::path ExecutablePath() {
    std::vector<wchar_t> buffer(32768);
    const auto length = GetModuleFileNameW(nullptr, buffer.data(),
        static_cast<DWORD>(buffer.size()));
    return length && length < buffer.size()
        ? std::filesystem::path(buffer.data()) : std::filesystem::path{};
}
}

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

    const auto executable = ExecutablePath();
    platform::FileAssociationRegistry association;
    if (options.value().register_file_types) {
        if (executable.empty()) return 4;
        const auto registered = association.register_application(executable);
        if (registered != ErrorCode::ok) {
            MessageBoxW(nullptr, L"无法注册 Markdown 和 TXT 候选程序。",
                L"马冬梅", MB_OK | MB_ICONERROR);
            return 4;
        }
        MessageBoxW(nullptr,
            L"Markdown 和 TXT 候选程序注册完成；当前默认应用没有被更改。\n"
            L"接下来请在 Windows 设置中亲自选择默认应用。",
            L"马冬梅", MB_OK | MB_ICONINFORMATION);
        return platform::OpenDefaultAppsSettings() == ErrorCode::ok ? 0 : 5;
    }
    if (options.value().unregister_file_types) {
        const auto removed = association.unregister_application(executable);
        MessageBoxW(nullptr, removed == ErrorCode::ok
            ? L"文件关联注册已撤销。" : L"无法撤销文件关联注册。",
            L"马冬梅", MB_OK | (removed == ErrorCode::ok ? MB_ICONINFORMATION : MB_ICONERROR));
        return removed == ErrorCode::ok ? 0 : 6;
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

    if (!executable.empty() &&
        association.state(executable) == platform::AssociationState::needs_repair &&
        !association.repair_prompt_ignored(executable)) {
        const auto repair = MessageBoxW(nullptr,
            L"检测到马冬梅的位置已经变化，原文件关联路径已失效。是否修复？",
            L"修复文件关联", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1);
        if (repair == IDYES) {
            if (association.register_application(executable) != ErrorCode::ok)
                MessageBoxW(nullptr, L"文件关联修复失败，程序仍可正常使用。",
                    L"马冬梅", MB_OK | MB_ICONWARNING);
        } else {
            (void)association.ignore_repair_prompt(executable);
        }
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
