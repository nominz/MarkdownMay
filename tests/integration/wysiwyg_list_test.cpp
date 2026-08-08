#include "markdownmay/editor/richedit_host.hpp"

#include <commdlg.h>
#include <richedit.h>

#include <array>

namespace {
CHARRANGE Find(HWND handle, wchar_t* text) {
    FINDTEXTEXW find{{0, -1}, text, {}};
    SendMessageW(handle, EM_FINDTEXTEXW, FR_DOWN, reinterpret_cast<LPARAM>(&find));
    return find.chrgText;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED,
                                  0, 0, 520, 420, nullptr, nullptr, instance, nullptr);
    if (!parent) return 1;
    const std::string source =
        "- first\n- [x] done\n\n3. third\n4. fourth\n\n- parent\n    - child";
    markdownmay::document::DocumentSession session(source);
    markdownmay::editor::RichEditHost host(session);
    RECT bounds{0, 0, 520, 420};
    if (host.create(parent, bounds) != markdownmay::ErrorCode::ok) return 2;
    std::array<wchar_t, 256> visible{};
    GetWindowTextW(host.handle(), visible.data(), static_cast<int>(visible.size()));
    const std::wstring text(visible.data());
    if (text.find(L"• first") == std::wstring::npos ||
        text.find(L"☑ done") == std::wstring::npos ||
        text.find(L"3. third") == std::wstring::npos ||
        text.find(L"4. fourth") == std::wstring::npos ||
        text.find(L"• child") == std::wstring::npos ||
        text.find(L"[x]") != std::wstring::npos) return 3;

    auto first = Find(host.handle(), const_cast<wchar_t*>(L"first"));
    const auto inside_first = first.cpMin + 1;
    SendMessageW(host.handle(), EM_SETSEL, inside_first, inside_first);
    SendMessageW(host.handle(), EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"新"));
    if (host.synchronize_change() != markdownmay::ErrorCode::ok ||
        session.snapshot().source.find("- f新irst") == std::string::npos) return 4;
    if (host.undo() != markdownmay::ErrorCode::ok || session.snapshot().source != source) return 5;

    first = Find(host.handle(), const_cast<wchar_t*>(L"first"));
    SendMessageW(host.handle(), EM_SETSEL, first.cpMax, first.cpMax);
    SendMessageW(host.handle(), EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"\r\n"));
    if (host.synchronize_change() != markdownmay::ErrorCode::ok ||
        session.snapshot().source.find("- first\n- \n- [x] done") == std::string::npos) return 12;
    if (host.undo() != markdownmay::ErrorCode::ok || session.snapshot().source != source) return 13;

    auto done = Find(host.handle(), const_cast<wchar_t*>(L"done"));
    SendMessageW(host.handle(), EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&done));
    if (host.toggle_task_checked() != markdownmay::ErrorCode::ok ||
        session.snapshot().source.find("- [ ] done") == std::string::npos) return 6;
    if (host.undo() != markdownmay::ErrorCode::ok || session.snapshot().source != source) return 7;

    auto child = Find(host.handle(), const_cast<wchar_t*>(L"child"));
    SendMessageW(host.handle(), EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&child));
    if (host.outdent_list() != markdownmay::ErrorCode::ok ||
        session.snapshot().source.find("\n- child") == std::string::npos) return 8;
    child = Find(host.handle(), const_cast<wchar_t*>(L"child"));
    SendMessageW(host.handle(), EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&child));
    if (host.indent_list() != markdownmay::ErrorCode::ok ||
        session.snapshot().source.find("\n    - child") == std::string::npos) return 9;

    markdownmay::document::DocumentSession plain("alpha\nbeta");
    markdownmay::editor::RichEditHost commands(plain);
    if (commands.create(parent, bounds) != markdownmay::ErrorCode::ok) return 10;
    SendMessageW(commands.handle(), EM_SETSEL, 0, -1);
    if (commands.toggle_ordered_list(5) != markdownmay::ErrorCode::ok ||
        plain.snapshot().source != "5. alpha\n6. beta") return 11;

    markdownmay::document::DocumentSession live("paragraph\n\n");
    markdownmay::editor::RichEditHost live_host(live);
    if (live_host.create(parent, bounds) != markdownmay::ErrorCode::ok) return 14;
    SendMessageW(live_host.handle(), EM_SETSEL,
        GetWindowTextLengthW(live_host.handle()), GetWindowTextLengthW(live_host.handle()));
    SendMessageW(live_host.handle(), WM_CHAR, L'*', 0);
    if (live_host.synchronize_change() != markdownmay::ErrorCode::ok ||
        live.snapshot().source != "paragraph\n\n*") return 15;
    std::array<wchar_t, 64> live_visible{};
    GetWindowTextW(live_host.handle(), live_visible.data(),
        static_cast<int>(live_visible.size()));
    if (std::wstring(live_visible.data()).find(L'*') == std::wstring::npos ||
        std::wstring(live_visible.data()).find(L'\x2022') != std::wstring::npos) return 16;
    SendMessageW(live_host.handle(), WM_CHAR, L' ', 0);
    if (live_host.synchronize_change() != markdownmay::ErrorCode::ok ||
        live.snapshot().source != "paragraph\n\n* ") return 17;
    SendMessageW(live_host.handle(), WM_CHAR, L'在', 0);
    if (live_host.synchronize_change() != markdownmay::ErrorCode::ok ||
        live.snapshot().source != "paragraph\n\n* 在") return 18;
    live_visible.fill(L'\0');
    GetWindowTextW(live_host.handle(), live_visible.data(),
        static_cast<int>(live_visible.size()));
    if (std::wstring(live_visible.data()).find(L"\x2022 在") == std::wstring::npos)
        return 19;
    SendMessageW(live_host.handle(), EM_REPLACESEL, TRUE,
        reinterpret_cast<LPARAM>(L"\r\n"));
    if (live_host.synchronize_change() != markdownmay::ErrorCode::ok) return 20;
    if (live.snapshot().source != "paragraph\n\n* 在\n* ") return 26;

    markdownmay::document::DocumentSession ordered("paragraph\n\n");
    markdownmay::editor::RichEditHost ordered_host(ordered);
    if (ordered_host.create(parent, bounds) != markdownmay::ErrorCode::ok) return 21;
    SendMessageW(ordered_host.handle(), EM_SETSEL,
        GetWindowTextLengthW(ordered_host.handle()),
        GetWindowTextLengthW(ordered_host.handle()));
    SendMessageW(ordered_host.handle(), WM_CHAR, L'1', 1);
    SendMessageW(ordered_host.handle(), WM_CHAR, L'.', 1);
    if (ordered_host.synchronize_change() != markdownmay::ErrorCode::ok ||
        ordered.snapshot().source != "paragraph\n\n1.") return 22;
    SendMessageW(ordered_host.handle(), WM_CHAR, L' ', 1);
    if (ordered_host.synchronize_change() != markdownmay::ErrorCode::ok) return 23;
    if (ordered.snapshot().source != "paragraph\n\n1. ") return 27;
    SendMessageW(ordered_host.handle(), WM_CHAR, L'甲', 1);
    if (ordered_host.synchronize_change() != markdownmay::ErrorCode::ok ||
        ordered.snapshot().source != "paragraph\n\n1. 甲") return 24;
    SendMessageW(ordered_host.handle(), EM_REPLACESEL, TRUE,
        reinterpret_cast<LPARAM>(L"\r\n"));
    if (ordered_host.synchronize_change() != markdownmay::ErrorCode::ok ||
        ordered.snapshot().source != "paragraph\n\n1. 甲\n2. ") return 25;

    markdownmay::document::DocumentSession middle("before\n\n* 在\n\nafter");
    markdownmay::editor::RichEditHost middle_host(middle);
    if (middle_host.create(parent, bounds) != markdownmay::ErrorCode::ok) return 28;
    FINDTEXTEXW find_item{{0, -1}, const_cast<wchar_t*>(L"在"), {}};
    if (SendMessageW(middle_host.handle(), EM_FINDTEXTEXW, FR_DOWN,
        reinterpret_cast<LPARAM>(&find_item)) < 0) return 29;
    SendMessageW(middle_host.handle(), EM_SETSEL,
        find_item.chrgText.cpMax, find_item.chrgText.cpMax);
    SendMessageW(middle_host.handle(), EM_REPLACESEL, TRUE,
        reinterpret_cast<LPARAM>(L"\r\n"));
    if (middle_host.synchronize_change() != markdownmay::ErrorCode::ok ||
        middle.snapshot().source != "before\n\n* 在\n* \n\nafter") return 30;
    const auto middle_selection = middle_host.source_selection();
    if (!middle_selection.is_ok() || middle_selection.value().caret != 16) return 31;
    DestroyWindow(parent);
    return 0;
}
