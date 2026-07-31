#include <windows.h>

#include <Scintilla.h>

#include <string_view>

namespace {

constexpr wchar_t kWindowClass[] = L"MarkdownMayScintillaProbeWindow";
constexpr wchar_t kWindowTitle[] =
    L"马冬梅 — Scintilla 静态链接风险样机";

HINSTANCE g_instance = nullptr;
HWND g_editor = nullptr;

void SendEditorMessage(
    unsigned int message,
    WPARAM w_param = 0,
    LPARAM l_param = 0) {
    SendMessageW(g_editor, message, w_param, l_param);
}

LRESULT CALLBACK WindowProcedure(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param) {
    switch (message) {
        case WM_CREATE: {
            g_editor = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"Scintilla",
                L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                    WS_HSCROLL,
                0,
                0,
                0,
                0,
                window,
                nullptr,
                g_instance,
                nullptr);
            if (g_editor == nullptr) {
                return -1;
            }

            SendEditorMessage(SCI_SETCODEPAGE, SC_CP_UTF8);
            SendEditorMessage(SCI_STYLESETFONT, STYLE_DEFAULT,
                reinterpret_cast<LPARAM>("Microsoft YaHei UI"));
            SendEditorMessage(SCI_STYLESETSIZE, STYLE_DEFAULT, 12);
            SendEditorMessage(SCI_STYLECLEARALL);
            SendEditorMessage(SCI_SETMARGINWIDTHN, 0, 48);
            SendEditorMessage(SCI_SETWRAPMODE, SC_WRAP_WORD);

            constexpr std::string_view sample =
                "# 马冬梅 Markdown May\n\n"
                "这是静态编入 EXE 的 Scintilla 源码编辑控件。\n\n"
                "- [x] 中文 UTF-8\n"
                "- [x] 可编辑 Markdown 源码\n"
                "- [x] 无 Scintilla.dll\n";
            SendEditorMessage(
                SCI_SETTEXT,
                0,
                reinterpret_cast<LPARAM>(sample.data()));
            SetFocus(g_editor);
            return 0;
        }

        case WM_SIZE:
            if (g_editor != nullptr) {
                MoveWindow(
                    g_editor,
                    0,
                    0,
                    LOWORD(l_param),
                    HIWORD(l_param),
                    TRUE);
            }
            return 0;

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

}  // namespace

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE previous_instance,
    PWSTR command_line,
    int show_command) {
    UNREFERENCED_PARAMETER(previous_instance);
    UNREFERENCED_PARAMETER(command_line);

    g_instance = instance;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    if (Scintilla_RegisterClasses(instance) == 0) {
        return 1;
    }

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = WindowProcedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    window_class.hbrBackground =
        reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    window_class.lpszClassName = kWindowClass;
    if (RegisterClassExW(&window_class) == 0) {
        Scintilla_ReleaseResources();
        return 2;
    }

    HWND window = CreateWindowExW(
        0,
        kWindowClass,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        900,
        640,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (window == nullptr) {
        Scintilla_ReleaseResources();
        return 3;
    }

    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    Scintilla_ReleaseResources();
    return static_cast<int>(message.wParam);
}
