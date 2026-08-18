#include "markdownmay/app/startup.hpp"

#include "markdownmay/app/application.hpp"
#include "markdownmay/app/command_line.hpp"

#include <shellapi.h>

#include <filesystem>
#include <string>
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

bool WaitForProcessExit(std::uint32_t process_id) {
    const auto process = OpenProcess(SYNCHRONIZE, FALSE, process_id);
    if (!process) return GetLastError() == ERROR_INVALID_PARAMETER;
    const auto wait = WaitForSingleObject(process, 30000);
    CloseHandle(process);
    return wait == WAIT_OBJECT_0;
}

std::wstring QuoteCommandLineArgument(std::wstring_view value) {
    std::wstring quoted(1, L'"');
    std::size_t backslashes{};
    for (const auto character : value) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'"');
        } else {
            quoted.append(backslashes, L'\\');
            quoted.push_back(character);
        }
        backslashes = 0;
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

bool LaunchDocumentProcess(const std::filesystem::path& executable,
                           const std::filesystem::path& document,
                           bool safe_mode) {
    auto command = QuoteCommandLineArgument(executable.native());
    if (safe_mode) command.append(L" --safe-mode");
    command.push_back(L' ');
    command.append(QuoteCommandLineArgument(document.native()));
    command.push_back(L'\0');

    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    const auto created = CreateProcessW(executable.c_str(), command.data(), nullptr,
        nullptr, FALSE, 0, nullptr, nullptr, &startup, &process);
    if (!created) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
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
    if (options.value().wait_for_process &&
        !WaitForProcessExit(*options.value().wait_for_process)) {
        MessageBoxW(nullptr, L"等待旧位置的马冬梅退出超时，安置后的程序未启动。",
            L"马冬梅", MB_OK | MB_ICONERROR);
        return 7;
    }
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

    if (options.value().repair_file_types) {
        if (executable.empty() ||
            association.register_application(executable) != ErrorCode::ok) {
            MessageBoxW(nullptr, L"马冬梅已在新位置启动，但候选文件关联修复失败。",
                L"马冬梅", MB_OK | MB_ICONWARNING);
        }
    }

    const auto launch_plan = BuildMultiInstanceLaunchPlan(options.value().paths);
    bool child_launch_failed{};
    for (const auto& path : launch_plan.child_process_paths) {
        if (executable.empty() ||
            !LaunchDocumentProcess(executable, path, options.value().safe_mode))
            child_launch_failed = true;
    }
    if (child_launch_failed) {
        MessageBoxW(nullptr,
            L"部分文档无法在独立窗口中启动；其他文档仍会继续打开。",
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
    application.enqueue_open_paths(launch_plan.current_process_paths);
    return application.run(show_command);
}

}  // namespace markdownmay::app
