#include "markdownmay/editor/view_mode_controller.hpp"

#include <Scintilla.h>
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
bool HiddenAt(HWND window, LONG position) {
    CHARRANGE saved{};
    SendMessageW(window, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&saved));
    CHARRANGE range{position, position + 1};
    SendMessageW(window, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
    CHARFORMAT2W format{};
    format.cbSize = sizeof(format);
    SendMessageW(window, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
    SendMessageW(window, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&saved));
    return (format.dwMask & CFM_HIDDEN) != 0 && (format.dwEffects & CFE_HIDDEN) != 0;
}
POINT PointAt(HWND window, LONG position) {
    POINT point{};
    SendMessageW(window, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&point), position);
    return point;
}
bool DrawsAnyTableGrid(markdownmay::editor::RichEditHost& host) {
    const auto screen = GetDC(host.handle());
    const auto memory = CreateCompatibleDC(screen);
    const auto bitmap = CreateCompatibleBitmap(screen, 900, 600);
    const auto old_bitmap = SelectObject(memory, bitmap);
    RECT canvas{0, 0, 900, 600};
    FillRect(memory, &canvas, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    host.draw_table_grid(memory);
    bool found{};
    for (int y = 0; y < 600 && !found; ++y)
        for (int x = 0; x < 900; ++x)
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
        "# A\nintro\n\n| x | y |\n|---|---|\n| 1 | 2 |\n\n## B\nbody\n# C\nend\n");
    HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED,
        0, 0, 900, 600, nullptr, nullptr, instance, nullptr);
    editor::ViewModeController modes(session);
    RECT bounds{0, 0, 900, 600};
    if (!parent || modes.create(parent, bounds) != ErrorCode::ok) return 1;
    const auto rendered = ReadWide(modes.render_view().handle());
    const auto body = static_cast<LONG>(rendered.find(L"intro"));
    const auto first_break = static_cast<LONG>(rendered.find(L'\r'));
    const auto next_heading = static_cast<LONG>(rendered.rfind(L'C'));
    bool clicked{};
    const auto fold_x = MulDiv(16, static_cast<int>(GetDpiForWindow(
        modes.render_view().handle())), 96);
    for (int y = 0; y < 100 && !clicked; ++y)
        clicked = modes.render_view().handle_heading_fold_click({fold_x, y});
    if (body < 0 || !clicked || !HiddenAt(modes.render_view().handle(), body))
        return 2;
    if (first_break < 0 || next_heading < 0 ||
        HiddenAt(modes.render_view().handle(), first_break) ||
        PointAt(modes.render_view().handle(), next_heading).y <=
            PointAt(modes.render_view().handle(), 0).y) return 12;
    if (DrawsAnyTableGrid(modes.render_view())) return 13;
    if (session.is_dirty() || session.snapshot().source_revision != 1) return 3;
    if (modes.switch_to(editor::ViewMode::split) != ErrorCode::ok) return 4;
    const auto intro_source = static_cast<WPARAM>(session.snapshot().source.find("intro"));
    const auto intro_line = SendMessageW(modes.source_view().handle(), SCI_LINEFROMPOSITION,
        intro_source, 0);
    if (SendMessageW(modes.source_view().handle(), SCI_GETLINEVISIBLE, intro_line, 0) != 0)
        return 5;
    const auto nested_line = SendMessageW(modes.source_view().handle(), SCI_LINEFROMPOSITION,
        static_cast<WPARAM>(session.snapshot().source.find("## B")), 0);
    if (SendMessageW(modes.source_view().handle(), SCI_GETLINEVISIBLE, nested_line, 0) != 0)
        return 6;
    if (modes.navigate_to_source(static_cast<std::uint64_t>(intro_source)) != ErrorCode::ok)
        return 7;
    if (SendMessageW(modes.source_view().handle(), SCI_GETLINEVISIBLE, intro_line, 0) == 0 ||
        HiddenAt(modes.split_view().render_view().handle(), body)) return 8;
    if (!modes.toggle_heading_fold_at(0) || !modes.toggle_heading_fold_at(
        static_cast<std::uint64_t>(session.snapshot().source.find("## B")))) return 9;
    if (!modes.toggle_heading_fold_at(0)) return 10;
    const auto body_line = SendMessageW(modes.source_view().handle(), SCI_LINEFROMPOSITION,
        static_cast<WPARAM>(session.snapshot().source.find("body")), 0);
    if (SendMessageW(modes.source_view().handle(), SCI_GETLINEVISIBLE, body_line, 0) != 0)
        return 11;
    DestroyWindow(parent);
    return 0;
}
