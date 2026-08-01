#include "markdownmay/ui/main_window.hpp"

#include <mutex>
#include <utility>

namespace markdownmay::ui {
namespace {
constexpr wchar_t kMainWindowClass[] = L"MarkdownMay.MainWindow";
constexpr wchar_t kApplicationTitle[] = L"马冬梅 - Markdown May";

bool RegisterMainWindowClass(HINSTANCE instance) {
    static std::once_flag once;
    static bool available{};
    std::call_once(once, [instance] {
        WNDCLASSEXW value{};
        value.cbSize = sizeof(value);
        value.style = CS_HREDRAW | CS_VREDRAW;
        value.lpfnWndProc = MainWindow::WindowProcedure;
        value.hInstance = instance;
        value.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        value.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        value.lpszClassName = kMainWindowClass;
        available = RegisterClassExW(&value) != 0 ||
            GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    });
    return available;
}
}  // namespace

MainWindow::MainWindow(document::DocumentSession& session)
    : document_window_(session) {}

ErrorCode MainWindow::create(HINSTANCE instance, int show_command) {
    if (handle_) return ErrorCode::ok;
    instance_ = instance;
    if (!RegisterMainWindowClass(instance_))
        return ErrorCode::editor_render_projection_failed;
    handle_ = CreateWindowExW(0, kMainWindowClass, kApplicationTitle,
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 720,
        nullptr, nullptr, instance_, this);
    if (!handle_) return ErrorCode::editor_render_projection_failed;
    ShowWindow(handle_, show_command);
    UpdateWindow(handle_);
    return ErrorCode::ok;
}

HWND MainWindow::handle() const noexcept { return handle_; }
DocumentWindow& MainWindow::document_window() noexcept { return document_window_; }
void MainWindow::set_command_callbacks(MenuController::Query query,
                                       MenuController::Execute execute) {
    menu_controller_ = std::make_unique<MenuController>(
        std::move(query), std::move(execute));
}
HACCEL MainWindow::accelerator() const noexcept {
    return menu_controller_ ? menu_controller_->accelerator() : nullptr;
}

LRESULT CALLBACK MainWindow::WindowProcedure(HWND window, UINT message,
                                              WPARAM w_param, LPARAM l_param) {
    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        self = static_cast<MainWindow*>(create->lpCreateParams);
        self->handle_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, w_param, l_param);
    try {
        switch (message) {
        case WM_CREATE:
            if (self->CreateDocumentWindow() != ErrorCode::ok) return -1;
            if (self->menu_controller_ &&
                !self->menu_controller_->create(window)) return -1;
            return 0;
        case WM_COMMAND:
            if (self->menu_controller_ &&
                self->menu_controller_->dispatch(LOWORD(w_param))) return 0;
            break;
        case WM_INITMENU:
            if (self->menu_controller_) self->menu_controller_->refresh();
            return 0;
        case WM_SIZE:
            self->Layout();
            return 0;
        case WM_SETFOCUS:
            if (self->document_window_.handle())
                SetFocus(self->document_window_.modes().render_view().handle());
            return 0;
        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<const RECT*>(l_param);
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOACTIVATE | SWP_NOZORDER);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            self->handle_ = nullptr;
            break;
        default:
            break;
        }
    } catch (...) {
        DestroyWindow(window);
        return 0;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

ErrorCode MainWindow::CreateDocumentWindow() {
    RECT client{};
    GetClientRect(handle_, &client);
    return document_window_.create(handle_, client);
}

void MainWindow::Layout() {
    RECT client{};
    GetClientRect(handle_, &client);
    document_window_.resize(client);
}

}  // namespace markdownmay::ui
