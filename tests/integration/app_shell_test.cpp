#include "markdownmay/app/command_dispatcher.hpp"
#include "markdownmay/ui/main_window.hpp"

#include <objbase.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windows.h>

#include <string>
#include <chrono>
#include <filesystem>
#include <cstring>
#include <cstddef>

namespace {
struct TemporaryDirectory final {
    std::filesystem::path path;
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    using namespace markdownmay;
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return 1;
    document::DocumentSession session("");
    ui::MainWindow window(session);
    bool exit_requested{};
    app::CommandDispatcher dispatcher(window.document_window(),
        [&exit_requested] { exit_requested = true; }, {
            [&session] { return !session.is_dirty(); },
            [&window] { return window.document_window().new_document(); },
            [] { return ErrorCode::ok; },
            [&window] { return window.document_window().save_document(); },
            [] { return ErrorCode::ok; },
        });
    window.set_command_callbacks(
        [&dispatcher](app::CommandId command) { return dispatcher.query(command); },
        [&dispatcher](app::CommandId command) {
            static_cast<void>(dispatcher.execute(command));
        });
    std::filesystem::path dropped_path;
    window.set_drop_callback([&dropped_path](const std::filesystem::path& path) {
        dropped_path = path;
    });
    if (window.create(instance, SW_HIDE) != ErrorCode::ok || !window.handle()) {
        CoUninitialize();
        return 2;
    }
    const auto length = GetWindowTextLengthW(window.handle());
    std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(window.handle(), title.data(), length + 1);
    title.resize(static_cast<std::size_t>(length));
    if (title != L"无标题 - 马冬梅" ||
        window.document_window().modes().mode() != editor::ViewMode::render ||
        !window.document_window().modes().render_view().handle() ||
        !window.toolbar() || !window.toolbar()->handle() ||
        !window.status_bar().handle()) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 3;
    }
    static_assert(static_cast<std::uint16_t>(app::CommandId::file_new) == 100);
    static_assert(static_cast<std::uint16_t>(app::CommandId::edit_undo) == 200);
    static_assert(static_cast<std::uint16_t>(app::CommandId::format_bold) == 300);
    static_assert(static_cast<std::uint16_t>(app::CommandId::view_render) == 400);
    if (!GetMenu(window.handle()) ||
        !dispatcher.query(app::CommandId::file_open).enabled ||
        !dispatcher.query(app::CommandId::view_render).checked ||
        (GetMenuState(GetMenu(window.handle()),
             static_cast<UINT>(app::CommandId::file_open), MF_BYCOMMAND) &
             (MF_DISABLED | MF_GRAYED)) != 0) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 4;
    }
    if (std::wstring(ui::Toolbar::tooltip(
            static_cast<std::uint16_t>(app::CommandId::view_source))).find(
            L"源码模式") == std::wstring::npos) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 5;
    }
    if (dispatcher.execute(app::CommandId::view_source) != ErrorCode::ok ||
        !dispatcher.query(app::CommandId::view_source).checked ||
        dispatcher.query(app::CommandId::format_bold).enabled) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 6;
    }
    SendMessageW(window.handle(), WM_COMMAND,
        static_cast<WPARAM>(app::CommandId::view_split), 0);
    if (!dispatcher.query(app::CommandId::view_split).checked ||
        !SendMessageW(window.toolbar()->handle(), TB_ISBUTTONCHECKED,
            static_cast<WPARAM>(app::CommandId::view_split), 0) ||
        SendMessageW(window.toolbar()->handle(), TB_ISBUTTONENABLED,
            static_cast<WPARAM>(app::CommandId::format_bold), 0) ||
        dispatcher.execute(app::CommandId::file_exit) != ErrorCode::ok ||
        !exit_requested) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 7;
    }
    SetWindowPos(window.handle(), nullptr, 0, 0, 760, 520,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT client{};
    RECT document{};
    GetClientRect(window.handle(), &client);
    GetWindowRect(window.document_window().handle(), &document);
    POINT corners[2]{{document.left, document.top}, {document.right, document.bottom}};
    MapWindowPoints(nullptr, window.handle(), corners, 2);
    if (corners[0].x != 0 ||
        corners[0].y != window.toolbar()->height() ||
        corners[1].x != client.right ||
        corners[1].y != client.bottom - window.status_bar().height()) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 8;
    }
    const auto before = session.snapshot();
    document::EditTransaction edit{1, before.source_revision,
        document::EditOrigin::source_view,
        {{{0, 0}, "状态栏"}}};
    if (session.commit(edit) != ErrorCode::ok) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 9;
    }
    wchar_t status[64]{};
    SendMessageW(window.status_bar().handle(), SB_GETTEXTW, 0,
        reinterpret_cast<LPARAM>(status));
    if (std::wstring(status) != L"未保存") {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 10;
    }
    if (dispatcher.query(app::CommandId::file_open).enabled) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 11;
    }
    window.status_bar().set_file_format(fileio::TextEncoding::utf16_le,
        fileio::LineEnding::lf);
    SendMessageW(window.status_bar().handle(), SB_GETTEXTW, 1,
        reinterpret_cast<LPARAM>(status));
    if (std::wstring(status) != L"UTF-16 LE") {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 12;
    }
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory temporary{std::filesystem::temp_directory_path() /
        ("markdownmay-app-files-" + std::to_string(nonce))};
    std::filesystem::create_directories(temporary.path);
    const auto first = temporary.path / L"首次保存.md";
    if (window.document_window().save_document_as(first) != ErrorCode::ok ||
        session.is_dirty() || !std::filesystem::exists(first)) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 13;
    }
    const auto second = temporary.path / L"打开文档.markdown";
    const std::string opened_source = "# 打开成功\n\nUTF-16 文件\n";
    if (fileio::SaveTextFileAtomic({second, opened_source,
            fileio::TextEncoding::utf16_le, fileio::LineEnding::lf}) !=
            ErrorCode::ok ||
        window.document_window().open_document(second) != ErrorCode::ok ||
        session.snapshot().source != opened_source ||
        window.document_window().encoding() != fileio::TextEncoding::utf16_le ||
        window.document_window().line_ending() != fileio::LineEnding::lf ||
        window.document_window().modes().mode() != editor::ViewMode::render) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 14;
    }
    const auto rich = window.document_window().modes().render_view().handle();
    SendMessageW(rich, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    SendMessageW(rich, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"追加\r"));
    if (window.document_window().save_document() != ErrorCode::ok) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 15;
    }
    const auto loaded = fileio::LoadTextFile(second);
    if (!loaded.is_ok() || loaded.value().source != opened_source + "追加\n" ||
        loaded.value().encoding != fileio::TextEncoding::utf16_le ||
        loaded.value().line_ending != fileio::LineEnding::lf ||
        window.document_window().new_document() != ErrorCode::ok ||
        window.document_window().is_named() || session.is_dirty() ||
        !session.snapshot().source.empty()) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 16;
    }
    const auto dropped = second.wstring();
    const auto drop_bytes = sizeof(DROPFILES) +
        (dropped.size() + 1) * sizeof(wchar_t);
    const auto drop_memory = GlobalAlloc(GHND, drop_bytes);
    if (!drop_memory) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 17;
    }
    auto* drop_data = static_cast<DROPFILES*>(GlobalLock(drop_memory));
    if (!drop_data) {
        GlobalFree(drop_memory);
        DestroyWindow(window.handle());
        CoUninitialize();
        return 18;
    }
    drop_data->pFiles = sizeof(DROPFILES);
    drop_data->fWide = TRUE;
    std::memcpy(reinterpret_cast<std::byte*>(drop_data) + sizeof(DROPFILES),
        dropped.c_str(), (dropped.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(drop_memory);
    SendMessageW(window.handle(), WM_DROPFILES,
        reinterpret_cast<WPARAM>(drop_memory), 0);
    if (dropped_path != second) {
        DestroyWindow(window.handle());
        CoUninitialize();
        return 19;
    }
    DestroyWindow(window.handle());
    CoUninitialize();
    return 0;
}
