#include "markdownmay/editor/richedit_host.hpp"

#include <richedit.h>
#include <commdlg.h>

#include <array>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED,
                                  0, 0, 500, 400, nullptr, nullptr, instance, nullptr);
    if (!parent) return 1;
    const std::string source =
        "# 标题\n\n> 引用\n\n```cpp\n代码\n```\n\n---\n";
    markdownmay::document::DocumentSession session(source);
    markdownmay::editor::RichEditHost host(session);
    RECT bounds{0, 0, 500, 400};
    if (host.create(parent, bounds) != markdownmay::ErrorCode::ok) return 2;
    std::array<wchar_t, 256> visible{};
    GetWindowTextW(host.handle(), visible.data(), static_cast<int>(visible.size()));
    const std::wstring text(visible.data());
    if (text.find(L"标题") == std::wstring::npos ||
        text.find(L"引用") == std::wstring::npos ||
        text.find(L"代码") == std::wstring::npos ||
        text.find(L"────────") == std::wstring::npos ||
        text.find(L"```") != std::wstring::npos ||
        text.find(L"> ") != std::wstring::npos || text.find(L"# ") != std::wstring::npos) return 3;
    FINDTEXTEXW find_quote{{0, -1}, const_cast<wchar_t*>(L"引用"), {}};
    if (SendMessageW(host.handle(), EM_FINDTEXTEXW, FR_DOWN,
        reinterpret_cast<LPARAM>(&find_quote)) < 0) return 15;
    SendMessageW(host.handle(), EM_SETSEL, find_quote.chrgText.cpMin,
        find_quote.chrgText.cpMin);
    if (!host.block_active(markdownmay::editor::BlockFormat::quote)) return 16;

    SendMessageW(host.handle(), EM_SETSEL, 0, 2);
    CHARFORMAT2W heading{};
    heading.cbSize = sizeof(heading);
    heading.dwMask = CFM_BOLD | CFM_SIZE;
    SendMessageW(host.handle(), EM_GETCHARFORMAT, SCF_SELECTION,
                 reinterpret_cast<LPARAM>(&heading));
    if ((heading.dwEffects & CFE_BOLD) == 0 || heading.yHeight != 420) return 4;
    host.set_render_style(markdownmay::editor::RenderStyle::song_ying);
    SendMessageW(host.handle(), EM_SETSEL, 0, 2);
    SendMessageW(host.handle(), EM_GETCHARFORMAT, SCF_SELECTION,
                 reinterpret_cast<LPARAM>(&heading));
    if (heading.yHeight != 400) return 40;
    CHARFORMAT2W body{};
    body.cbSize = sizeof(body);
    body.dwMask = CFM_FACE | CFM_SIZE;
    SendMessageW(host.handle(), EM_SETSEL, 3, 3);
    SendMessageW(host.handle(), EM_GETCHARFORMAT, SCF_SELECTION,
                 reinterpret_cast<LPARAM>(&body));
    const std::wstring body_face(body.szFaceName);
    if (body.yHeight != 230 || (body_face != L"SimSun" && body_face != L"宋体")) return 41;

    FINDTEXTEXW find_code{{0, -1}, const_cast<wchar_t*>(L"代码"), {}};
    if (SendMessageW(host.handle(), EM_FINDTEXTEXW, FR_DOWN,
                     reinterpret_cast<LPARAM>(&find_code)) < 0) return 50;
    const auto code_position = find_code.chrgText.cpMin + 1;
    SendMessageW(host.handle(), EM_SETSEL, code_position, code_position);
    CHARRANGE selected_code{};
    SendMessageW(host.handle(), EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selected_code));
    if (selected_code.cpMin != code_position || selected_code.cpMax != code_position) return 51;
    SendMessageW(host.handle(), EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"新"));
    if (host.synchronize_change() != markdownmay::ErrorCode::ok ||
        session.snapshot().source.find("代新码") == std::string::npos) return 5;
    if (host.undo() != markdownmay::ErrorCode::ok || session.snapshot().source != source) return 6;

    markdownmay::document::DocumentSession plain("标题");
    markdownmay::editor::RichEditHost command_host(plain);
    if (command_host.create(parent, bounds) != markdownmay::ErrorCode::ok) return 7;
    SendMessageW(command_host.handle(), EM_SETSEL, 0, 2);
    if (command_host.set_heading(3) != markdownmay::ErrorCode::ok ||
        plain.snapshot().source != "### 标题") return 8;
    if (command_host.toggle_quote() != markdownmay::ErrorCode::ok) return 9;
    if (command_host.undo() != markdownmay::ErrorCode::ok ||
        plain.snapshot().source != "### 标题") return 10;
    markdownmay::document::DocumentSession empty("");
    markdownmay::editor::RichEditHost empty_host(empty);
    if (empty_host.create(parent, bounds) != markdownmay::ErrorCode::ok) return 11;
    if (empty_host.set_heading(1) != markdownmay::ErrorCode::ok ||
        empty.snapshot().source != "# ") return 12;
    const auto empty_selection = empty_host.source_selection();
    if (!empty_selection.is_ok() || empty_selection.value().caret != 2 ||
        empty_selection.value().anchor != 2) return 13;
    SendMessageW(empty_host.handle(), EM_REPLACESEL, TRUE,
        reinterpret_cast<LPARAM>(L"在"));
    if (empty_host.synchronize_change() != markdownmay::ErrorCode::ok ||
        empty.snapshot().source != "# 在") return 14;

    markdownmay::document::DocumentSession boundary("## 标题\n\n正文\n\n后段");
    markdownmay::editor::RichEditHost boundary_host(boundary);
    if (boundary_host.create(parent, bounds) != markdownmay::ErrorCode::ok) return 52;
    FINDTEXTEXW find_body{{0, -1}, const_cast<wchar_t*>(L"正文"), {}};
    if (SendMessageW(boundary_host.handle(), EM_FINDTEXTEXW, FR_DOWN,
            reinterpret_cast<LPARAM>(&find_body)) < 0) return 53;
    SendMessageW(boundary_host.handle(), EM_SETSEL, find_body.chrgText.cpMin,
        find_body.chrgText.cpMax);
    if (boundary_host.toggle_code_block("python") != markdownmay::ErrorCode::ok) return 54;
    if (boundary.snapshot().source != "## 标题\n\n```python\n正文\n```\n\n后段") return 57;
    find_body.chrg = {0, -1};
    if (SendMessageW(boundary_host.handle(), EM_FINDTEXTEXW, FR_DOWN,
            reinterpret_cast<LPARAM>(&find_body)) < 0) return 55;
    SendMessageW(boundary_host.handle(), EM_SETSEL, find_body.chrgText.cpMin,
        find_body.chrgText.cpMax);
    if (!boundary_host.block_active(markdownmay::editor::BlockFormat::code_block)) return 55;
    DestroyWindow(parent);
    return 0;
}
