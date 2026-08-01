#include "markdownmay/editor/view_mode_controller.hpp"
#include "markdownmay/fileio/file_service.hpp"

#include <Scintilla.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
struct TemporaryDirectory final {
    std::filesystem::path path;
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    using namespace markdownmay;
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory temporary{std::filesystem::temp_directory_path() /
        ("markdownmay-save-reopen-" + std::to_string(nonce))};
    std::filesystem::create_directories(temporary.path);
    const auto target = temporary.path / L"三模式保存.md";
    const std::string original = "# 原文\n\n保留内容\n";
    if (fileio::SaveTextFileAtomic(
            {target, original, fileio::TextEncoding::utf8,
             fileio::LineEnding::lf}) != ErrorCode::ok) return 1;

    document::DocumentSession session(original);
    HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED,
        0, 0, 900, 600, nullptr, nullptr, instance, nullptr);
    if (!parent) return 2;
    editor::ViewModeController modes(session);
    RECT bounds{0, 0, 900, 600};
    if (modes.create(parent, bounds) != ErrorCode::ok || session.is_dirty()) return 3;

    if (modes.switch_to(editor::ViewMode::source) != ErrorCode::ok) return 4;
    const std::string source_edit = "# 源码保存\n\n中文与 emoji 😀\n";
    SendMessageW(modes.source_view().handle(), SCI_SETTEXT, 0,
        reinterpret_cast<LPARAM>(source_edit.c_str()));
    if (modes.save(target, fileio::TextEncoding::utf8,
            fileio::LineEnding::lf) != ErrorCode::ok || session.is_dirty()) return 5;
    auto reopened = fileio::LoadTextFile(target);
    if (!reopened.is_ok() || reopened.value().source != source_edit ||
        reopened.value().encoding != fileio::TextEncoding::utf8 ||
        reopened.value().line_ending != fileio::LineEnding::lf) return 6;
    document::DocumentSession reopened_session(reopened.value().source);
    if (!reopened_session.can_export() || reopened_session.snapshot().source !=
        session.snapshot().source) return 7;

    const std::string pending = "# 失败不得覆盖\n\n尚未保存\n";
    SendMessageW(modes.source_view().handle(), SCI_SETTEXT, 0,
        reinterpret_cast<LPARAM>(pending.c_str()));
    const auto saved_revision = session.snapshot().saved_revision;
    const auto failed = modes.save(target, fileio::TextEncoding::utf8,
        fileio::LineEnding::lf,
        [](const std::filesystem::path& temporary_path,
           const std::filesystem::path& target_path) {
            return std::filesystem::exists(temporary_path) &&
                   std::filesystem::exists(target_path)
                ? ErrorCode::file_write_failed
                : ErrorCode::document_invalid_state;
        });
    if (failed != ErrorCode::file_write_failed || !session.is_dirty() ||
        session.snapshot().saved_revision != saved_revision) return 8;
    reopened = fileio::LoadTextFile(target);
    if (!reopened.is_ok() || reopened.value().source != source_edit) return 9;

    std::string invalid = "invalid";
    invalid.push_back(static_cast<char>(0xff));
    SendMessageW(modes.source_view().handle(), SCI_SETTEXT, 0,
        reinterpret_cast<LPARAM>(invalid.c_str()));
    if (modes.save(target, fileio::TextEncoding::utf8,
            fileio::LineEnding::lf) != ErrorCode::file_encoding_invalid) return 10;
    reopened = fileio::LoadTextFile(target);
    if (!reopened.is_ok() || reopened.value().source != source_edit) return 11;

    SendMessageW(modes.source_view().handle(), SCI_SETTEXT, 0,
        reinterpret_cast<LPARAM>(pending.c_str()));
    if (modes.switch_to(editor::ViewMode::render) != ErrorCode::ok) return 12;
    if (modes.save(target, fileio::TextEncoding::utf16_le,
            fileio::LineEnding::crlf) != ErrorCode::ok || session.is_dirty()) return 13;
    reopened = fileio::LoadTextFile(target);
    if (!reopened.is_ok() || reopened.value().encoding !=
            fileio::TextEncoding::utf16_le ||
        reopened.value().line_ending != fileio::LineEnding::crlf ||
        reopened.value().source != "# 失败不得覆盖\r\n\r\n尚未保存\r\n") return 14;

    DestroyWindow(parent);
    return 0;
}
