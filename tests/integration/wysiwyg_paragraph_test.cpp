#include "markdownmay/editor/richedit_host.hpp"

#include <richedit.h>
#include <commdlg.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED,
                                  0, 0, 400, 300, nullptr, nullptr, instance, nullptr);
    if (!parent) return 1;
    markdownmay::document::DocumentSession session("第一段\r\n\r\nsecond");
    markdownmay::editor::RichEditHost host(session);
    RECT bounds{0, 0, 400, 300};
    if (host.create(parent, bounds) != markdownmay::ErrorCode::ok) return 2;

    SendMessageW(host.handle(), EM_SETSEL, 0, 0);
    SendMessageW(host.handle(), EM_REPLACESEL, TRUE,
                 reinterpret_cast<LPARAM>(L"新"));
    if (host.synchronize_change() != markdownmay::ErrorCode::ok ||
        session.snapshot().source != "新第一段\r\n\r\nsecond") return 3;

    SendMessageW(host.handle(), EM_SETSEL, 0, 1);
    SendMessageW(host.handle(), EM_REPLACESEL, TRUE,
                 reinterpret_cast<LPARAM>(L""));
    if (host.synchronize_change() != markdownmay::ErrorCode::ok ||
        session.snapshot().source != "第一段\r\n\r\nsecond") return 4;
    if (host.undo() != markdownmay::ErrorCode::ok ||
        session.snapshot().source != "新第一段\r\n\r\nsecond") return 5;
    if (host.redo() != markdownmay::ErrorCode::ok ||
        session.snapshot().source != "第一段\r\n\r\nsecond") return 6;

    markdownmay::document::DocumentSession middle("前段\n\n# 后段");
    markdownmay::editor::RichEditHost middle_host(middle);
    if (middle_host.create(parent, bounds) != markdownmay::ErrorCode::ok) return 7;
    FINDTEXTEXW find_before{{0, -1}, const_cast<wchar_t*>(L"前段"), {}};
    if (SendMessageW(middle_host.handle(), EM_FINDTEXTEXW, FR_DOWN,
        reinterpret_cast<LPARAM>(&find_before)) < 0) return 8;
    const auto after_before = find_before.chrgText.cpMax;
    SendMessageW(middle_host.handle(), EM_SETSEL, after_before, after_before);
    SendMessageW(middle_host.handle(), EM_REPLACESEL, TRUE,
        reinterpret_cast<LPARAM>(L"\r\n"));
    if (middle_host.synchronize_change() != markdownmay::ErrorCode::ok ||
        middle.snapshot().source != "前段\n\n\n# 后段") return 9;
    const auto middle_selection = middle_host.source_selection();
    if (!middle_selection.is_ok() || middle_selection.value().caret != 7) return 10;

    std::string long_source;
    for (int paragraph = 0; paragraph < 100; ++paragraph)
        long_source += "普通正文 " + std::to_string(paragraph) + " " +
            std::string(140, 'a') + "\n\n";
    markdownmay::document::DocumentSession performance(long_source);
    markdownmay::editor::RichEditHost performance_host(performance);
    if (performance_host.create(parent, bounds) != markdownmay::ErrorCode::ok) return 11;
    SendMessageW(performance_host.handle(), EM_SETSEL,
        GetWindowTextLengthW(performance_host.handle()),
        GetWindowTextLengthW(performance_host.handle()));
    SendMessageW(performance_host.handle(), WM_CHAR, L'在', 1);
    const auto started = GetTickCount64();
    if (performance_host.synchronize_change() != markdownmay::ErrorCode::ok) return 12;
    const auto elapsed = GetTickCount64() - started;
    if (!performance.snapshot().source.ends_with("在") || elapsed > 750) return 13;
    DestroyWindow(parent);
    return 0;
}
