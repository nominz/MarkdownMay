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

    auto first_paragraph = Find(host.handle(), const_cast<wchar_t*>(L"first"));
    SendMessageW(host.handle(), EM_EXSETSEL, 0,
        reinterpret_cast<LPARAM>(&first_paragraph));
    PARAFORMAT2 hanging{};
    hanging.cbSize = sizeof(hanging);
    hanging.dwMask = PFM_STARTINDENT | PFM_OFFSET;
    SendMessageW(host.handle(), EM_GETPARAFORMAT, 0,
        reinterpret_cast<LPARAM>(&hanging));
    if ((hanging.dwMask & (PFM_STARTINDENT | PFM_OFFSET)) !=
            (PFM_STARTINDENT | PFM_OFFSET) || hanging.dxOffset <= 0 ||
        hanging.dxStartIndent < 1440 ||
        hanging.dxStartIndent + hanging.dxOffset <= hanging.dxStartIndent) return 37;

    POINT first_point{};
    SendMessageW(host.handle(), EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&first_point),
        first_paragraph.cpMin);
    SendMessageW(host.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
        MAKELPARAM(1, first_point.y + 2));
    SendMessageW(host.handle(), WM_LBUTTONUP, 0, MAKELPARAM(1, first_point.y + 2));
    CHARRANGE whole_line{};
    SendMessageW(host.handle(), EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&whole_line));
    if (whole_line.cpMax <= whole_line.cpMin || whole_line.cpMax < first_paragraph.cpMax)
        return 38;

    POINT marker_point{};
    SendMessageW(host.handle(), EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&marker_point), 0);
    SendMessageW(host.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
        MAKELPARAM(marker_point.x + 1, marker_point.y + 2));
    SendMessageW(host.handle(), WM_LBUTTONUP, 0,
        MAKELPARAM(marker_point.x + 1, marker_point.y + 2));
    CHARRANGE protected_marker{};
    SendMessageW(host.handle(), EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&protected_marker));
    if (protected_marker.cpMin != protected_marker.cpMax ||
        protected_marker.cpMin < first_paragraph.cpMin) return 39;

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

    markdownmay::document::DocumentSession sequence("before\n\n# after");
    markdownmay::editor::RichEditHost sequence_host(sequence);
    if (sequence_host.create(parent, bounds) != markdownmay::ErrorCode::ok) return 32;
    FINDTEXTEXW find_before{{0, -1}, const_cast<wchar_t*>(L"before"), {}};
    if (SendMessageW(sequence_host.handle(), EM_FINDTEXTEXW, FR_DOWN,
        reinterpret_cast<LPARAM>(&find_before)) < 0) return 33;
    SendMessageW(sequence_host.handle(), EM_SETSEL,
        find_before.chrgText.cpMax, find_before.chrgText.cpMax);
    const auto type_and_sync = [&](wchar_t value) {
        if (value == L'\r') {
            SendMessageW(sequence_host.handle(), EM_REPLACESEL, TRUE,
                reinterpret_cast<LPARAM>(L"\r\n"));
        } else {
            SendMessageW(sequence_host.handle(), WM_CHAR, value, 1);
        }
        return sequence_host.synchronize_change();
    };
    if (type_and_sync(L'\r') != markdownmay::ErrorCode::ok ||
        type_and_sync(L'*') != markdownmay::ErrorCode::ok ||
        type_and_sync(L' ') != markdownmay::ErrorCode::ok ||
        type_and_sync(L'在') != markdownmay::ErrorCode::ok ||
        type_and_sync(L'\r') != markdownmay::ErrorCode::ok ||
        type_and_sync(L'\r') != markdownmay::ErrorCode::ok ||
        type_and_sync(L'1') != markdownmay::ErrorCode::ok) return 34;
    if (sequence.snapshot().source.find("* 在\n1\n\n# after") ==
        std::string::npos) return 35;
    std::array<wchar_t, 128> sequence_visible{};
    GetWindowTextW(sequence_host.handle(), sequence_visible.data(),
        static_cast<int>(sequence_visible.size()));
    const std::wstring sequence_text(sequence_visible.data());
    if (sequence_text.find(L"1. ") != std::wstring::npos) return 36;
    DestroyWindow(parent);
    return 0;
}
