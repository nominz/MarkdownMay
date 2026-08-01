#include "markdownmay/editor/split_view.hpp"

#include <Scintilla.h>
#include <richedit.h>

#include <string>

namespace {
std::wstring ReadWindow(HWND window) {
    const auto length = GetWindowTextLengthW(window);
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(window, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(length));
    return value;
}

bool PumpUntil(std::uint64_t milliseconds, const auto& predicate) {
    const auto deadline = GetTickCount64() + milliseconds;
    MSG message{};
    while (GetTickCount64() < deadline) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (predicate()) return true;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
    }
    return predicate();
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    using namespace markdownmay;
    std::string initial = "# 标题\n\n正文\n";
    for (int index = 0; index < 80; ++index)
        initial += "\n第 " + std::to_string(index + 1) + " 行内容\n";
    document::DocumentSession session(initial);
    HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED,
        0, 0, 1000, 600, nullptr, nullptr, instance, nullptr);
    if (!parent) return 1;
    editor::SplitView split(session);
    RECT bounds{0, 0, 1000, 600};
    if (split.create(parent, bounds) != ErrorCode::ok) return 2;
    const auto left = split.source_view().handle();
    const auto right = split.render_view().handle();
    if (!left || !right || ReadWindow(right).find(L"标题") == std::wstring::npos ||
        ReadWindow(right).find(L"正文") == std::wstring::npos) return 3;

    const auto before_read_only = ReadWindow(right);
    if ((SendMessageW(right, EM_GETOPTIONS, 0, 0) & ECO_READONLY) == 0) return 4;
    SendMessageW(right, EM_SETSEL, 0, 0);
    SendMessageW(right, WM_CHAR, L'X', 0);
    if (ReadWindow(right) != before_read_only) return 4;

    const auto body = static_cast<WPARAM>(initial.find("正文"));
    SendMessageW(left, SCI_SETSEL, body, body);
    SendMessageW(left, SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>("新"));
    if (!PumpUntil(1200, [&] {
            return session.snapshot().source.find("新正文") != std::string::npos &&
                ReadWindow(right).find(L"新正文") != std::wstring::npos;
        })) return 5;

    std::string invalid = "bad\n";
    invalid.push_back(static_cast<char>(0xff));
    SendMessageW(left, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(invalid.c_str()));
    if (split.source_view().synchronize_now() != ErrorCode::file_encoding_invalid) return 6;
    const auto error_text = ReadWindow(right);
    if (error_text.find(L"当前源码无法渲染") == std::wstring::npos ||
        error_text.find(L"标题") != std::wstring::npos) return 7;

    const auto valid = session.snapshot().source;
    SendMessageW(left, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(valid.c_str()));
    if (split.source_view().synchronize_now() != ErrorCode::ok ||
        ReadWindow(right).find(L"新正文") == std::wstring::npos) return 8;

    SendMessageW(left, SCI_GOTOLINE, 60, 0);
    SendMessageW(left, SCI_LINESCROLL, 0, 60);
    if (!PumpUntil(500, [&] {
            return SendMessageW(right, EM_GETFIRSTVISIBLELINE, 0, 0) > 0;
        })) return 9;
    DestroyWindow(parent);
    return 0;
}
