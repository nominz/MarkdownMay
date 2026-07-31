#include "markdownmay/fileio/file_service.hpp"
#include "markdownmay/fileio/path_utils.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>

int RunFileServiceTests() {
    using namespace markdownmay;
    using namespace markdownmay::fileio;

    const auto root = std::filesystem::temp_directory_path() /
        (L"markdownmay-fileio-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const auto cleanup = [&] { std::filesystem::remove_all(root, ignored); };

    const auto target = root / L"测试文档.md";
    if (AssetsDirectoryFor(target).filename() != L"测试文档.assets") {
        cleanup(); return 20;
    }
    if (!IsPathInside(root / L"a" / L"b", root) ||
        IsPathInside(root.parent_path(), root)) {
        cleanup(); return 21;
    }

    SaveRequest save{target, "# 马冬梅\n\n正文 😀\n",
                     TextEncoding::utf16_le, LineEnding::crlf};
    if (SaveTextFileAtomic(save) != ErrorCode::ok) {
        cleanup(); return 22;
    }
    auto loaded = LoadTextFile(target);
    if (!loaded.is_ok() || loaded.value().encoding != TextEncoding::utf16_le ||
        loaded.value().line_ending != LineEnding::crlf ||
        loaded.value().source != "# 马冬梅\r\n\r\n正文 😀\r\n") {
        cleanup(); return 23;
    }

    const std::string before = loaded.value().source;
    SaveRequest invalid{target, std::string_view("\xc0\xaf", 2),
                        TextEncoding::utf8, LineEnding::crlf};
    if (SaveTextFileAtomic(invalid) == ErrorCode::ok) {
        cleanup(); return 24;
    }
    loaded = LoadTextFile(target);
    if (!loaded.is_ok() || loaded.value().source != before) {
        cleanup(); return 25;
    }
    cleanup();
    return 0;
}
