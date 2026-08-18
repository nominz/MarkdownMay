#include "markdownmay/ui/main_window.hpp"
#include "resource.h"

#include <commctrl.h>
#include <shellapi.h>

#include <mutex>
#include <algorithm>
#include <cwchar>
#include <utility>

namespace markdownmay::ui {
namespace {
constexpr wchar_t kMainWindowClass[] = L"MarkdownMay.MainWindow";
constexpr wchar_t kApplicationTitle[] = L"马冬梅 - Markdown May";
constexpr UINT kOpenRequestsMessage = WM_APP + 17;
constexpr UINT kRefreshChromeMessage = WM_APP + 18;
constexpr UINT kRefreshRecentFilesMessage = WM_APP + 19;

bool RegisterMainWindowClass(HINSTANCE instance) {
    static std::once_flag once;
    static bool available{};
    std::call_once(once, [instance] {
        WNDCLASSEXW value{};
        value.cbSize = sizeof(value);
        value.style = CS_HREDRAW | CS_VREDRAW;
        value.lpfnWndProc = MainWindow::WindowProcedure;
        value.hInstance = instance;
        value.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_MARKDOWNMAY));
        value.hIconSm = static_cast<HICON>(LoadImageW(
            instance, MAKEINTRESOURCEW(IDI_MARKDOWNMAY), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
            LR_DEFAULTCOLOR));
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
    : session_(session), document_window_(session),
      status_bar_(session, document_window_.modes()) {
    status_bar_.set_outline_callbacks(
        [this] { return document_window_.outline_visible(); },
        [this] {
            document_window_.toggle_outline();
            if (menu_controller_) menu_controller_->refresh();
        });
    const std::weak_ptr<int> lifetime(lifetime_);
    session.subscribe([this, lifetime](const document::DocumentEvent&) {
        if (!lifetime.expired()) refresh_document_chrome();
    });
}

MainWindow::~MainWindow() {
    lifetime_.reset();
    if (background_brush_) DeleteObject(background_brush_);
}

ErrorCode MainWindow::create(HINSTANCE instance, int show_command) {
    if (handle_) return ErrorCode::ok;
    instance_ = instance;
    if (!RegisterMainWindowClass(instance_))
        return ErrorCode::editor_render_projection_failed;
    handle_ = CreateWindowExW(0, kMainWindowClass, kApplicationTitle,
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 720,
        nullptr, nullptr, instance_, this);
    if (!handle_) return ErrorCode::editor_render_projection_failed;
    dpi_ = GetDpiForWindow(handle_);
    ApplyAppearance();
    ShowWindow(handle_, show_command);
    UpdateWindow(handle_);
    return ErrorCode::ok;
}

HWND MainWindow::handle() const noexcept { return handle_; }
DocumentWindow& MainWindow::document_window() noexcept { return document_window_; }
Toolbar* MainWindow::toolbar() noexcept { return toolbar_.get(); }
MenuController* MainWindow::menu_controller() noexcept { return menu_controller_.get(); }
StatusBar& MainWindow::status_bar() noexcept { return status_bar_; }
void MainWindow::set_command_callbacks(MenuController::Query query,
                                       MenuController::Execute execute) {
    auto execute_and_refresh = [this, execute = std::move(execute)](
            app::CommandId command) {
        execute(command);
        if (toolbar_) toolbar_->refresh();
        status_bar_.refresh();
        if (menu_controller_) menu_controller_->refresh();
    };
    toolbar_ = std::make_unique<Toolbar>(query, execute_and_refresh);
    menu_controller_ = std::make_unique<MenuController>(
        std::move(query), std::move(execute_and_refresh));
}
void MainWindow::set_drop_callback(
    std::function<void(const std::filesystem::path&)> callback) {
    drop_callback_ = std::move(callback);
}
void MainWindow::set_close_callback(std::function<bool()> callback) {
    close_callback_ = std::move(callback);
}
void MainWindow::set_activate_callback(std::function<void()> callback) {
    activate_callback_ = std::move(callback);
}
void MainWindow::set_open_request_callback(std::function<void()> callback) {
    open_request_callback_ = std::move(callback);
}
void MainWindow::notify_open_requests() noexcept {
    if (handle_) PostMessageW(handle_, kOpenRequestsMessage, 0, 0);
}
void MainWindow::refresh_document_chrome() {
    document_window_.refresh_outline_state();
    status_bar_.set_file_format(document_window_.encoding(),
        document_window_.line_ending());
    if (menu_controller_) menu_controller_->refresh();
    if (toolbar_) toolbar_->refresh();
    if (!handle_) return;
    const auto name = document_window_.is_named()
        ? document_window_.path().filename().wstring() : std::wstring(L"无标题");
    const auto pending = pending_open_count_ ?
        L" [待打开 " + std::to_wstring(pending_open_count_) + L"]" : std::wstring{};
    const auto title = name + (document_window_.is_read_only() ? L" [只读]" : L"") +
        (session_.is_dirty() ? L" *" : L"") + pending +
        L" - 马冬梅";
    SetWindowTextW(handle_, title.c_str());
}
HACCEL MainWindow::accelerator() const noexcept {
    return menu_controller_ ? menu_controller_->accelerator() : nullptr;
}
void MainWindow::set_recent_files(std::vector<std::filesystem::path> files) {
    pending_recent_files_ = std::move(files);
    if (!handle_) {
        if (menu_controller_)
            menu_controller_->set_recent_files(std::move(pending_recent_files_));
        return;
    }
    if (!recent_files_refresh_pending_) {
        recent_files_refresh_pending_ = true;
        PostMessageW(handle_, kRefreshRecentFilesMessage, 0, 0);
    }
}
void MainWindow::set_pending_open_count(std::size_t count) {
    pending_open_count_ = count;
    refresh_document_chrome();
}
void MainWindow::set_theme_preference(ThemePreference preference) {
    theme_preference_ = preference;
    ApplyAppearance();
}
ThemePreference MainWindow::theme_preference() const noexcept { return theme_preference_; }
ThemeKind MainWindow::theme_kind() const noexcept { return theme_kind_; }
UINT MainWindow::dpi() const noexcept { return dpi_; }

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
        case kOpenRequestsMessage:
            if (self->open_request_callback_) self->open_request_callback_();
            return 0;
        case kRefreshRecentFilesMessage:
            self->recent_files_refresh_pending_ = false;
            if (self->menu_controller_)
                self->menu_controller_->set_recent_files(
                    std::move(self->pending_recent_files_));
            return 0;
        case kRefreshChromeMessage:
            if (self->toolbar_) self->toolbar_->refresh();
            return 0;
        case WM_CREATE:
            if (self->CreateDocumentWindow() != ErrorCode::ok) return -1;
            if (self->menu_controller_ &&
                !self->menu_controller_->create(window)) return -1;
            if (self->toolbar_ && !self->toolbar_->create(window)) return -1;
            if (!self->status_bar_.create(window)) return -1;
            DragAcceptFiles(window, TRUE);
            self->Layout();
            self->refresh_document_chrome();
            return 0;
        case WM_COMMAND:
            if (self->document_window_.handle_control(
                    reinterpret_cast<HWND>(l_param), HIWORD(w_param))) return 0;
            if (self->toolbar_ && self->toolbar_->handle_control(
                    LOWORD(w_param), HIWORD(w_param), reinterpret_cast<HWND>(l_param))) {
                self->status_bar_.refresh();
                return 0;
            }
            if (self->menu_controller_ &&
                self->menu_controller_->handle_control(LOWORD(w_param),
                    reinterpret_cast<HWND>(l_param))) return 0;
            if (self->menu_controller_ &&
                self->menu_controller_->dispatch(LOWORD(w_param))) {
                if (self->toolbar_) self->toolbar_->refresh();
                self->status_bar_.refresh();
                return 0;
            }
            break;
        case WM_SYSCHAR:
            if (self->menu_controller_ &&
                self->menu_controller_->handle_syschar(
                    static_cast<wchar_t>(w_param))) return 0;
            break;
        case WM_INITMENU:
            if (self->menu_controller_) self->menu_controller_->refresh();
            return 0;
        case WM_MEASUREITEM:
            if (self->toolbar_ && self->toolbar_->measure_heading_menu(
                    *reinterpret_cast<MEASUREITEMSTRUCT*>(l_param))) return TRUE;
            if (self->menu_controller_ && self->menu_controller_->measure(
                    *reinterpret_cast<MEASUREITEMSTRUCT*>(l_param))) return TRUE;
            break;
        case WM_DRAWITEM:
            if (self->toolbar_ && self->toolbar_->draw_heading_menu(
                    *reinterpret_cast<DRAWITEMSTRUCT*>(l_param))) return TRUE;
            if (self->menu_controller_ &&
                self->menu_controller_->draw(
                    *reinterpret_cast<DRAWITEMSTRUCT*>(l_param))) return TRUE;
            break;
        case WM_NOTIFY: {
            const auto* header = reinterpret_cast<const NMHDR*>(l_param);
            if (header && self->document_window_.handle_notify(*header)) return 0;
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
            if (self->toolbar_ && header &&
                header->hwndFrom == self->toolbar_->handle() &&
                header->code == NM_CUSTOMDRAW) {
                return self->toolbar_->custom_draw(
                    *reinterpret_cast<NMTBCUSTOMDRAW*>(l_param));
            }
            break;
        }
        case WM_DROPFILES: {
            const auto drop = reinterpret_cast<HDROP>(w_param);
            const auto length = DragQueryFileW(drop, 0, nullptr, 0);
            std::wstring path(static_cast<std::size_t>(length) + 1, L'\0');
            if (length && DragQueryFileW(drop, 0, path.data(), length + 1)) {
                path.resize(length);
                if (self->drop_callback_) self->drop_callback_(path);
            }
            DragFinish(drop);
            return 0;
        }
        case WM_SIZE:
            self->Layout();
            return 0;
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            self->ApplyAppearance();
            return 0;
        case WM_ERASEBKGND: {
            RECT client{};
            GetClientRect(window, &client);
            FillRect(reinterpret_cast<HDC>(w_param), &client,
                self->background_brush_ ? self->background_brush_ :
                reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
            return 1;
        }
        case WM_ACTIVATE:
            if (LOWORD(w_param) != WA_INACTIVE && self->activate_callback_)
                self->activate_callback_();
            return 0;
        case WM_CLOSE:
            if (!self->close_callback_ || self->close_callback_())
                DestroyWindow(window);
            return 0;
        case WM_SETFOCUS:
            if (self->document_window_.handle())
                SetFocus(self->document_window_.modes().render_view().handle());
            return 0;
        case WM_DPICHANGED: {
            self->dpi_ = HIWORD(w_param);
            const auto* suggested = reinterpret_cast<const RECT*>(l_param);
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOACTIVATE | SWP_NOZORDER);
            self->ApplyAppearance();
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
    const auto menu_height = menu_controller_ ? menu_controller_->height() : 0;
    const auto toolbar_height = toolbar_ ? toolbar_->height() : 0;
    const auto status_height = status_bar_.handle() ? status_bar_.height() : 0;
    if (menu_controller_) menu_controller_->resize(width);
    if (toolbar_) toolbar_->resize(width, menu_height);
    status_bar_.resize(width, height);
    const auto document_bottom = (std::max)(
        static_cast<LONG>(menu_height + toolbar_height),
        static_cast<LONG>(height - status_height));
    RECT document{0, static_cast<LONG>(menu_height + toolbar_height),
        static_cast<LONG>(width), document_bottom};
    document_window_.resize(document);
}

void MainWindow::ApplyAppearance() {
    if (applying_appearance_) return;
    applying_appearance_ = true;
    theme_kind_ = ResolveTheme(theme_preference_, ReadSystemTheme());
    palette_ = PaletteFor(theme_kind_);
    if (background_brush_) DeleteObject(background_brush_);
    background_brush_ = CreateSolidBrush(palette_.window);
    if (handle_) {
        static_cast<void>(ApplyTitleBarTheme(handle_, theme_kind_));
    }
    if (toolbar_) toolbar_->apply_appearance(palette_.text, palette_.surface, dpi_);
    if (menu_controller_) menu_controller_->apply_appearance(
        palette_.text, palette_.surface, dpi_);
    status_bar_.apply_appearance(palette_.text, palette_.surface, dpi_);
    document_window_.apply_appearance(palette_.text, palette_.window, palette_.accent, dpi_);
    Layout();
    if (handle_) {
        InvalidateRect(handle_, nullptr, TRUE);
    }
    applying_appearance_ = false;
}

}  // namespace markdownmay::ui
