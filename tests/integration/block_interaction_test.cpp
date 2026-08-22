#include "markdownmay/editor/richedit_host.hpp"

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
        "# Heading\n\nparagraph\n\n## Second\nbody\n");
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
    if (heading_position < 0 || paragraph_position < 0) return 2;
    const auto heading_point = PointAt(host.handle(), heading_position);
    if (!host.update_block_hover({200, heading_point.y + 2})) return 3;
    const auto heading = host.hovered_block();
    if (!heading || heading->kind != document::NodeKind::heading) return 4;
    const auto type_rect = host.block_type_hit_rect();
    const auto handle_rect = host.block_handle_hit_rect();
    if (IsRectEmpty(&type_rect) || IsRectEmpty(&handle_rect) ||
        type_rect.right > handle_rect.left || !HasInk(host, type_rect) ||
        !HasInk(host, handle_rect)) return 5;

    const auto paragraph_point = PointAt(host.handle(), paragraph_position);
    SendMessageW(host.handle(), WM_MOUSEMOVE, 0,
        MAKELPARAM(300, paragraph_point.y + 2));
    const auto paragraph = host.hovered_block();
    const auto paragraph_type = host.block_type_hit_rect();
    const auto paragraph_handle = host.block_handle_hit_rect();
    if (!paragraph || paragraph->kind != document::NodeKind::paragraph ||
        !IsRectEmpty(&paragraph_type) || IsRectEmpty(&paragraph_handle)) return 7;
    if (session.is_dirty() || session.snapshot().source_revision != 1) return 13;

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
