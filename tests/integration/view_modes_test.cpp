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

    // DEF-048: a repeated command must repair any temporary mismatch between
    // the logical mode (used by toolbar state) and the actually visible child.
    ShowWindow(modes.split_view().handle(), SW_HIDE);
    ShowWindow(modes.render_view().handle(), SW_SHOW);
    if (modes.switch_to(editor::ViewMode::source) != ErrorCode::ok ||
        !HasVisibleStyle(modes.split_view().handle()) ||
        HasVisibleStyle(modes.render_view().handle()) ||
        HasVisibleStyle(modes.split_view().render_view().handle())) return 39;
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
    const auto split_before_add = session.snapshot().source;
    const auto second_offset = static_cast<std::uint64_t>(split_before_add.find("第二段"));
    const auto split_context = modes.split_view().render_view().block_context_at_source(
        second_offset);
    if (!split_context || modes.split_view().execute_block_menu(
        editor::BlockMenuCommand::add_below, *split_context) != ErrorCode::ok ||
        ReadSource(modes.source_view().handle()) != session.snapshot().source ||
        ReadWide(modes.split_view().render_view().handle()).find(L"第二段") ==
            std::wstring::npos ||
        SendMessageW(modes.source_view().handle(), SCI_GETCURRENTPOS, 0, 0) !=
            static_cast<LRESULT>(split_context->source_range.end + 1)) return 36;
    if (modes.undo() != ErrorCode::ok || session.snapshot().source != split_before_add ||
        ReadSource(modes.source_view().handle()) != split_before_add) return 37;
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

    if (modes.switch_to(editor::ViewMode::split) != ErrorCode::ok) return 24;
    const auto split_insert = static_cast<WPARAM>(session.snapshot().source.find("第二段"));
    SendMessageW(modes.source_view().handle(), SCI_SETSEL, split_insert, split_insert);
    SendMessageW(modes.source_view().handle(), SCI_REPLACESEL, 0,
        reinterpret_cast<LPARAM>("\n"));
    const auto switch_started = GetTickCount64();
    if (modes.switch_to(editor::ViewMode::render) != ErrorCode::ok) return 25;
    if (GetTickCount64() - switch_started > 2000) return 26;
    if (session.snapshot().source.find("\n第二段") == std::string::npos ||
        session.snapshot().parsed_revision != session.snapshot().source_revision) return 27;
    DestroyWindow(parent);

    std::string long_source;
    for (int line = 0; line < 1500; ++line)
        long_source += "line " + std::to_string(line) + "\n\n";
    document::DocumentSession scroll_session(long_source);
    HWND scroll_parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED,
        0, 0, 900, 300, nullptr, nullptr, instance, nullptr);
    editor::ViewModeController scroll_modes(scroll_session);
    RECT scroll_bounds{0, 0, 900, 300};
    if (!scroll_parent || scroll_modes.create(scroll_parent, scroll_bounds) != ErrorCode::ok)
        return 19;
    scroll_modes.render_view().scroll_to_fraction(80, 200);
    const auto before_switch = scroll_modes.render_view().scroll_fraction();
    if (scroll_modes.switch_to(editor::ViewMode::source) != ErrorCode::ok)
        return 20;
    const auto in_source = scroll_modes.source_view().scroll_fraction();
    const auto source_delta = static_cast<std::int64_t>(
        in_source.first * before_switch.second) - static_cast<std::int64_t>(
        before_switch.first * in_source.second);
    if (std::abs(source_delta) > static_cast<std::int64_t>(before_switch.second * 3))
        return 21;
    if (scroll_modes.switch_to(editor::ViewMode::render) != ErrorCode::ok)
        return 22;
    const auto after_switch = scroll_modes.render_view().scroll_fraction();
    if (after_switch.first + 3 < before_switch.first ||
        after_switch.first > before_switch.first + 3) return 23;
    ShowWindow(scroll_parent, SW_SHOWNOACTIVATE);
    UpdateWindow(scroll_parent);
    const auto navigation_target = static_cast<std::uint64_t>(long_source.find("line 1200"));
    if (scroll_modes.navigate_to_source(navigation_target) != ErrorCode::ok) return 33;
    const auto navigated = scroll_modes.render_view().source_selection();
    CHARRANGE navigated_control{};
    SendMessageW(scroll_modes.render_view().handle(), EM_EXGETSEL, 0,
        reinterpret_cast<LPARAM>(&navigated_control));
    const auto navigated_line = SendMessageW(scroll_modes.render_view().handle(),
        EM_LINEFROMCHAR, static_cast<WPARAM>(navigated_control.cpMin), 0);
    const auto navigated_first = SendMessageW(scroll_modes.render_view().handle(),
        EM_GETFIRSTVISIBLELINE, 0, 0);
    if (!navigated.is_ok() || navigated.value().caret != navigation_target ||
        navigated_line != navigated_first) return 34;
    if (scroll_modes.switch_to(editor::ViewMode::split) != ErrorCode::ok) return 28;
    const auto long_insert = static_cast<WPARAM>(long_source.size() / 2);
    SendMessageW(scroll_modes.source_view().handle(), SCI_SETSEL, long_insert, long_insert);
    SendMessageW(scroll_modes.source_view().handle(), SCI_REPLACESEL, 0,
        reinterpret_cast<LPARAM>("\n"));
    const auto long_switch_started = GetTickCount64();
    if (scroll_modes.switch_to(editor::ViewMode::render) != ErrorCode::ok) return 29;
    if (GetTickCount64() - long_switch_started > 2000) return 30;
    if (scroll_session.snapshot().source.size() != long_source.size() + 1 ||
        scroll_session.snapshot().parsed_revision !=
            scroll_session.snapshot().source_revision) return 31;
    DestroyWindow(scroll_parent);

    document::DocumentSession exact_session("正文\n");
    HWND exact_parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED,
        0, 0, 900, 600, nullptr, nullptr, instance, nullptr);
    editor::ViewModeController exact_modes(exact_session);
    if (!exact_parent || exact_modes.create(exact_parent, bounds) != ErrorCode::ok)
        return 32;
    SendMessageW(exact_modes.render_view().handle(), EM_SETSEL, 2, 2);
    SendMessageW(exact_modes.render_view().handle(), EM_REPLACESEL, TRUE,
        reinterpret_cast<LPARAM>(L"渲染输入"));
    if (exact_modes.render_view().synchronize_change() != ErrorCode::ok ||
        exact_modes.switch_to(editor::ViewMode::split) != ErrorCode::ok) return 33;
    SendMessageW(exact_modes.source_view().handle(), SCI_SETSEL, 0, 0);
    SendMessageW(exact_modes.source_view().handle(), SCI_REPLACESEL, 0,
        reinterpret_cast<LPARAM>("# 标题1\n\n"));
    if (exact_modes.source_view().synchronize_now() != ErrorCode::ok ||
        ReadWide(exact_modes.split_view().render_view().handle()).find(L"标题1") ==
            std::wstring::npos) return 34;
    const auto exact_started = GetTickCount64();
    if (exact_modes.switch_to(editor::ViewMode::render) != ErrorCode::ok ||
        GetTickCount64() - exact_started > 1000 ||
        ReadWide(exact_modes.render_view().handle()).find(L"标题1") ==
            std::wstring::npos) return 35;
    CHARRANGE context_range{0, 3};
    SendMessageW(exact_modes.render_view().handle(), EM_EXSETSEL, 0,
        reinterpret_cast<LPARAM>(&context_range));
    POINT context_point{};
    SendMessageW(exact_modes.render_view().handle(), EM_POSFROMCHAR,
        reinterpret_cast<WPARAM>(&context_point), 1);
    exact_modes.render_view().remember_context_selection_at(context_point);
    SendMessageW(exact_modes.render_view().handle(), EM_SETSEL, 1, 1);
    exact_modes.render_view().restore_context_selection();
    SendMessageW(exact_modes.render_view().handle(), EM_EXGETSEL, 0,
        reinterpret_cast<LPARAM>(&context_range));
    if (context_range.cpMin != 0 || context_range.cpMax != 3) return 43;
    if (exact_modes.change_document_kind(document::DocumentKind::plain_text) != ErrorCode::ok ||
        exact_modes.mode() != editor::ViewMode::source ||
        HasVisibleStyle(exact_modes.split_view().render_view().handle()) ||
        exact_modes.split_view().render_view().block_context_at_source(0)) return 38;
    SendMessageW(exact_modes.source_view().handle(), SCI_SETSEL, 0, 2);
    if (exact_modes.erase_selection() != ErrorCode::ok) return 40;
    const auto deleted_sync = exact_modes.source_view().synchronize_now();
    if (deleted_sync != ErrorCode::ok &&
        deleted_sync != ErrorCode::markdown_parse_failed) return 41;
    if (exact_session.snapshot().source.starts_with("# ")) return 42;
    DestroyWindow(exact_parent);

    std::string undo_tail_source;
    for (int index = 0; index < 180; ++index)
        undo_tail_source += "前文 " + std::to_string(index) + "。\n\n";
    const auto undo_tail_original = undo_tail_source +
        "## 10. 冻结结论\n\n2026-08-12 用户明确同意冻结 V04。\n";
    document::DocumentSession undo_tail_session(undo_tail_original);
    HWND undo_tail_parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED,
        0, 0, 900, 420, nullptr, nullptr, instance, nullptr);
    editor::ViewModeController undo_tail_modes(undo_tail_session);
    if (!undo_tail_parent || undo_tail_modes.create(undo_tail_parent, {0, 0, 900, 420}) !=
            ErrorCode::ok) return 44;
    const auto tail_begin = static_cast<std::uint64_t>(
        undo_tail_original.find("2026-08-12"));
    const auto tail_end = static_cast<std::uint64_t>(undo_tail_original.find('\n', tail_begin));
    if (undo_tail_modes.render_view().select_source_range({tail_begin, tail_end}) != ErrorCode::ok)
        return 45;
    undo_tail_modes.render_view().scroll_to_fraction(179, 180);
    if (undo_tail_modes.execute(editor::EditorCommand::quote) != ErrorCode::ok ||
        undo_tail_session.snapshot().source.find("> 2026-08-12") == std::string::npos)
        return 46;
    const auto before_tail_undo_scroll = undo_tail_modes.render_view().scroll_fraction();
    if (undo_tail_modes.undo() != ErrorCode::ok ||
        undo_tail_session.snapshot().source != undo_tail_original) return 47;
    const auto after_tail_undo_scroll = undo_tail_modes.render_view().scroll_fraction();
    if (after_tail_undo_scroll.first + 5 < before_tail_undo_scroll.first) return 48;
    const auto restored_tail = undo_tail_modes.render_view().source_selection();
    if (!restored_tail.is_ok() || restored_tail.value().caret < tail_begin - 2) return 49;
    DestroyWindow(undo_tail_parent);
    return 0;
}
