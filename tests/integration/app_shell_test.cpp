#include "markdownmay/app/command_dispatcher.hpp"
#include "markdownmay/ui/main_window.hpp"

#include <objbase.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windows.h>
#include <richedit.h>
#include <commdlg.h>

#include <string>
#include <chrono>
#include <filesystem>
#include <cstring>
#include <cstddef>

namespace {
struct TemporaryDirectory final {
    std::filesystem::path path;
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};
bool ContainsKind(const markdownmay::document::Node& node,
                  markdownmay::document::NodeKind kind) {
    if (node.kind == kind) return true;
    for (const auto& child : node.children)
        if (ContainsKind(*child, kind)) return true;
    return false;
}
HWND FindDescendantById(HWND root, int identifier) {
    struct Context { int id; HWND result; } context{identifier, nullptr};
    EnumChildWindows(root, [](HWND window, LPARAM value) -> BOOL {
        auto& context = *reinterpret_cast<Context*>(value);
        if (GetDlgCtrlID(window) == context.id) { context.result = window; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&context));
    return context.result;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    using namespace markdownmay;
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return 1;
    document::DocumentSession session("");
    ui::MainWindow window(session);
    bool exit_requested{};
    bool export_requested{};
    bool insert_requested{};
    bool split_requested{};
    CHARRANGE selection_at_toolbar_dispatch{-1, -1};
    app::CommandDispatcher dispatcher(window.document_window(),
        [&exit_requested] { exit_requested = true; }, {
            [&session] { return !session.is_dirty(); },
            [&window] { return window.document_window().new_document(); },
            [] { return ErrorCode::ok; },
            [&window] { return window.document_window().save_document(); },
            [] { return ErrorCode::ok; },
            [] { return ErrorCode::ok; },
            [] { return ErrorCode::ok; },
            [&export_requested] { export_requested = true; return ErrorCode::ok; },
        }, {
            [] { return true; },
            [] { return true; },
            [&insert_requested] { insert_requested = true; return ErrorCode::ok; },
            [&split_requested] { split_requested = true; return ErrorCode::ok; },
        });
    window.set_command_callbacks(
        [&dispatcher](app::CommandId command) { return dispatcher.query(command); },
        [&dispatcher, &window, &selection_at_toolbar_dispatch](app::CommandId command) {
            if (command == app::CommandId::format_code_block) {
                SendMessageW(window.document_window().modes().render_view().handle(),
                    EM_EXGETSEL, 0,
                    reinterpret_cast<LPARAM>(&selection_at_toolbar_dispatch));
            }
            static_cast<void>(dispatcher.execute(command));
        });
    std::filesystem::path dropped_path;
    window.set_drop_callback([&dropped_path](const std::filesystem::path& path) {
        dropped_path = path;
    });
    // Persisted appearance is applied before Application creates any HWND.
    window.set_theme_preference(ui::ThemePreference::dark);
    window.set_theme_preference(ui::ThemePreference::follow_system);
    if (window.create(instance, SW_HIDE) != ErrorCode::ok || !window.handle()) {
        CoUninitialize();
        return 2;
    }
    const auto length = GetWindowTextLengthW(window.handle());
    std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(window.handle(), title.data(), length + 1);
    title.resize(static_cast<std::size_t>(length));
    if (title != L"无标题 - 马冬梅") {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 3;
    }
    if (window.document_window().modes().mode() != editor::ViewMode::render ||
        !window.document_window().modes().render_view().handle()) {
        DestroyWindow(window.handle()); CoUninitialize(); return 39;
    }
    if (!window.toolbar() || !window.toolbar()->handle()) {
        DestroyWindow(window.handle()); CoUninitialize(); return 40;
    }
    constexpr std::array toolbar_commands{
        app::CommandId::file_open, app::CommandId::file_save, app::CommandId::file_print,
        app::CommandId::format_bold, app::CommandId::format_italic,
        app::CommandId::format_inline_code, app::CommandId::format_code_block,
        app::CommandId::format_quote, app::CommandId::format_unordered_list,
        app::CommandId::format_ordered_list, app::CommandId::format_task_list,
        app::CommandId::insert_thematic_break, app::CommandId::format_clear,
        app::CommandId::insert_table, app::CommandId::view_render,
        app::CommandId::view_source, app::CommandId::view_split,
        app::CommandId::view_outline, app::CommandId::view_style_yuan_lang,
        app::CommandId::edit_find, app::CommandId::edit_replace,
        app::CommandId::tools_settings};
    for (const auto command : toolbar_commands) {
        TBBUTTONINFO info{sizeof(info), TBIF_STYLE};
        if (SendMessageW(window.toolbar()->handle(), TB_GETBUTTONINFO,
                static_cast<WPARAM>(command), reinterpret_cast<LPARAM>(&info)) < 0)
            return 80;
    }
    TBBUTTONINFO outline_style{sizeof(outline_style), TBIF_STYLE};
    TBBUTTONINFO render_style{sizeof(render_style), TBIF_STYLE};
    if (SendMessageW(window.toolbar()->handle(), TB_GETBUTTONINFO,
            static_cast<WPARAM>(app::CommandId::view_outline),
            reinterpret_cast<LPARAM>(&outline_style)) < 0 ||
        SendMessageW(window.toolbar()->handle(), TB_GETBUTTONINFO,
            static_cast<WPARAM>(app::CommandId::view_render),
            reinterpret_cast<LPARAM>(&render_style)) < 0 ||
        (outline_style.fsStyle & BTNS_CHECK) == 0 ||
        (render_style.fsStyle & BTNS_CHECKGROUP) != BTNS_CHECKGROUP) return 81;
    if (!dispatcher.query(app::CommandId::file_export).enabled ||
        dispatcher.execute(app::CommandId::file_export) != ErrorCode::ok ||
        !export_requested) { DestroyWindow(window.handle()); CoUninitialize(); return 58; }
    if (!window.status_bar().handle()) {
        DestroyWindow(window.handle()); CoUninitialize(); return 41;
    }
    if (!window.document_window().outline_handle() ||
        TreeView_GetCount(window.document_window().outline_handle()) != 1) {
        DestroyWindow(window.handle()); CoUninitialize(); return 52;
    }
    if (window.document_window().outline_visible()) return 56;
    std::string outline_source = "# 一级\n\n";
    for (int line = 0; line < 40; ++line)
        outline_source += "正文行 " + std::to_string(line) + "\n\n";
    const auto third_heading_offset = outline_source.size();
    outline_source += "### 三级\n\n";
    for (int line = 0; line < 40; ++line)
        outline_source += "后续正文 " + std::to_string(line) + "\n\n";
    if (window.document_window().modes().reload(outline_source) != ErrorCode::ok ||
        TreeView_GetCount(window.document_window().outline_handle()) != 2) {
        DestroyWindow(window.handle()); CoUninitialize(); return 53;
    }
    const auto outline = window.document_window().outline_handle();
    const auto root_item = TreeView_GetRoot(outline);
    const auto child_item = TreeView_GetChild(outline, root_item);
    std::array<wchar_t, 32> outline_label{};
    TVITEMW outline_item{};
    outline_item.mask = TVIF_TEXT;
    outline_item.hItem = child_item;
    outline_item.pszText = outline_label.data();
    outline_item.cchTextMax = static_cast<int>(outline_label.size());
    if (!root_item || !child_item || !TreeView_GetItem(outline, &outline_item) ||
        std::wstring_view(outline_label.data()) != L"三级") {
        DestroyWindow(window.handle()); CoUninitialize(); return 54;
    }
    outline_item.mask = TVIF_PARAM;
    outline_item.hItem = child_item;
    if (!TreeView_GetItem(outline, &outline_item) ||
        static_cast<std::size_t>(outline_item.lParam) < third_heading_offset ||
        static_cast<std::size_t>(outline_item.lParam) > third_heading_offset + 4) {
        DestroyWindow(window.handle()); CoUninitialize(); return 64;
    }
    TreeView_Expand(outline, root_item, TVE_COLLAPSE);
    if (TreeView_GetNextVisible(outline, root_item) != nullptr) {
        DestroyWindow(window.handle()); CoUninitialize(); return 59;
    }
    TreeView_Expand(outline, root_item, TVE_EXPAND);
    if (!TreeView_SelectItem(outline, child_item) ||
        TreeView_GetSelection(outline) != child_item) {
        DestroyWindow(window.handle()); CoUninitialize(); return 65;
    }
    NMTREEVIEWW selection_changed{};
    selection_changed.hdr.hwndFrom = outline;
    selection_changed.hdr.code = TVN_SELCHANGEDW;
    selection_changed.itemNew.hItem = child_item;
    if (!window.document_window().handle_notify(selection_changed.hdr)) {
        DestroyWindow(window.handle()); CoUninitialize(); return 66;
    }
    if (window.document_window().modes().navigate_to_source(
            static_cast<std::uint64_t>(outline_item.lParam)) != ErrorCode::ok) {
        DestroyWindow(window.handle()); CoUninitialize(); return 67;
    }
    if (window.document_window().modes().mode() != editor::ViewMode::render) {
        DestroyWindow(window.handle()); CoUninitialize(); return 71;
    }
    if (window.document_window().modes().render_view().select_source_range(
            {third_heading_offset, third_heading_offset}) != ErrorCode::ok) {
        DestroyWindow(window.handle()); CoUninitialize(); return 69;
    }
    const auto outline_selection = window.document_window().modes().render_view().source_selection();
    if (!outline_selection.is_ok()) { DestroyWindow(window.handle()); CoUninitialize(); return 60; }
    if (outline_selection.value().caret < third_heading_offset ||
        outline_selection.value().caret > third_heading_offset + 4) {
        DestroyWindow(window.handle()); CoUninitialize(); return 61;
    }
    if (session.is_dirty()) { DestroyWindow(window.handle()); CoUninitialize(); return 63; }
    static_cast<void>(window.document_window().new_document());
    TBBUTTONINFO heading_button{sizeof(heading_button), TBIF_SIZE | TBIF_STATE};
    if (SendMessageW(window.toolbar()->handle(), TB_GETBUTTONINFOW,
            static_cast<WPARAM>(app::CommandId::format_body),
            reinterpret_cast<LPARAM>(&heading_button)) < 0 ||
        heading_button.cx < 80 || (heading_button.fsState & TBSTATE_ENABLED) == 0) {
        DestroyWindow(window.handle()); CoUninitialize(); return 38;
    }
    ShowWindow(window.handle(), SW_SHOWNOACTIVATE);
    UpdateWindow(window.handle());
    SetWindowPos(window.handle(), nullptr, 0, 0, 760, 520,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    const auto tooltip_window = reinterpret_cast<HWND>(
        SendMessageW(window.toolbar()->handle(), TB_GETTOOLTIPS, 0, 0));
    if (window.document_window().modes().reload("") != ErrorCode::ok) return 51;
    const auto empty_render = window.document_window().modes().render_view().handle();
    if (dispatcher.execute(app::CommandId::format_heading1) != ErrorCode::ok) return 52;
    if (session.snapshot().source != "# " || GetFocus() != empty_render) return 52;
    const auto heading_selection =
        window.document_window().modes().render_view().source_selection();
    if (!heading_selection.is_ok() || heading_selection.value().caret != 2 ||
        heading_selection.value().anchor != 2) return 53;
    SendMessageW(empty_render, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"在"));
    if (window.document_window().modes().render_view().synchronize_change() != ErrorCode::ok ||
        session.snapshot().source != "# 在") return 54;
    if (!tooltip_window ||
        (GetWindowLongPtrW(tooltip_window, GWL_STYLE) & TTS_ALWAYSTIP) == 0) return 50;
    if (window.document_window().modes().reload("段落\n") != ErrorCode::ok)
        return 42;
    SendMessageW(window.document_window().modes().render_view().handle(), EM_SETSEL, 1, 1);
    if (dispatcher.execute(app::CommandId::format_heading1) != ErrorCode::ok) return 43;
    if (session.snapshot().source != "# 段落\n") return 43;

    const auto toolbar_command = [&](app::CommandId command) {
        SendMessageW(window.handle(), WM_COMMAND,
            MAKEWPARAM(static_cast<WORD>(command), 0),
            reinterpret_cast<LPARAM>(window.toolbar()->handle()));
    };
    auto format_render = window.document_window().modes().render_view().handle();
    if (window.document_window().modes().reload("粗体") != ErrorCode::ok) return 82;
    SendMessageW(format_render, EM_SETSEL, 1, 1);
    toolbar_command(app::CommandId::format_bold);
    if (session.snapshot().source != "粗体") return 82;
    SendMessageW(format_render, EM_SETSEL, 0, 2);
    toolbar_command(app::CommandId::format_bold);
    if (session.snapshot().source != "**粗体**" ||
        !dispatcher.query(app::CommandId::format_bold).checked) return 83;
    toolbar_command(app::CommandId::format_bold);
    if (session.snapshot().source != "粗体" ||
        dispatcher.query(app::CommandId::format_bold).checked) return 84;

    if (window.document_window().modes().reload("代码") != ErrorCode::ok) return 85;
    SendMessageW(format_render, EM_SETSEL, 0, 2);
    toolbar_command(app::CommandId::format_code_block);
    if (session.snapshot().source.find("```") == std::string::npos ||
        session.snapshot().source.find("代码") == std::string::npos) return 85;
    if (!dispatcher.query(app::CommandId::format_code_block).checked) return 89;

    const std::string boundary_source =
        "## Heading\n\n"
        "Paragraph A **bold text** tail.\n\n"
        "Paragraph B";
    if (window.document_window().modes().reload(boundary_source) != ErrorCode::ok) return 90;
    FINDTEXTEXW find_paragraph{{0, -1}, const_cast<wchar_t*>(L"Paragraph A bold text tail."), {}};
    if (SendMessageW(format_render, EM_FINDTEXTEXW, FR_DOWN,
            reinterpret_cast<LPARAM>(&find_paragraph)) < 0) return 91;
    SendMessageW(format_render, EM_SETSEL, find_paragraph.chrgText.cpMin,
        find_paragraph.chrgText.cpMax);
    SetFocus(format_render);
    CHARRANGE before_toolbar{};
    SendMessageW(format_render, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&before_toolbar));
    RECT code_button{};
    if (!SendMessageW(window.toolbar()->handle(), TB_GETRECT,
            static_cast<WPARAM>(app::CommandId::format_code_block),
            reinterpret_cast<LPARAM>(&code_button))) return 92;
    const auto click = MAKELPARAM((code_button.left + code_button.right) / 2,
        (code_button.top + code_button.bottom) / 2);
    SendMessageW(window.toolbar()->handle(), WM_LBUTTONDOWN, MK_LBUTTON, click);
    SendMessageW(window.toolbar()->handle(), WM_LBUTTONUP, 0, click);
    MSG projection_message{};
    while (PeekMessageW(&projection_message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&projection_message);
        DispatchMessageW(&projection_message);
    }
    if (selection_at_toolbar_dispatch.cpMin != before_toolbar.cpMin ||
        selection_at_toolbar_dispatch.cpMax != before_toolbar.cpMax) return 93;
    if (session.snapshot().source !=
            "## Heading\n\n```\nParagraph A **bold text** tail.\n```\n\nParagraph B") return 94;
    if (window.document_window().modes().reload("段落") != ErrorCode::ok) return 86;
    SendMessageW(format_render, EM_SETSEL, 2, 2);
    toolbar_command(app::CommandId::insert_thematic_break);
    if (session.snapshot().source.find("---") == std::string::npos) return 86;

    struct BlockCase { const char* source; app::CommandId command; };
    constexpr std::array block_cases{
        BlockCase{"> 引用", app::CommandId::format_quote},
        BlockCase{"- 项目", app::CommandId::format_unordered_list},
        BlockCase{"1. 项目", app::CommandId::format_ordered_list},
        BlockCase{"- [ ] 项目", app::CommandId::format_task_list}};
    for (const auto& item : block_cases) {
        if (window.document_window().modes().reload(item.source) != ErrorCode::ok) return 87;
        SendMessageW(format_render, EM_SETSEL, 1, 1);
        if (!dispatcher.query(item.command).checked) return 87;
    }
    window.toolbar()->refresh();
    if ((SendMessageW(window.toolbar()->handle(), TB_GETSTATE,
            static_cast<WPARAM>(app::CommandId::view_style_yuan_lang), 0) &
            TBSTATE_CHECKED) != 0) return 88;

    if (window.document_window().modes().reload("深色正文\n") != ErrorCode::ok) return 47;
    window.set_theme_preference(ui::ThemePreference::dark);
    const auto dark_palette = ui::PaletteFor(ui::ThemeKind::dark);
    SendMessageW(window.document_window().modes().render_view().handle(), EM_SETSEL, 0, 4);
    CHARFORMAT2W dark_format{};
    dark_format.cbSize = sizeof(dark_format);
    SendMessageW(window.document_window().modes().render_view().handle(), EM_GETCHARFORMAT,
        SCF_SELECTION, reinterpret_cast<LPARAM>(&dark_format));
    if ((dark_format.dwMask & (CFM_COLOR | CFM_BACKCOLOR)) !=
            (CFM_COLOR | CFM_BACKCOLOR) ||
        dark_format.crTextColor != dark_palette.text ||
        dark_format.crBackColor != dark_palette.window) return 48;
    window.set_theme_preference(ui::ThemePreference::light);

    if (window.document_window().modes().reload("**粗体**\n") != ErrorCode::ok)
        return 44;
    SendMessageW(window.document_window().modes().render_view().handle(), EM_SETSEL, 0, 2);
    window.toolbar()->refresh();
    if (!dispatcher.query(app::CommandId::format_bold).checked ||
        !SendMessageW(window.toolbar()->handle(), TB_ISBUTTONCHECKED,
            static_cast<WPARAM>(app::CommandId::format_bold), 0)) return 45;
    TBBUTTONINFO button_info{};
    button_info.cbSize = sizeof(button_info);
    button_info.dwMask = TBIF_STYLE;
    RECT toolbar_button{};
    if (SendMessageW(window.toolbar()->handle(), TB_GETBUTTONINFO,
            static_cast<WPARAM>(app::CommandId::format_bold),
            reinterpret_cast<LPARAM>(&button_info)) < 0 ||
        (button_info.fsStyle & BTNS_SHOWTEXT) != 0 ||
        !SendMessageW(window.toolbar()->handle(), TB_GETRECT,
            static_cast<WPARAM>(app::CommandId::format_bold),
            reinterpret_cast<LPARAM>(&toolbar_button)) ||
        toolbar_button.right - toolbar_button.left <
            MulDiv(30, static_cast<int>(window.dpi()), 96)) {
        DestroyWindow(window.handle()); CoUninitialize(); return 37;
    }
    const auto render = window.document_window().modes().render_view().handle();
    if ((GetWindowLongPtrW(render, GWL_STYLE) & WS_VSCROLL) != 0) {
        DestroyWindow(window.handle()); CoUninitialize(); return 25;
    }
    std::string long_document;
    for (int line = 0; line < 200; ++line)
        long_document += "scroll line " + std::to_string(line) + "\n\n";
    if (window.document_window().modes().reload(long_document) != ErrorCode::ok) {
        DestroyWindow(window.handle()); CoUninitialize(); return 25;
    }
    ShowWindow(window.handle(), SW_SHOWNOACTIVATE);
    UpdateWindow(window.handle());
    SendMessageW(render, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
    if (SendMessageW(render, EM_GETFIRSTVISIBLELINE, 0, 0) <= 0) {
        DestroyWindow(window.handle()); CoUninitialize(); return 30;
    }
    if ((GetWindowLongPtrW(render, GWL_STYLE) & WS_VSCROLL) == 0) {
        DestroyWindow(window.handle()); CoUninitialize(); return 31;
    }
    if (window.document_window().new_document() != ErrorCode::ok) {
        DestroyWindow(window.handle()); CoUninitialize(); return 32;
    }
    if (GetMenu(window.handle()) != nullptr || !window.menu_controller() ||
        !window.menu_controller()->handle() ||
        window.menu_controller()->height() < MulDiv(36, static_cast<int>(window.dpi()), 96)) {
        DestroyWindow(window.handle()); CoUninitialize(); return 26;
    }
    const auto first_menu_button = GetDlgItem(window.handle(), 9000);
    wchar_t first_menu_text[32]{};
    if (!first_menu_button || GetParent(first_menu_button) != window.handle() ||
        GetWindowTextW(first_menu_button, first_menu_text,
            static_cast<int>(std::size(first_menu_text))) <= 0 ||
        std::wstring(first_menu_text).find(L"文件") == std::wstring::npos) {
        DestroyWindow(window.handle()); CoUninitialize(); return 38;
    }
    if ((GetWindowLongPtrW(window.menu_controller()->handle(), GWL_STYLE) &
            (WS_VISIBLE | WS_CLIPSIBLINGS)) != (WS_VISIBLE | WS_CLIPSIBLINGS) ||
        (GetWindowLongPtrW(first_menu_button, GWL_STYLE) & WS_CLIPSIBLINGS) == 0) {
        DestroyWindow(window.handle()); CoUninitialize(); return 77;
    }
    RECT menu_bar_bounds{};
    GetClientRect(window.menu_controller()->handle(), &menu_bar_bounds);
    RECT main_client{};
    GetClientRect(window.handle(), &main_client);
    if (menu_bar_bounds.right != main_client.right) {
        DestroyWindow(window.handle()); CoUninitialize(); return 78;
    }
    MEASUREITEMSTRUCT heading_measure{};
    heading_measure.CtlType = ODT_MENU;
    heading_measure.itemID = 9200;
    heading_measure.itemData = reinterpret_cast<ULONG_PTR>(L"正文");
    SendMessageW(window.handle(), WM_MEASUREITEM, 0,
        reinterpret_cast<LPARAM>(&heading_measure));
    if (heading_measure.itemHeight < static_cast<UINT>(
            MulDiv(32, static_cast<int>(window.dpi()), 96))) {
        DestroyWindow(window.handle()); CoUninitialize(); return 79;
    }
    MEASUREITEMSTRUCT popup_measure{};
    popup_measure.CtlType = ODT_MENU;
    popup_measure.itemData = reinterpret_cast<ULONG_PTR>(L"新建(&N)\tCtrl+N");
    SendMessageW(window.handle(), WM_MEASUREITEM, 0,
        reinterpret_cast<LPARAM>(&popup_measure));
    if (popup_measure.itemHeight < static_cast<UINT>(
            MulDiv(33, static_cast<int>(window.dpi()), 96))) {
        DestroyWindow(window.handle()); CoUninitialize(); return 39;
    }
    RECT formatting{};
    SendMessageW(render, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&formatting));
    if (formatting.left < MulDiv(7, static_cast<int>(window.dpi()), 96) ||
        formatting.top < MulDiv(7, static_cast<int>(window.dpi()), 96)) {
        DestroyWindow(window.handle()); CoUninitialize(); return 37;
    }
    static_assert(static_cast<std::uint16_t>(app::CommandId::file_new) == 100);
    static_assert(static_cast<std::uint16_t>(app::CommandId::file_exit) == 104);
    static_assert(static_cast<std::uint16_t>(app::CommandId::file_print) == 105);
    static_assert(static_cast<std::uint16_t>(app::CommandId::edit_undo) == 200);
    static_assert(static_cast<std::uint16_t>(app::CommandId::format_bold) == 300);
    static_assert(static_cast<std::uint16_t>(app::CommandId::view_render) == 400);
    if (!dispatcher.query(app::CommandId::file_open).enabled ||
        !dispatcher.query(app::CommandId::file_print).enabled ||
        !dispatcher.query(app::CommandId::edit_find).enabled ||
        !dispatcher.query(app::CommandId::edit_replace).enabled ||
        !dispatcher.query(app::CommandId::edit_insert_document).enabled ||
        !dispatcher.query(app::CommandId::edit_split_document).enabled ||
        !dispatcher.query(app::CommandId::view_render).checked) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 4;
    }
    if (dispatcher.execute(app::CommandId::edit_insert_document) != ErrorCode::ok ||
        dispatcher.execute(app::CommandId::edit_split_document) != ErrorCode::ok ||
        !insert_requested || !split_requested) return 76;
    if (!SendMessageW(window.toolbar()->handle(), TB_ISBUTTONENABLED,
            static_cast<WPARAM>(app::CommandId::edit_find), 0) ||
        SendMessageW(window.toolbar()->handle(), TB_ISBUTTONENABLED,
            static_cast<WPARAM>(app::CommandId::tools_settings), 0)) return 64;
    if (window.document_window().modes().reload("alpha beta\n") != ErrorCode::ok ||
        dispatcher.execute(app::CommandId::edit_find) != ErrorCode::ok) return 57;
    const auto find_edit = FindDescendantById(window.handle(), 8101);
    const auto find_next = FindDescendantById(window.handle(), 8103);
    if (!find_edit || !find_next || !IsWindowVisible(find_edit)) return 58;
    SetWindowTextW(find_edit, L"alpha");
    SendMessageW(GetParent(find_next), WM_COMMAND,
        MAKEWPARAM(8103, BN_CLICKED), reinterpret_cast<LPARAM>(find_next));
    const auto found_selection = window.document_window().modes().render_view().source_selection();
    if (!found_selection.is_ok() || found_selection.value().anchor != 0 ||
        found_selection.value().caret != 5) return 59;
    if (dispatcher.execute(app::CommandId::edit_find) != ErrorCode::ok ||
        IsWindowVisible(find_edit)) return 60;
    if (dispatcher.execute(app::CommandId::edit_replace) != ErrorCode::ok) return 61;
    const auto close_find = FindDescendantById(window.handle(), 8109);
    if (!close_find || !IsWindowVisible(find_edit)) return 62;
    SendMessageW(GetParent(close_find), WM_COMMAND,
        MAKEWPARAM(8109, BN_CLICKED), reinterpret_cast<LPARAM>(close_find));
    if (IsWindowVisible(find_edit)) return 63;
    if (std::wstring(ui::Toolbar::tooltip(
            static_cast<std::uint16_t>(app::CommandId::view_source))).find(
            L"源码模式") == std::wstring::npos) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 5;
    }
    if (dispatcher.execute(app::CommandId::view_source) != ErrorCode::ok ||
        !dispatcher.query(app::CommandId::view_source).checked ||
        dispatcher.query(app::CommandId::format_bold).enabled) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 6;
    }
    SendMessageW(window.handle(), WM_COMMAND,
        static_cast<WPARAM>(app::CommandId::view_split), 0);
    if (!dispatcher.query(app::CommandId::view_split).checked ||
        !SendMessageW(window.toolbar()->handle(), TB_ISBUTTONCHECKED,
            static_cast<WPARAM>(app::CommandId::view_split), 0) ||
        SendMessageW(window.toolbar()->handle(), TB_ISBUTTONENABLED,
            static_cast<WPARAM>(app::CommandId::format_bold), 0) ||
        dispatcher.execute(app::CommandId::file_exit) != ErrorCode::ok ||
        !exit_requested) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 7;
    }
    SetWindowPos(window.handle(), nullptr, 0, 0, 760, 520,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT client{};
    RECT document{};
    GetClientRect(window.handle(), &client);
    GetWindowRect(window.document_window().handle(), &document);
    POINT corners[2]{{document.left, document.top}, {document.right, document.bottom}};
    MapWindowPoints(nullptr, window.handle(), corners, 2);
    if (corners[0].x != 0 ||
        corners[0].y != window.menu_controller()->height() + window.toolbar()->height() ||
        corners[1].x != client.right ||
        corners[1].y != client.bottom - window.status_bar().height()) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 8;
    }
    if (dispatcher.query(app::CommandId::view_outline).checked ||
        !window.menu_controller()->dispatch(static_cast<std::uint16_t>(
            app::CommandId::view_outline)) ||
        window.document_window().outline_visible()) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 38;
    }
    GetWindowRect(window.document_window().handle(), &document);
    corners[0] = {document.left, document.top};
    MapWindowPoints(nullptr, window.handle(), corners, 1);
    if (corners[0].x != 0) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 39;
    }
    const auto before = session.snapshot();
    document::EditTransaction edit{1, before.source_revision,
        document::EditOrigin::source_view,
        {{{0, 0}, "状态栏"}}};
    if (session.commit(edit) != ErrorCode::ok) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 9;
    }
    wchar_t status[64]{};
    SendMessageW(window.status_bar().handle(), SB_GETTEXTW, 1,
        reinterpret_cast<LPARAM>(status));
    if (std::wstring(status) != L"未保存") {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 10;
    }
    if (dispatcher.query(app::CommandId::file_open).enabled) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 11;
    }
    window.status_bar().set_file_format(fileio::TextEncoding::utf16_le,
        fileio::LineEnding::lf);
    SendMessageW(window.status_bar().handle(), SB_GETTEXTW, 2,
        reinterpret_cast<LPARAM>(status));
    if (std::wstring(status) != L"编码方式：UTF-16 LE") {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 12;
    }
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory temporary{std::filesystem::temp_directory_path() /
        ("markdownmay-app-files-" + std::to_string(nonce))};
    std::filesystem::create_directories(temporary.path);
    const auto first = temporary.path / L"首次保存.md";
    if (window.document_window().save_document_as(first) != ErrorCode::ok ||
        session.is_dirty() || !std::filesystem::exists(first)) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 13;
    }
    const auto second = temporary.path / L"打开文档.markdown";
    const std::string opened_source = "# 打开成功\n\nUTF-16 文件\n";
    if (fileio::SaveTextFileAtomic({second, opened_source,
            fileio::TextEncoding::utf16_le, fileio::LineEnding::lf}) !=
            ErrorCode::ok ||
        window.document_window().open_document(second) != ErrorCode::ok ||
        session.snapshot().source != opened_source ||
        window.document_window().encoding() != fileio::TextEncoding::utf16_le ||
        window.document_window().line_ending() != fileio::LineEnding::lf ||
        window.document_window().modes().mode() != editor::ViewMode::render) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 14;
    }
    const auto rich = window.document_window().modes().render_view().handle();
    CHARRANGE opened_selection{};
    SendMessageW(rich, EM_EXGETSEL, 0,
        reinterpret_cast<LPARAM>(&opened_selection));
    if (opened_selection.cpMin != 0 || opened_selection.cpMax != 0 ||
        SendMessageW(rich, EM_GETFIRSTVISIBLELINE, 0, 0) != 0) {
        DestroyWindow(window.handle()); CoUninitialize(); return 27;
    }
    if (dispatcher.execute(app::CommandId::view_source) != ErrorCode::ok ||
        window.document_window().modes().mode() != editor::ViewMode::source ||
        !IsWindowVisible(window.document_window().modes().split_view().handle()) ||
        IsWindowVisible(rich) ||
        dispatcher.execute(app::CommandId::view_split) != ErrorCode::ok ||
        window.document_window().modes().mode() != editor::ViewMode::split ||
        !IsWindowVisible(window.document_window().modes().split_view().render_view().handle()) ||
        dispatcher.execute(app::CommandId::view_render) != ErrorCode::ok) {
        DestroyWindow(window.handle()); CoUninitialize(); return 96;
    }
    const auto pump_mode_messages = [] {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    };
    const auto toolbar = window.toolbar()->handle();
    SendMessageW(window.handle(), WM_COMMAND,
        MAKEWPARAM(static_cast<WORD>(app::CommandId::view_source), 0),
        reinterpret_cast<LPARAM>(toolbar));
    pump_mode_messages();
    if (window.document_window().modes().mode() != editor::ViewMode::source ||
        !IsWindowVisible(window.document_window().modes().split_view().handle()) ||
        IsWindowVisible(rich) ||
        IsWindowVisible(window.document_window().modes().split_view().render_view().handle()))
        return 97;
    SendMessageW(window.handle(), WM_COMMAND,
        MAKEWPARAM(static_cast<WORD>(app::CommandId::view_render), 0),
        reinterpret_cast<LPARAM>(toolbar));
    pump_mode_messages();
    SendMessageW(window.handle(), WM_COMMAND,
        MAKEWPARAM(static_cast<WORD>(app::CommandId::view_source), 0),
        reinterpret_cast<LPARAM>(toolbar));
    pump_mode_messages();
    if (window.document_window().modes().mode() != editor::ViewMode::source ||
        !IsWindowVisible(window.document_window().modes().split_view().handle()) ||
        IsWindowVisible(rich) ||
        IsWindowVisible(window.document_window().modes().split_view().render_view().handle()))
        return 98;
    if (dispatcher.execute(app::CommandId::view_render) != ErrorCode::ok) return 99;
    SendMessageW(rich, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    SendMessageW(rich, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"追加\r"));
    if (window.document_window().save_document() != ErrorCode::ok) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 15;
    }
    const auto loaded = fileio::LoadTextFile(second);
    if (!loaded.is_ok() || loaded.value().source != opened_source + "追加\n" ||
        loaded.value().encoding != fileio::TextEncoding::utf16_le ||
        loaded.value().line_ending != fileio::LineEnding::lf ||
        window.document_window().new_document() != ErrorCode::ok ||
        window.document_window().is_named() || session.is_dirty() ||
        !session.snapshot().source.empty()) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 16;
    }
    const auto text_file = temporary.path / L"纯文本.txt";
    const std::string text_source = "# 不是标题\n* 不是列表\n";
    if (fileio::SaveTextFileAtomic({text_file, text_source,
            fileio::TextEncoding::utf8_bom, fileio::LineEnding::crlf}) != ErrorCode::ok ||
        window.document_window().open_document(text_file) != ErrorCode::ok ||
        session.kind() != document::DocumentKind::plain_text ||
        session.snapshot().semantic || session.can_export() ||
        window.document_window().modes().mode() != editor::ViewMode::source ||
        dispatcher.query(app::CommandId::view_render).enabled ||
        dispatcher.query(app::CommandId::view_split).enabled ||
        dispatcher.query(app::CommandId::view_outline).enabled ||
        dispatcher.query(app::CommandId::format_bold).enabled ||
        dispatcher.query(app::CommandId::file_export).enabled ||
        dispatcher.query(app::CommandId::file_print).enabled ||
        !dispatcher.query(app::CommandId::edit_find).enabled) {
        DestroyWindow(window.handle()); CoUninitialize(); return 71;
    }
    window.status_bar().refresh();
    SendMessageW(window.status_bar().handle(), SB_GETTEXTW, 0,
        reinterpret_cast<LPARAM>(status));
    if (std::wstring(status) != L"纯文本" ||
        window.document_window().save_document() != ErrorCode::ok) {
        DestroyWindow(window.handle()); CoUninitialize(); return 72;
    }
    const auto text_reopened = fileio::LoadTextFile(text_file);
    if (!text_reopened.is_ok() || text_reopened.value().source !=
            fileio::NormalizeLineEndings(text_source, fileio::LineEnding::crlf) ||
        text_reopened.value().encoding != fileio::TextEncoding::utf8_bom ||
        text_reopened.value().line_ending != fileio::LineEnding::crlf) {
        DestroyWindow(window.handle()); CoUninitialize(); return 73;
    }
    const auto converted_markdown = temporary.path / L"纯文本转 Markdown.md";
    if (window.document_window().save_document_as(converted_markdown) != ErrorCode::ok ||
        session.kind() != document::DocumentKind::markdown || !session.can_export() ||
        session.snapshot().source != fileio::NormalizeLineEndings(
            text_source, fileio::LineEnding::crlf)) {
        DestroyWindow(window.handle()); CoUninitialize(); return 74;
    }
    if (window.document_window().new_document() != ErrorCode::ok) {
        DestroyWindow(window.handle()); CoUninitialize(); return 75;
    }
    const auto dropped = second.wstring();
    const auto drop_bytes = sizeof(DROPFILES) +
        (dropped.size() + 1) * sizeof(wchar_t);
    const auto drop_memory = GlobalAlloc(GHND, drop_bytes);
    if (!drop_memory) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 17;
    }
    auto* drop_data = static_cast<DROPFILES*>(GlobalLock(drop_memory));
    if (!drop_data) {
        GlobalFree(drop_memory);
        DestroyWindow(window.handle());
        CoUninitialize();
        return 18;
    }
    drop_data->pFiles = sizeof(DROPFILES);
    drop_data->fWide = TRUE;
    std::memcpy(reinterpret_cast<std::byte*>(drop_data) + sizeof(DROPFILES),
        dropped.c_str(), (dropped.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(drop_memory);
    SendMessageW(window.handle(), WM_DROPFILES,
        reinterpret_cast<WPARAM>(drop_memory), 0);
    if (dropped_path != second) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 19;
    }
    if (window.document_window().open_document(second) != ErrorCode::ok ||
        window.document_window().has_external_change()) {
        DestroyWindow(window.handle()); CoUninitialize(); return 20;
    }
    if (fileio::SaveTextFileAtomic({second, "# external\n",
            fileio::TextEncoding::utf8, fileio::LineEnding::lf}) != ErrorCode::ok ||
        !window.document_window().has_external_change()) {
        DestroyWindow(window.handle()); CoUninitialize(); return 21;
    }
    window.document_window().acknowledge_external_change();
    if (window.document_window().has_external_change()) {
        DestroyWindow(window.handle()); CoUninitialize(); return 22;
    }
    SetFileAttributesW(second.c_str(), FILE_ATTRIBUTE_READONLY);
    if (window.document_window().open_document(second) != ErrorCode::ok ||
        !window.document_window().is_read_only() ||
        window.document_window().save_document() != ErrorCode::file_read_only) {
        SetFileAttributesW(second.c_str(), FILE_ATTRIBUTE_NORMAL);
        DestroyWindow(window.handle()); CoUninitialize(); return 23;
    }
    window.refresh_document_chrome();
    const auto read_only_title_length = GetWindowTextLengthW(window.handle());
    std::wstring read_only_title(static_cast<std::size_t>(read_only_title_length) + 1U, L'\0');
    GetWindowTextW(window.handle(), read_only_title.data(), read_only_title_length + 1);
    read_only_title.resize(static_cast<std::size_t>(read_only_title_length));
    if (read_only_title.find(L"[只读]") == std::wstring::npos) {
        SetFileAttributesW(second.c_str(), FILE_ATTRIBUTE_NORMAL);
        DestroyWindow(window.handle()); CoUninitialize(); return 45;
    }
    wchar_t read_only_status[32]{};
    SendMessageW(window.status_bar().handle(), SB_GETTEXTW, 1,
        reinterpret_cast<LPARAM>(read_only_status));
    if (std::wstring(read_only_status) != L"只读") {
        SetFileAttributesW(second.c_str(), FILE_ATTRIBUTE_NORMAL);
        DestroyWindow(window.handle()); CoUninitialize(); return 46;
    }
    const auto read_only_source = session.snapshot().source;
    const auto read_only_rich = window.document_window().modes().render_view().handle();
    SendMessageW(read_only_rich, EM_SETSEL, 0, 0);
    SendMessageW(read_only_rich, EM_REPLACESEL, TRUE,
        reinterpret_cast<LPARAM>(L"禁止写入"));
    if (session.snapshot().source != read_only_source) {
        SetFileAttributesW(second.c_str(), FILE_ATTRIBUTE_NORMAL);
        DestroyWindow(window.handle()); CoUninitialize(); return 47;
    }
    SetFileAttributesW(second.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (window.document_window().new_document() != ErrorCode::ok) {
        DestroyWindow(window.handle()); CoUninitialize(); return 28;
    }
    SetFocus(rich);
    SendMessageW(rich, EM_SETSEL, 0, 0);
    const auto pump = [] {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    };
    SendMessageW(rich, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"正文"));
    pump();
    SendMessageW(rich, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"\r"));
    pump();
    SendMessageW(rich, WM_CHAR, L'-', 0);
    pump();
    SendMessageW(rich, WM_CHAR, L'-', 0);
    pump();
    SendMessageW(rich, WM_CHAR, L'-', 0);
    pump();
    const auto live = session.snapshot();
    if (live.source.find("---") == std::string::npos) {
        DestroyWindow(window.handle()); CoUninitialize(); return 33;
    }
    if (live.source.find("\n\n---") == std::string::npos &&
        live.source.find("\r\n\r\n---") == std::string::npos) {
        DestroyWindow(window.handle()); CoUninitialize(); return 38;
    }
    if (!live.semantic || !ContainsKind(*live.semantic->root(),
            document::NodeKind::thematic_break)) {
        DestroyWindow(window.handle()); CoUninitialize(); return 29;
    }
    window.set_close_callback([] { return false; });
    SendMessageW(window.handle(), WM_CLOSE, 0, 0);
    if (!IsWindow(window.handle())) { CoUninitialize(); return 24; }
    window.set_close_callback([] { return true; });
    SendMessageW(window.handle(), WM_CLOSE, 0, 0);
    CoUninitialize();
    return 0;
}
