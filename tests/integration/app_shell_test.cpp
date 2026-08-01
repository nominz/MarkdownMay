#include "markdownmay/ui/main_window.hpp"

#include <objbase.h>
#include <windows.h>

#include <string>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    using namespace markdownmay;
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return 1;
    document::DocumentSession session("");
    ui::MainWindow window(session);
    if (window.create(instance, SW_HIDE) != ErrorCode::ok || !window.handle()) {
        CoUninitialize();
        return 2;
    }
    const auto length = GetWindowTextLengthW(window.handle());
    std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(window.handle(), title.data(), length + 1);
    title.resize(static_cast<std::size_t>(length));
    if (title != L"马冬梅 - Markdown May" ||
        window.document_window().modes().mode() != editor::ViewMode::render ||
        !window.document_window().modes().render_view().handle()) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 3;
    }
    SetWindowPos(window.handle(), nullptr, 0, 0, 760, 520,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT client{};
    RECT document{};
    GetClientRect(window.handle(), &client);
    GetWindowRect(window.document_window().handle(), &document);
    POINT corners[2]{{document.left, document.top}, {document.right, document.bottom}};
    MapWindowPoints(nullptr, window.handle(), corners, 2);
    if (corners[0].x != 0 || corners[0].y != 0 ||
        corners[1].x != client.right || corners[1].y != client.bottom) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 4;
    }
    DestroyWindow(window.handle());
    CoUninitialize();
    return 0;
}
