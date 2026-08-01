#include "markdownmay/editor/view_mode_controller.hpp"

#include <Scintilla.h>
#include <commdlg.h>
#include <richedit.h>

#include <string>

namespace {
std::wstring ReadWide(HWND window) {
    const auto length = GetWindowTextLengthW(window);
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(window, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(length));
    return value;
}

std::string ReadSource(HWND editor) {
    const auto length = static_cast<std::size_t>(SendMessageW(editor, SCI_GETLENGTH, 0, 0));
    std::string value(length + 1, '\0');
    SendMessageW(editor, SCI_GETTEXT, length + 1, reinterpret_cast<LPARAM>(value.data()));
    value.resize(length);
    return value;
}

bool HasVisibleStyle(HWND window) {
    return (GetWindowLongPtrW(window, GWL_STYLE) & WS_VISIBLE) != 0;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    using namespace markdownmay;
    const std::string original = "# 标题\n\n正文\n\n第二段\n";
    document::DocumentSession session(original);
    HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED,
        0, 0, 900, 600, nullptr, nullptr, instance, nullptr);
    if (!parent) return 1;
    editor::ViewModeController modes(session);
    RECT bounds{0, 0, 900, 600};
    if (modes.create(parent, bounds) != ErrorCode::ok ||
        modes.mode() != editor::ViewMode::render ||
        !HasVisibleStyle(modes.render_view().handle())) return 2;

    FINDTEXTEXW find_body{{0, -1}, const_cast<wchar_t*>(L"正文"), {}};
    if (SendMessageW(modes.render_view().handle(), EM_FINDTEXTEXW, FR_DOWN,
        reinterpret_cast<LPARAM>(&find_body)) < 0) return 3;
    SendMessageW(modes.render_view().handle(), EM_SETSEL,
        find_body.chrgText.cpMin, find_body.chrgText.cpMin);
    SendMessageW(modes.render_view().handle(), EM_REPLACESEL, TRUE,
        reinterpret_cast<LPARAM>(L"渲"));
    if (modes.render_view().synchronize_change() != ErrorCode::ok ||
        session.snapshot().source.find("渲正文") == std::string::npos ||
        !modes.can_undo()) return 4;

    if (modes.switch_to(editor::ViewMode::source) != ErrorCode::ok ||
        modes.mode() != editor::ViewMode::source ||
        ReadSource(modes.source_view().handle()) != session.snapshot().source ||
        !HasVisibleStyle(modes.split_view().handle()) ||
        HasVisibleStyle(modes.split_view().render_view().handle())) return 5;
    auto source = session.snapshot().source;
    const auto source_insert = static_cast<WPARAM>(source.find("正文"));
    SendMessageW(modes.source_view().handle(), SCI_SETSEL, source_insert, source_insert);
    SendMessageW(modes.source_view().handle(), SCI_REPLACESEL, 0,
        reinterpret_cast<LPARAM>("源"));
    if (modes.source_view().synchronize_now() != ErrorCode::ok ||
        session.snapshot().source.find("渲源正文") == std::string::npos) return 6;

    if (modes.switch_to(editor::ViewMode::split) != ErrorCode::ok ||
        modes.mode() != editor::ViewMode::split ||
        !HasVisibleStyle(modes.split_view().render_view().handle()) ||
        ReadWide(modes.split_view().render_view().handle()).find(L"渲源正文") ==
            std::wstring::npos) return 7;
    if (modes.undo() != ErrorCode::ok ||
        session.snapshot().source.find("渲正文") == std::string::npos ||
        session.snapshot().source.find("源正文") != std::string::npos ||
        !modes.can_redo()) return 8;
    if (modes.switch_to(editor::ViewMode::render) != ErrorCode::ok ||
        ReadWide(modes.render_view().handle()).find(L"渲正文") == std::wstring::npos) return 9;
    if (modes.undo() != ErrorCode::ok || session.snapshot().source != original) return 10;
    if (modes.redo() != ErrorCode::ok ||
        session.snapshot().source.find("渲正文") == std::string::npos) return 11;
    if (modes.switch_to(editor::ViewMode::source) != ErrorCode::ok ||
        modes.redo() != ErrorCode::ok ||
        session.snapshot().source.find("渲源正文") == std::string::npos) return 12;

    const auto location = session.snapshot().source.find("第二段");
    if (modes.source_view().select_source_range({location, location}) != ErrorCode::ok ||
        modes.switch_to(editor::ViewMode::render) != ErrorCode::ok) return 13;
    const auto mapped = modes.render_view().source_selection();
    if (!mapped.is_ok() || mapped.value().caret != location) return 14;

    if (modes.switch_to(editor::ViewMode::source) != ErrorCode::ok) return 15;
    std::string invalid = "broken\n";
    invalid.push_back(static_cast<char>(0xff));
    SendMessageW(modes.source_view().handle(), SCI_SETTEXT, 0,
        reinterpret_cast<LPARAM>(invalid.c_str()));
    if (modes.switch_to(editor::ViewMode::render) !=
            ErrorCode::editor_cannot_enter_render_mode ||
        modes.mode() != editor::ViewMode::source) return 16;
    if (modes.switch_to(editor::ViewMode::split) != ErrorCode::ok ||
        ReadWide(modes.split_view().render_view().handle()).find(
            L"当前源码无法渲染") == std::wstring::npos) return 17;
    const auto valid = session.snapshot().source;
    SendMessageW(modes.source_view().handle(), SCI_SETTEXT, 0,
        reinterpret_cast<LPARAM>(valid.c_str()));
    if (modes.source_view().synchronize_now() != ErrorCode::ok ||
        modes.switch_to(editor::ViewMode::render) != ErrorCode::ok ||
        session.snapshot().parsed_revision != session.snapshot().source_revision) return 18;
    DestroyWindow(parent);
    return 0;
}
