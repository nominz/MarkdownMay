#include "markdownmay/ui/main_window.hpp"

#include <commctrl.h>

#include <mutex>
#include <algorithm>
#include <cwchar>
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
    : document_window_(session), status_bar_(session, document_window_.modes()) {
    const std::weak_ptr<int> lifetime(lifetime_);
    session.subscribe([this, lifetime](const document::DocumentEvent&) {
        if (!lifetime.expired()) status_bar_.refresh();
    });
}

MainWindow::~MainWindow() { lifetime_.reset(); }

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
Toolbar* MainWindow::toolbar() noexcept { return toolbar_.get(); }
StatusBar& MainWindow::status_bar() noexcept { return status_bar_; }
void MainWindow::set_command_callbacks(MenuController::Query query,
                                       MenuController::Execute execute) {
    toolbar_ = std::make_unique<Toolbar>(query);
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
            if (self->toolbar_ && !self->toolbar_->create(window)) return -1;
            if (!self->status_bar_.create(window)) return -1;
            self->Layout();
            return 0;
        case WM_COMMAND:
            if (self->menu_controller_ &&
                self->menu_controller_->dispatch(LOWORD(w_param))) {
                if (self->toolbar_) self->toolbar_->refresh();
                self->status_bar_.refresh();
                return 0;
            }
            break;
        case WM_INITMENU:
            if (self->menu_controller_) self->menu_controller_->refresh();
            return 0;
        case WM_NOTIFY: {
            const auto* header = reinterpret_cast<const NMHDR*>(l_param);
            if (self->toolbar_ && header &&
                header->hwndFrom == self->toolbar_->handle() &&
                header->code == TBN_GETINFOTIPW) {
                auto* information = reinterpret_cast<NMTBGETINFOTIPW*>(l_param);
                wcsncpy_s(information->pszText,
                    static_cast<std::size_t>(information->cchTextMax),
                    Toolbar::tooltip(static_cast<std::uint16_t>(information->iItem)),
                    _TRUNCATE);
                return 0;
            }
            break;
        }
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
    const auto width = client.right - client.left;
    const auto height = client.bottom - client.top;
    const auto toolbar_height = toolbar_ ? toolbar_->height() : 0;
    const auto status_height = status_bar_.handle() ? status_bar_.height() : 0;
    if (toolbar_) toolbar_->resize(width);
    status_bar_.resize(width, height);
    const auto document_bottom = (std::max)(
        static_cast<LONG>(toolbar_height),
        static_cast<LONG>(height - status_height));
    RECT document{0, static_cast<LONG>(toolbar_height),
        static_cast<LONG>(width), document_bottom};
    document_window_.resize(document);
}

}  // namespace markdownmay::ui
