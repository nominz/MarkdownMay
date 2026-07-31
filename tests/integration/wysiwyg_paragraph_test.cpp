#include "markdownmay/editor/richedit_host.hpp"

#include <richedit.h>

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
    DestroyWindow(parent);
    return 0;
}
