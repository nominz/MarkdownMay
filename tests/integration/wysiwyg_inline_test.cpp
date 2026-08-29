#include "markdownmay/editor/richedit_host.hpp"

#include <richedit.h>
#include <commdlg.h>

#include <array>
#include <cstdlib>
#include <string>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED,
                                  0, 0, 400, 300, nullptr, nullptr, instance, nullptr);
    if (!parent) return 1;
    markdownmay::document::DocumentSession session("**粗体** 和 [链接](local.md)");
    markdownmay::editor::RichEditHost host(session);
    RECT bounds{0, 0, 400, 300};
    if (host.create(parent, bounds) != markdownmay::ErrorCode::ok) return 2;

    std::array<wchar_t, 64> visible{};
    GetWindowTextW(host.handle(), visible.data(), static_cast<int>(visible.size()));
    if (std::wstring_view(visible.data()) != L"粗体 和 链接") return 3;
    SendMessageW(host.handle(), EM_SETSEL, 0, 2);
    CHARFORMAT2W format{};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_BOLD;
    SendMessageW(host.handle(), EM_GETCHARFORMAT, SCF_SELECTION,
                 reinterpret_cast<LPARAM>(&format));
    if ((format.dwEffects & CFE_BOLD) == 0) return 4;

    SendMessageW(host.handle(), EM_SETSEL, 1, 1);
    SendMessageW(host.handle(), EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"新"));
    if (host.synchronize_change() != markdownmay::ErrorCode::ok ||
        session.snapshot().source != "**粗新体** 和 [链接](local.md)") return 5;
    GetWindowTextW(host.handle(), visible.data(), static_cast<int>(visible.size()));
    if (std::wstring_view(visible.data()) != L"粗新体 和 链接") return 6;
    CHARRANGE caret{};
    SendMessageW(host.handle(), EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&caret));
    if (caret.cpMin != 2 || caret.cpMax != 2) return 8;
    if (host.undo() != markdownmay::ErrorCode::ok ||
        session.snapshot().source != "**粗体** 和 [链接](local.md)") return 7;
    SendMessageW(host.handle(), EM_SETSEL, 6, 6);
    SendMessageW(host.handle(), EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"新"));
    if (host.synchronize_change() != markdownmay::ErrorCode::ok ||
        session.snapshot().source != "**粗体** 和 [链新接](local.md)") return 11;
    if (host.undo() != markdownmay::ErrorCode::ok ||
        session.snapshot().source != "**粗体** 和 [链接](local.md)") return 12;
    SendMessageW(host.handle(), EM_SETSEL, 0, 2);
    if (host.toggle_inline(markdownmay::editor::InlineFormat::bold) !=
            markdownmay::ErrorCode::ok ||
        session.snapshot().source != "粗体 和 [链接](local.md)") return 9;
    SendMessageW(host.handle(), EM_SETSEL, 0, 2);
    if (host.toggle_inline(markdownmay::editor::InlineFormat::bold) !=
            markdownmay::ErrorCode::ok ||
        session.snapshot().source != "**粗体** 和 [链接](local.md)") return 10;
    if (!host.inline_active(markdownmay::editor::InlineFormat::bold)) return 13;
    if (host.toggle_inline(markdownmay::editor::InlineFormat::bold) !=
            markdownmay::ErrorCode::ok ||
        session.snapshot().source != "粗体 和 [链接](local.md)" ||
        host.inline_active(markdownmay::editor::InlineFormat::bold)) return 14;

    SendMessageW(host.handle(), EM_SETSEL, 0, 2);
    if (host.toggle_inline(markdownmay::editor::InlineFormat::italic) !=
            markdownmay::ErrorCode::ok ||
        session.snapshot().source != "*粗体* 和 [链接](local.md)" ||
        !host.inline_active(markdownmay::editor::InlineFormat::italic)) return 15;
    if (host.toggle_inline(markdownmay::editor::InlineFormat::italic) !=
            markdownmay::ErrorCode::ok ||
        session.snapshot().source != "粗体 和 [链接](local.md)") return 16;

    SendMessageW(host.handle(), EM_SETSEL, 0, 2);
    if (host.toggle_inline(markdownmay::editor::InlineFormat::strike) !=
            markdownmay::ErrorCode::ok ||
        session.snapshot().source != "~~粗体~~ 和 [链接](local.md)" ||
        !host.inline_active(markdownmay::editor::InlineFormat::strike)) return 17;
    if (host.toggle_inline(markdownmay::editor::InlineFormat::strike) !=
            markdownmay::ErrorCode::ok ||
        session.snapshot().source != "粗体 和 [链接](local.md)") return 18;
    if (session.reload("正文 `code` 结尾") != markdownmay::ErrorCode::ok ||
        host.project() != markdownmay::ErrorCode::ok) return 21;
    FINDTEXTEXW find_code{{0, -1}, const_cast<wchar_t*>(L"code"), {}};
    if (SendMessageW(host.handle(), EM_FINDTEXTEXW, FR_DOWN,
            reinterpret_cast<LPARAM>(&find_code)) < 0) return 22;
    SendMessageW(host.handle(), EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&find_code.chrgText));
    CHARFORMAT2W code_format{};
    code_format.cbSize = sizeof(code_format);
    code_format.dwMask = CFM_BACKCOLOR | CFM_SIZE;
    SendMessageW(host.handle(), EM_GETCHARFORMAT, SCF_SELECTION,
        reinterpret_cast<LPARAM>(&code_format));
    if (code_format.crBackColor != RGB(255, 232, 238) || code_format.yHeight != 200)
        return 23;

    std::string long_source;
    for (int index = 0; index < 70; ++index)
        long_source += "第" + std::to_string(index) + "行普通正文。\n\n";
    long_source += "目标行需要转换成行内代码并保持当前视口。";
    markdownmay::document::DocumentSession viewport_session(long_source);
    markdownmay::editor::RichEditHost viewport_host(viewport_session);
    if (viewport_host.create(parent, {0, 0, 400, 180}) != markdownmay::ErrorCode::ok) return 24;
    SendMessageW(viewport_host.handle(), EM_LINESCROLL, 0, 120);
    FINDTEXTEXW find_target{{0, -1}, const_cast<wchar_t*>(L"目标行需要转换成行内代码"), {}};
    if (SendMessageW(viewport_host.handle(), EM_FINDTEXTEXW, FR_DOWN,
            reinterpret_cast<LPARAM>(&find_target)) < 0) return 25;
    SendMessageW(viewport_host.handle(), EM_EXSETSEL, 0,
        reinterpret_cast<LPARAM>(&find_target.chrgText));
    POINT scroll_before{};
    SendMessageW(viewport_host.handle(), EM_GETSCROLLPOS, 0,
        reinterpret_cast<LPARAM>(&scroll_before));
    if (viewport_host.toggle_inline(markdownmay::editor::InlineFormat::code) !=
            markdownmay::ErrorCode::ok) return 26;
    POINT scroll_after{};
    SendMessageW(viewport_host.handle(), EM_GETSCROLLPOS, 0,
        reinterpret_cast<LPARAM>(&scroll_after));
    if (std::abs(scroll_after.y - scroll_before.y) > 2 ||
        viewport_session.snapshot().source.find("`目标行需要转换成行内代码`") ==
            std::string::npos) return 27;

    const std::string wrapping_text(90, 'W');
    markdownmay::document::DocumentSession wrapping_session("`" + wrapping_text + "`");
    markdownmay::editor::RichEditHost wrapping_host(wrapping_session);
    if (wrapping_host.create(parent, {0, 0, 240, 180}) != markdownmay::ErrorCode::ok) return 28;
    POINT first_code{};
    POINT last_code{};
    SendMessageW(wrapping_host.handle(), EM_POSFROMCHAR,
        reinterpret_cast<WPARAM>(&first_code), 0);
    SendMessageW(wrapping_host.handle(), EM_POSFROMCHAR,
        reinterpret_cast<WPARAM>(&last_code), wrapping_text.size() - 1);
    if (last_code.y <= first_code.y) return 29;
    const auto screen = GetDC(wrapping_host.handle());
    const auto memory = CreateCompatibleDC(screen);
    const auto bitmap = CreateCompatibleBitmap(screen, 240, 180);
    const auto old_bitmap = SelectObject(memory, bitmap);
    RECT canvas{0, 0, 240, 180};
    FillRect(memory, &canvas, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    wrapping_host.draw_inline_code_frames(memory);
    int first_ink_y = -1;
    int last_ink_y = -1;
    for (int y = 0; y < 180; ++y)
        for (int x = 0; x < 240; ++x)
            if (GetPixel(memory, x, y) == RGB(232, 168, 184)) {
                if (first_ink_y < 0) first_ink_y = y;
                last_ink_y = y;
            }
    SelectObject(memory, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(wrapping_host.handle(), screen);
    if (first_ink_y < 0 || last_ink_y - first_ink_y < 12) return 30;
    if (session.reload("# **粗体** 和 **孤立 <b>标签</b>") != markdownmay::ErrorCode::ok ||
        host.project() != markdownmay::ErrorCode::ok) return 19;
    SendMessageW(host.handle(), EM_SETSEL, 1, 1);
    if (host.execute(markdownmay::editor::EditorCommand::clear_format) !=
            markdownmay::ErrorCode::ok ||
        session.snapshot().source != "粗体 和 **孤立 标签") return 20;
    DestroyWindow(parent);
    return 0;
}
