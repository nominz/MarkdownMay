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
        "# Heading\n\nparagraph\n\n## Second\nbody\n\n- item\n");
    const auto parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED,
        0, 0, 900, 500, nullptr, nullptr, instance, nullptr);
    editor::RichEditHost host(session);
    RECT bounds{0, 0, 900, 500};
    if (!parent || host.create(parent, bounds) != ErrorCode::ok) return 1;
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
        !list_capabilities.outdent || list_capabilities.add_below) return 20;

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
    SendMessageW(host.handle(), WM_VSCROLL, SB_LINEDOWN, 0);
    if (host.hovered_block()) return 11;

    document::DocumentSession plain("# plain", document::DocumentKind::plain_text);
    editor::BlockInteractionController plain_controller;
    if (plain_controller.refresh(1, plain.snapshot(), {})) return 12;
    DestroyWindow(parent);
    return 0;
}
