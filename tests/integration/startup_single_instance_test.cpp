#include "markdownmay/app/command_line.hpp"
#include "markdownmay/platform/single_instance.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <string_view>
#include <thread>
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

    platform::SingleInstance primary;
    platform::SingleInstance secondary;
    if (primary.acquire() != ErrorCode::ok || !primary.is_primary() ||
        secondary.acquire() != ErrorCode::ok || secondary.is_primary()) return 6;
    std::atomic_bool received{};
    std::vector<std::filesystem::path> delivered;
    if (primary.start_receiver([&](auto value) {
            delivered = std::move(value);
            received = true;
        }) != ErrorCode::ok) return 7;
    if (secondary.send(paths) != ErrorCode::ok) return 8;
    for (int attempt = 0; attempt < 100 && !received; ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    primary.stop();
    if (!received || delivered != std::vector<std::filesystem::path>(paths.begin(), paths.end()))
        return 9;
    return 0;
}
