#include <windows.h>

#include <commctrl.h>
#include <richedit.h>

#include <array>
#include <cstdint>
#include <cwchar>

namespace {

constexpr wchar_t kWindowClass[] = L"MarkdownMayPrototypeWindow";
constexpr wchar_t kAppTitle[] = L"马冬梅（Markdown May）— 第三阶段风险样机";
constexpr UINT kCreateEditorMessage = WM_APP + 1;

HINSTANCE g_instance = nullptr;
HMODULE g_rich_edit_module = nullptr;
HWND g_editor = nullptr;

void ApplyPrototypeFormatting(HWND editor) {
    CHARFORMAT2W base{};
    base.cbSize = sizeof(base);
    base.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR;
    base.yHeight = 240;
    base.crTextColor = RGB(32, 32, 32);
    wcscpy_s(base.szFaceName, L"Microsoft YaHei UI");
    SendMessageW(
        editor,
        EM_SETCHARFORMAT,
        SCF_ALL,
        reinterpret_cast<LPARAM>(&base));

    SendMessageW(editor, EM_SETSEL, 0, 14);
    CHARFORMAT2W heading{};
    heading.cbSize = sizeof(heading);
    heading.dwMask = CFM_BOLD | CFM_SIZE | CFM_COLOR;
    heading.dwEffects = CFE_BOLD;
    heading.yHeight = 400;
    heading.crTextColor = RGB(30, 70, 130);
    SendMessageW(
        editor,
        EM_SETCHARFORMAT,
        SCF_SELECTION,
        reinterpret_cast<LPARAM>(&heading));

    PARAFORMAT2 paragraph{};
    paragraph.cbSize = sizeof(paragraph);
    paragraph.dwMask = PFM_SPACEAFTER | PFM_LINESPACING;
    paragraph.dySpaceAfter = 120;
    paragraph.bLineSpacingRule = 4;
    paragraph.dyLineSpacing = 300;
    SendMessageW(
        editor,
        EM_SETPARAFORMAT,
        0,
        reinterpret_cast<LPARAM>(&paragraph));

    SendMessageW(
        editor,
        EM_SETSEL,
        static_cast<WPARAM>(-1),
        static_cast<LPARAM>(-1));
}

void PopulatePrototypeDocument(HWND editor) {
    constexpr wchar_t sample[] =
        L"马冬梅 Markdown May\r\n"
        L"\r\n"
        L"这是一个原生 Win32 + RichEdit 风险样机。\r\n"
        L"\r\n"
        L"• 可以直接输入中文并测试输入法候选框位置。\r\n"
        L"• 文本排版由 Windows 系统组件完成。\r\n"
        L"• 最终产品将提供渲染、源码、对照三种模式。\r\n"
        L"\r\n"
        L"请在这里输入中文、英文和 Emoji，观察输入与窗口缩放。";

    SetWindowTextW(editor, sample);
    ApplyPrototypeFormatting(editor);
}

void ResizeEditor(HWND window) {
    if (g_editor == nullptr) {
        return;
    }

    RECT client{};
    GetClientRect(window, &client);
    constexpr int margin = 16;
    MoveWindow(
        g_editor,
        margin,
        margin,
        (client.right - client.left) - (margin * 2),
        (client.bottom - client.top) - (margin * 2),
        TRUE);
}

bool CreateEditor(HWND window) {
    g_editor = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        MSFTEDIT_CLASS,
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN |
            ES_NOHIDESEL,
        0,
        0,
        0,
        0,
        window,
        nullptr,
        g_instance,
        nullptr);
    if (g_editor == nullptr) {
        return false;
    }

    SendMessageW(g_editor, EM_SETBKGNDCOLOR, 0, RGB(250, 250, 248));
    SendMessageW(g_editor, EM_SETLIMITTEXT, 0, 0);
    PopulatePrototypeDocument(g_editor);
    ResizeEditor(window);
    SetFocus(g_editor);
    return true;
}

LRESULT CALLBACK WindowProcedure(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param) {
    switch (message) {
        case WM_CREATE:
            return 0;

        case kCreateEditorMessage:
            return CreateEditor(window) ? 0 : -1;

        case WM_SIZE:
            ResizeEditor(window);
            return 0;

        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<const RECT*>(l_param);
            SetWindowPos(
                window,
                nullptr,
                suggested->left,
                suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOACTIVATE | SWP_NOZORDER);
            return 0;
        }

        case WM_SETFOCUS:
            if (g_editor != nullptr) {
                SetFocus(g_editor);
            }
            return 0;

        case WM_DESTROY:
            g_editor = nullptr;
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(window, message, w_param, l_param);
    }
}

bool RegisterPrototypeWindowClass() {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = WindowProcedure;
    window_class.hInstance = g_instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    window_class.hbrBackground =
        reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    window_class.lpszClassName = kWindowClass;
    return RegisterClassExW(&window_class) != 0;
}

void UpdateTitleWithStartupTime(HWND window, LARGE_INTEGER start) {
    LARGE_INTEGER finish{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&finish);
    QueryPerformanceFrequency(&frequency);

    const auto elapsed_microseconds =
        ((finish.QuadPart - start.QuadPart) * 1'000'000LL) /
        frequency.QuadPart;

    std::array<wchar_t, 160> title{};
    swprintf_s(
        title.data(),
        title.size(),
        L"%s — 窗口创建 %lld.%03lld ms",
        kAppTitle,
        elapsed_microseconds / 1000,
        elapsed_microseconds % 1000);
    SetWindowTextW(window, title.data());
}

}  // namespace

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE previous_instance,
    PWSTR command_line,
    int show_command) {
    UNREFERENCED_PARAMETER(previous_instance);
    UNREFERENCED_PARAMETER(command_line);

    LARGE_INTEGER startup_begin{};
    QueryPerformanceCounter(&startup_begin);

    g_instance = instance;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    g_rich_edit_module = LoadLibraryExW(
        L"msftedit.dll",
        nullptr,
        LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (g_rich_edit_module == nullptr) {
        MessageBoxW(
            nullptr,
            L"无法加载 Windows RichEdit 组件。",
            kAppTitle,
            MB_OK | MB_ICONERROR);
        return 1;
    }

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    if (!RegisterPrototypeWindowClass()) {
        MessageBoxW(
            nullptr,
            L"无法注册程序窗口。",
            kAppTitle,
            MB_OK | MB_ICONERROR);
        FreeLibrary(g_rich_edit_module);
        return 2;
    }

    HWND window = CreateWindowExW(
        0,
        kWindowClass,
        kAppTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        960,
        680,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (window == nullptr) {
        MessageBoxW(
            nullptr,
            L"无法创建程序窗口。",
            kAppTitle,
            MB_OK | MB_ICONERROR);
        FreeLibrary(g_rich_edit_module);
        return 3;
    }

    ShowWindow(window, show_command);
    UpdateWindow(window);
    UpdateTitleWithStartupTime(window, startup_begin);
    PostMessageW(window, kCreateEditorMessage, 0, 0);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    FreeLibrary(g_rich_edit_module);
    g_rich_edit_module = nullptr;
    return static_cast<int>(message.wParam);
}
