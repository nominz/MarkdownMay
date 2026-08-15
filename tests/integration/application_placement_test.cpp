#include "markdownmay/platform/application_placement.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace markdownmay;

namespace {

std::string Read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void Write(const std::filesystem::path& path, std::string_view value) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(value.data(), static_cast<std::streamsize>(value.size()));
}

}

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        (L"MarkdownMay-placement-test-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root / L"source");
    const auto source = root / L"source" / L"portable.exe";
    Write(source, std::string_view("first-version\0binary", 20));

    platform::ApplicationPlacementService service;
    const auto first = service.inspect(source, root / L"home");
    if (!first.is_ok() || first.value().same_path || first.value().target_exists)
        return 1;
    if (service.place(first.value(), false) != ErrorCode::ok) return 2;
    const auto target = root / L"home" / L"MarkdownMay.exe";
    if (Read(source) != Read(target)) return 3;

    const auto identical = service.inspect(source, root / L"home");
    if (!identical.is_ok() || !identical.value().target_exists ||
        !identical.value().same_content ||
        service.place(identical.value(), true) != ErrorCode::ok) return 12;

    const auto same = service.inspect(target, root / L"home");
    if (!same.is_ok() || !same.value().same_path || !same.value().same_content)
        return 4;

    Write(source, "second-version");
    const auto replacement = service.inspect(source, root / L"home");
    if (!replacement.is_ok() || !replacement.value().target_exists ||
        replacement.value().same_content) return 5;
    if (service.place(replacement.value(), false) !=
        ErrorCode::platform_placement_target_changed) return 6;
    if (service.place(replacement.value(), true) != ErrorCode::ok) return 7;
    if (Read(target) != "second-version") return 8;

    const auto stale = service.inspect(source, root / L"home");
    if (!stale.is_ok()) return 9;
    Write(target, "external-change");
    if (service.place(stale.value(), true) !=
        ErrorCode::platform_placement_target_changed) return 10;

    for (const auto& entry : std::filesystem::directory_iterator(root / L"home"))
        if (entry.path().filename().wstring().starts_with(L".MarkdownMay.place."))
            return 11;
    std::filesystem::remove_all(root, ignored);
    return 0;
}
