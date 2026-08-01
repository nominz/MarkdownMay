#include "markdownmay/editor/source_view.hpp"

#include "markdownmay/fileio/file_service.hpp"

#include <Scintilla.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace {
bool PumpUntil(std::uint64_t milliseconds, const auto& predicate) {
    const auto deadline = GetTickCount64() + milliseconds;
    MSG message{};
    while (GetTickCount64() < deadline) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (predicate()) return true;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
    }
    return predicate();
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    using namespace markdownmay;
    constexpr std::string_view initial = "# 标题\r\n\r\n正文\r\n";
    document::DocumentSession session{std::string(initial)};
    HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED,
        0, 0, 700, 500, nullptr, nullptr, instance, nullptr);
    if (!parent) return 1;
    editor::SourceView view(session);
    RECT bounds{0, 0, 700, 500};
    if (view.create(parent, bounds) != ErrorCode::ok || !view.handle()) return 2;
    if (SendMessageW(view.handle(), SCI_GETCODEPAGE, 0, 0) != SC_CP_UTF8 ||
        SendMessageW(view.handle(), SCI_GETSTYLEAT, 0, 0) != 1) return 3;

    const auto body = static_cast<WPARAM>(initial.find("正文"));
    SendMessageW(view.handle(), SCI_SETSEL, body, body);
    SendMessageW(view.handle(), SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>("新"));
    if (!PumpUntil(1000, [&] {
            return session.snapshot().source.find("新正文") != std::string::npos;
        }) || session.snapshot().parsed_revision != session.snapshot().source_revision) return 4;

    const auto directory = std::filesystem::temp_directory_path() /
        L"markdownmay-source-view-test";
    std::filesystem::create_directories(directory);
    const auto target = directory / L"saved.md";
    std::error_code ignored;
    std::filesystem::remove(target, ignored);
    if (view.save(target, fileio::TextEncoding::utf8,
                  fileio::LineEnding::crlf) != ErrorCode::ok || session.is_dirty()) return 5;
    const auto loaded = fileio::LoadTextFile(target);
    if (!loaded.is_ok() || loaded.value().source != session.snapshot().source) return 6;

    std::string invalid = "first\n";
    invalid.push_back(static_cast<char>(0xff));
    SendMessageW(view.handle(), SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(invalid.c_str()));
    if (view.synchronize_now() != ErrorCode::file_encoding_invalid ||
        view.diagnostics().empty() || view.diagnostics()[0].line != 2 ||
        view.go_to_first_error() != ErrorCode::ok) return 7;
    const auto error_at = static_cast<WPARAM>(view.diagnostics()[0].begin);
    if (SendMessageW(view.handle(), SCI_INDICATORVALUEAT, 8, error_at) == 0) return 8;

    const auto valid = session.snapshot().source;
    SendMessageW(view.handle(), SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(valid.c_str()));
    if (view.synchronize_now() != ErrorCode::ok || !view.diagnostics().empty()) return 9;
    std::filesystem::remove_all(directory, ignored);
    DestroyWindow(parent);
    return 0;
}
