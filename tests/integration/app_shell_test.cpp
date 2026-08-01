#include "markdownmay/app/command_dispatcher.hpp"
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
    bool exit_requested{};
    app::CommandDispatcher dispatcher(window.document_window(),
        [&exit_requested] { exit_requested = true; });
    window.set_command_callbacks(
        [&dispatcher](app::CommandId command) { return dispatcher.query(command); },
        [&dispatcher](app::CommandId command) {
            static_cast<void>(dispatcher.execute(command));
        });
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
    static_assert(static_cast<std::uint16_t>(app::CommandId::file_new) == 100);
    static_assert(static_cast<std::uint16_t>(app::CommandId::edit_undo) == 200);
    static_assert(static_cast<std::uint16_t>(app::CommandId::format_bold) == 300);
    static_assert(static_cast<std::uint16_t>(app::CommandId::view_render) == 400);
    if (!GetMenu(window.handle()) ||
        dispatcher.query(app::CommandId::file_open).enabled ||
        !dispatcher.query(app::CommandId::view_render).checked ||
        (GetMenuState(GetMenu(window.handle()),
             static_cast<UINT>(app::CommandId::file_open), MF_BYCOMMAND) &
             (MF_DISABLED | MF_GRAYED)) == 0) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 4;
    }
    if (dispatcher.execute(app::CommandId::view_source) != ErrorCode::ok ||
        !dispatcher.query(app::CommandId::view_source).checked ||
        dispatcher.query(app::CommandId::format_bold).enabled) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 5;
    }
    SendMessageW(window.handle(), WM_COMMAND,
        static_cast<WPARAM>(app::CommandId::view_split), 0);
    if (!dispatcher.query(app::CommandId::view_split).checked ||
        dispatcher.execute(app::CommandId::file_exit) != ErrorCode::ok ||
        !exit_requested) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 6;
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
        return 7;
    }
    DestroyWindow(window.handle());
    CoUninitialize();
    return 0;
}
