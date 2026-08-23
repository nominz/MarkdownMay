#include "markdownmay/app/command_line.hpp"
#include "markdownmay/app/startup.hpp"
#include "markdownmay/platform/single_instance.hpp"

#include <windows.h>

#include <array>
#include <filesystem>
#include <string_view>
#include <vector>

using namespace markdownmay;

int main() {
    const std::array<std::wstring_view, 5> arguments{
        L"MarkdownMay.exe", L"--safe-mode", L"中文 空格.md", L"😀.markdown", L"relative.md"};
    const auto parsed = app::ParseCommandLine(arguments);
    if (!parsed.is_ok() || !parsed.value().safe_mode || parsed.value().paths.size() != 3)
        return 1;
    const std::array<std::wstring_view, 3> invalid{
        L"MarkdownMay.exe", L"--register", L"--unregister"};
    if (app::ParseCommandLine(invalid).is_ok()) return 2;
    const std::array<std::wstring_view, 3> mixed_operation{
        L"MarkdownMay.exe", L"--register", L"document.md"};
    if (app::ParseCommandLine(mixed_operation).is_ok()) return 10;
    const std::array<std::wstring_view, 5> placement{
        L"MarkdownMay.exe", L"--wait-for-process", L"1234",
        L"--repair-file-types", L"document.md"};
    const auto placement_parsed = app::ParseCommandLine(placement);
    if (!placement_parsed.is_ok() || !placement_parsed.value().repair_file_types ||
        placement_parsed.value().wait_for_process != std::uint32_t{1234} ||
        placement_parsed.value().paths.size() != 1) return 11;
    const std::array<std::wstring_view, 3> bad_pid{
        L"MarkdownMay.exe", L"--wait-for-process", L"12x"};
    if (app::ParseCommandLine(bad_pid).is_ok()) return 12;
    const std::array<std::wstring_view, 2> repair_without_wait{
        L"MarkdownMay.exe", L"--repair-file-types"};
    if (app::ParseCommandLine(repair_without_wait).is_ok()) return 13;

    const std::array<std::filesystem::path, 2> paths{
        std::filesystem::path(L"C:\\财务 文档\\预算😀.md"),
        std::filesystem::path(L"D:\\资料\\说明.markdown")};
    const auto encoded = platform::EncodeOpenRequest(paths);
    if (!encoded.is_ok()) return 3;
    const auto decoded = platform::DecodeOpenRequest(encoded.value());
    if (!decoded.is_ok() || decoded.value() != std::vector<std::filesystem::path>(paths.begin(), paths.end()))
        return 4;
    auto corrupt = encoded.value();
    corrupt[0] = std::byte{0};
    if (platform::DecodeOpenRequest(corrupt).is_ok()) return 5;

    const auto launch_plan = app::BuildMultiInstanceLaunchPlan(paths);
    if (launch_plan.current_process_paths.size() != 1 ||
        launch_plan.current_process_paths.front() != paths.front() ||
        launch_plan.child_process_paths.size() != 1 ||
        launch_plan.child_process_paths.front() != paths.back()) return 14;
    const std::span<const std::filesystem::path> no_paths;
    if (!app::BuildMultiInstanceLaunchPlan(no_paths).current_process_paths.empty()) return 15;
    if (!app::ShouldReuseWindowForOpen(false, false, true) ||
        app::ShouldReuseWindowForOpen(true, false, true) ||
        app::ShouldReuseWindowForOpen(false, true, false) ||
        app::ShouldReuseWindowForOpen(false, false, false)) return 16;
    return 0;
}
