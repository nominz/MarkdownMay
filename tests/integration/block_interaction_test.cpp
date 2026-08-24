#include "markdownmay/editor/richedit_host.hpp"

#include <richedit.h>

#include <string>
#include <thread>

namespace {
std::wstring ReadWide(HWND window) {
    const auto length = GetWindowTextLengthW(window);
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(window, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(length));
    return value;
}
POINT PointAt(HWND window, LONG position) {
    POINT point{};
    SendMessageW(window, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&point), position);
    return point;
}
bool HasInk(markdownmay::editor::RichEditHost& host, const RECT& area) {
    const auto screen = GetDC(host.handle());
    const auto memory = CreateCompatibleDC(screen);
    const auto bitmap = CreateCompatibleBitmap(screen, 900, 500);
    const auto old_bitmap = SelectObject(memory, bitmap);
    RECT canvas{0, 0, 900, 500};
    FillRect(memory, &canvas, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    host.draw_block_interaction(memory);
    bool found{};
    for (auto y = area.top; y < area.bottom && !found; ++y)
        for (auto x = area.left; x < area.right; ++x)
            if (GetPixel(memory, x, y) != RGB(255, 255, 255)) {
                found = true;
                break;
            }
    SelectObject(memory, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(host.handle(), screen);
    return found;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    using namespace markdownmay;
    document::DocumentSession session(
        "# Heading\n\nparagraph\n\n## Second\nbody\n\n- parent\n- item\n");
    const auto parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED,
        0, 0, 900, 500, nullptr, nullptr, instance, nullptr);
    editor::RichEditHost host(session);
    RECT bounds{0, 0, 900, 500};
    if (!parent || host.create(parent, bounds) != ErrorCode::ok) return 1;
    ShowWindow(host.handle(), SW_SHOW);
    ShowWindow(parent, SW_SHOW);
    UpdateWindow(parent);

    const auto rendered = ReadWide(host.handle());
    const auto heading_position = static_cast<LONG>(rendered.find(L"Heading"));
    const auto paragraph_position = static_cast<LONG>(rendered.find(L"paragraph"));
    const auto item_position = static_cast<LONG>(rendered.find(L"item"));
    if (heading_position < 0 || paragraph_position < 0 || item_position < 0) return 2;
    const auto heading_point = PointAt(host.handle(), heading_position);
    if (!host.update_block_hover({200, heading_point.y + 2})) return 3;
    const auto heading = host.hovered_block();
    if (!heading || heading->kind != document::NodeKind::heading) return 4;
    const auto type_rect = host.block_type_hit_rect();
    const auto handle_rect = host.block_handle_hit_rect();
    if (!IsWindowVisible(host.block_type_window()) ||
        !IsWindowVisible(host.block_handle_window())) return 14;
    wchar_t accessible_name[128]{};
    GetWindowTextW(host.block_handle_window(), accessible_name, 128);
    wchar_t accessible_class[32]{};
    GetClassNameW(host.block_handle_window(), accessible_class, 32);
    if (std::wstring(accessible_name).find(L"块操作") == std::wstring::npos ||
        std::wstring(accessible_class) != L"Button" ||
        (GetWindowLongPtrW(host.block_handle_window(), GWL_STYLE) & WS_TABSTOP) == 0)
        return 15;
    const auto heading_capabilities = host.query_block_menu(*heading);
    if (!heading_capabilities.convert || !heading_capabilities.copy ||
        heading_capabilities.indent) return 16;
    const auto heading_menu = host.create_block_context_menu(*heading);
    if (!heading_menu || GetMenuItemCount(heading_menu) != 10 ||
        !GetSubMenu(heading_menu, 0) || GetMenuItemCount(GetSubMenu(heading_menu, 0)) != 7) {
        if (heading_menu) DestroyMenu(heading_menu);
        return 17;
    }
    DestroyMenu(heading_menu);
    if (IsRectEmpty(&type_rect) || IsRectEmpty(&handle_rect) ||
        type_rect.right > handle_rect.left || !HasInk(host, type_rect) ||
        !HasInk(host, handle_rect)) return 5;
    POINT menu_point{handle_rect.left, handle_rect.bottom};
    ClientToScreen(host.handle(), &menu_point);
    std::thread close_menu([window = host.handle()] {
        Sleep(100);
        PostMessageW(window, WM_CANCELMODE, 0, 0);
    });
    const auto menu_opened = host.show_block_context_menu(*heading, menu_point);
    close_menu.join();
    if (!menu_opened || GetFocus() != host.handle()) return 21;
    std::thread close_keyboard_menu([window = host.handle()] {
        Sleep(100);
        PostMessageW(window, WM_CANCELMODE, 0, 0);
    });
    SendMessageW(host.handle(), WM_KEYDOWN, VK_APPS, 0);
    close_keyboard_menu.join();
    if (GetFocus() != host.handle()) return 22;

    const auto paragraph_point = PointAt(host.handle(), paragraph_position);
    SendMessageW(host.handle(), WM_MOUSEMOVE, 0,
        MAKELPARAM(300, paragraph_point.y + 2));
    const auto paragraph = host.hovered_block();
    const auto paragraph_type = host.block_type_hit_rect();
    const auto paragraph_handle = host.block_handle_hit_rect();
    if (!paragraph || paragraph->kind != document::NodeKind::paragraph ||
        !IsRectEmpty(&paragraph_type) || IsRectEmpty(&paragraph_handle)) return 7;
    if (IsWindowVisible(host.block_type_window()) == TRUE ||
        !IsWindowVisible(host.block_handle_window())) return 18;
    if (session.is_dirty() || session.snapshot().source_revision != 1) return 13;
    const auto list_item = host.block_context_at_source(
        static_cast<std::uint64_t>(session.snapshot().source.find("item")));
    if (!list_item || list_item->kind != document::NodeKind::list_item) return 19;
    const auto list_capabilities = host.query_block_menu(*list_item);
    if (list_capabilities.convert || !list_capabilities.indent ||
        list_capabilities.outdent || list_capabilities.add_below) return 20;

    SendMessageW(host.handle(), WM_MOUSELEAVE, 0, 0);
    if (host.hovered_block()) return 8;
    host.apply_appearance(RGB(230, 230, 230), RGB(32, 32, 32), 144);
    const auto scaled_heading_point = PointAt(host.handle(), heading_position);
    if (!host.update_block_hover({300, scaled_heading_point.y + 2})) return 9;
    const auto scaled_type = host.block_type_hit_rect();
    const auto scaled_handle = host.block_handle_hit_rect();
    if (scaled_type.left != MulDiv(31, 144, 96) ||
        scaled_handle.left != MulDiv(55, 144, 96) ||
        scaled_type.right > scaled_handle.left) return 10;
    host.apply_appearance(GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_WINDOW), 288);
    const auto dpi_heading_point = PointAt(host.handle(), heading_position);
    if (!host.update_block_hover({600, dpi_heading_point.y + 2})) return 44;
    const auto dpi_type = host.block_type_hit_rect();
    const auto dpi_handle = host.block_handle_hit_rect();
    if (dpi_type.left != MulDiv(31, 288, 96) ||
        dpi_handle.left != MulDiv(55, 288, 96) || dpi_type.right > dpi_handle.left ||
        !IsWindowVisible(host.block_type_window()) ||
        !IsWindowVisible(host.block_handle_window())) return 45;
    const auto process = GetCurrentProcess();
    const auto user_before = GetGuiResources(process, GR_USEROBJECTS);
    const auto gdi_before = GetGuiResources(process, GR_GDIOBJECTS);
    for (int index = 0; index < 200; ++index) {
        const auto menu = host.create_block_context_menu(*heading);
        if (!menu) return 46;
        DestroyMenu(menu);
    }
    const auto user_after = GetGuiResources(process, GR_USEROBJECTS);
    const auto gdi_after = GetGuiResources(process, GR_GDIOBJECTS);
    if (user_after > user_before + 2 || gdi_after > gdi_before + 2) return 47;
    SendMessageW(host.handle(), WM_VSCROLL, SB_LINEDOWN, 0);
    if (host.hovered_block()) return 11;

    auto command_context = host.block_context_at_source(
        static_cast<std::uint64_t>(session.snapshot().source.find("Heading")));
    if (!command_context || host.execute_block_menu(
        editor::BlockMenuCommand::convert_h3, *command_context) != ErrorCode::ok ||
        session.snapshot().source.find("### Heading") != 0) return 23;
    if (host.execute_block_menu(editor::BlockMenuCommand::remove, *command_context) !=
        ErrorCode::editor_transaction_conflict) return 24;
    if (host.undo() != ErrorCode::ok || session.snapshot().source.find("# Heading") != 0)
        return 25;
    command_context = host.block_context_at_source(
        static_cast<std::uint64_t>(session.snapshot().source.find("Heading")));
    if (!command_context || host.execute_block_menu(
        editor::BlockMenuCommand::convert_paragraph, *command_context) != ErrorCode::ok ||
        session.snapshot().source.find("Heading") != 0 || host.undo() != ErrorCode::ok)
        return 37;

    command_context = host.block_context_at_source(
        static_cast<std::uint64_t>(session.snapshot().source.find("paragraph")));
    if (!command_context || host.execute_block_menu(
        editor::BlockMenuCommand::convert_h6, *command_context) != ErrorCode::ok ||
        session.snapshot().source.find("###### paragraph") == std::string::npos ||
        host.undo() != ErrorCode::ok) return 38;
    command_context = host.block_context_at_source(
        static_cast<std::uint64_t>(session.snapshot().source.find("paragraph")));
    const auto before_add = session.snapshot();
    const auto add_end = command_context ? command_context->source_range.end : 0;
    if (!command_context || host.execute_block_menu(
        editor::BlockMenuCommand::add_below, *command_context) != ErrorCode::ok ||
        session.snapshot().source.size() != before_add.source.size() + 1 ||
        session.snapshot().source.substr(static_cast<std::size_t>(add_end), 3) != "\n\n\n")
        return 39;
    const auto added_selection = host.source_selection();
    if (!added_selection.is_ok() || added_selection.value().caret != add_end + 1 ||
        host.undo() != ErrorCode::ok || session.snapshot().source != before_add.source)
        return 40;
    command_context = host.block_context_at_source(
        static_cast<std::uint64_t>(session.snapshot().source.find("paragraph")));
    const auto before_copy = session.snapshot();
    if (!command_context || host.execute_block_menu(
        editor::BlockMenuCommand::copy, *command_context) != ErrorCode::ok) return 26;
    if (session.snapshot().source_revision != before_copy.source_revision ||
        session.snapshot().source != before_copy.source) return 27;
    if (!OpenClipboard(parent)) return 28;
    const auto clipboard_data = GetClipboardData(CF_UNICODETEXT);
    const auto* clipboard_text = clipboard_data
        ? static_cast<const wchar_t*>(GlobalLock(clipboard_data)) : nullptr;
    const auto copied = clipboard_text ? std::wstring(clipboard_text) : std::wstring{};
    if (clipboard_text) GlobalUnlock(clipboard_data);
    CloseClipboard();
    if (copied.find(L"paragraph") == std::wstring::npos) return 29;

    if (host.execute_block_menu(editor::BlockMenuCommand::cut, *command_context) !=
        ErrorCode::ok || session.snapshot().source.find("paragraph") != std::string::npos)
        return 30;
    if (host.undo() != ErrorCode::ok ||
        session.snapshot().source.find("paragraph") == std::string::npos) return 31;

    command_context = host.block_context_at_source(
        static_cast<std::uint64_t>(session.snapshot().source.find("Second")));
    if (!command_context || host.execute_block_menu(
        editor::BlockMenuCommand::remove, *command_context) != ErrorCode::ok ||
        session.snapshot().source.find("Second") != std::string::npos) return 32;
    if (host.undo() != ErrorCode::ok ||
        session.snapshot().source.find("Second") == std::string::npos) return 33;

    command_context = host.block_context_at_source(
        static_cast<std::uint64_t>(session.snapshot().source.find("item")));
    if (!command_context || host.execute_block_menu(
        editor::BlockMenuCommand::indent, *command_context) != ErrorCode::ok ||
        session.snapshot().source.find("    - item") == std::string::npos) return 34;
    command_context = host.block_context_at_source(
        static_cast<std::uint64_t>(session.snapshot().source.find("item")));
    if (!command_context || host.execute_block_menu(
        editor::BlockMenuCommand::outdent, *command_context) != ErrorCode::ok ||
        session.snapshot().source.find("\n- item") == std::string::npos) return 35;
    if (host.undo() != ErrorCode::ok ||
        session.snapshot().source.find("    - item") == std::string::npos ||
        host.undo() != ErrorCode::ok ||
        session.snapshot().source.find("\n- item") == std::string::npos) return 36;

    document::DocumentSession plain("# plain", document::DocumentKind::plain_text);
    editor::BlockInteractionController plain_controller;
    if (plain_controller.refresh(1, plain.snapshot(), {})) return 12;

    document::DocumentSession crlf("first\r\n\r\ntail");
    editor::RichEditHost crlf_host(crlf);
    RECT crlf_bounds{0, 0, 400, 200};
    if (crlf_host.create(parent, crlf_bounds) != ErrorCode::ok) return 41;
    const auto tail = crlf_host.block_context_at_source(
        static_cast<std::uint64_t>(crlf.snapshot().source.find("tail")));
    if (!tail || crlf_host.execute_block_menu(
        editor::BlockMenuCommand::add_below, *tail) != ErrorCode::ok ||
        crlf.snapshot().source != "first\r\n\r\ntail\r\n\r\n") return 42;
    const auto tail_selection = crlf_host.source_selection();
    if (!tail_selection.is_ok() || tail_selection.value().caret != tail->source_range.end + 2 ||
        crlf_host.undo() != ErrorCode::ok || crlf.snapshot().source != "first\r\n\r\ntail")
        return 43;
    DestroyWindow(parent);
    return 0;
}
