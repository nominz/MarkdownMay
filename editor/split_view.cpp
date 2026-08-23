#include "markdownmay/editor/split_view.hpp"

#include <windowsx.h>

#include <mutex>
#include <string>

namespace markdownmay::editor {
namespace {

constexpr wchar_t kSplitHostClass[] = L"MarkdownMay.SplitView.Host";
constexpr int kDividerWidth = 6;

bool RegisterSplitClass() {
    static std::once_flag once;
    static bool available{};
    std::call_once(once, [] {
        WNDCLASSEXW value{};
        value.cbSize = sizeof(value);
        value.lpfnWndProc = SplitView::HostProcedure;
        value.hInstance = GetModuleHandleW(nullptr);
        value.hCursor = LoadCursorW(nullptr, IDC_SIZEWE);
        value.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH));
        value.lpszClassName = kSplitHostClass;
        available = RegisterClassExW(&value) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    });
    return available;
}

}  // namespace

SplitView::SplitView(document::DocumentSession& session)
    : session_(session), source_(session), render_(session) {
    render_.set_block_menu_callback([this](BlockMenuCommand command,
        const BlockCommandContext& context) {
        static_cast<void>(execute_block_menu(command, context));
    });
    source_.set_synchronized_callback(
        [this](ErrorCode result) {
            if (!suppress_refresh_) RefreshRender(result);
        });
    source_.set_scroll_callback([this](std::uint64_t line, std::uint64_t total) {
        render_.scroll_to_fraction(line, total);
    });
}

SplitView::~SplitView() {
    render_.set_block_menu_callback({});
    source_.set_synchronized_callback({});
    source_.set_scroll_callback({});
    if (host_ && IsWindow(host_)) DestroyWindow(host_);
}

ErrorCode SplitView::create(HWND parent, const RECT& bounds) {
    if (host_) return ErrorCode::ok;
    if (!RegisterSplitClass()) return ErrorCode::editor_split_control_failed;
    host_ = CreateWindowExW(0, kSplitHostClass, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
        parent, nullptr, GetModuleHandleW(nullptr), this);
    if (!host_) return ErrorCode::editor_split_control_failed;
    const auto width = bounds.right - bounds.left;
    const auto height = bounds.bottom - bounds.top;
    const auto left = (std::max)(1L, (width - kDividerWidth) / 2L);
    RECT source_bounds{0, 0, left, height};
    RECT render_bounds{left + kDividerWidth, 0, width, height};
    if (source_.create(host_, source_bounds) != ErrorCode::ok ||
        render_.create(host_, render_bounds) != ErrorCode::ok) {
        return ErrorCode::editor_split_control_failed;
    }
    render_.set_read_only(true);
    ShowWindow(render_.handle(), SW_SHOW);
    return project();
}

ErrorCode SplitView::project() {
    const auto source_result = source_.project();
    if (source_result != ErrorCode::ok) return source_result;
    RefreshRender(ErrorCode::ok);
    return ErrorCode::ok;
}

ErrorCode SplitView::synchronize_source(bool refresh_render) {
    suppress_refresh_ = !refresh_render;
    const auto result = source_.synchronize_now();
    suppress_refresh_ = false;
    return result;
}

ErrorCode SplitView::execute_block_menu(
    const BlockMenuCommand command, const BlockCommandContext& context) {
    const auto result = render_.execute_block_menu(command, context);
    if (result != ErrorCode::ok) return result;
    const auto selection = render_.source_selection();
    const auto refreshed = project();
    if (refreshed != ErrorCode::ok) return refreshed;
    if (selection.is_ok())
        static_cast<void>(source_.select_source_range(selection.value()));
    return ErrorCode::ok;
}

HWND SplitView::handle() const noexcept { return host_; }
SourceView& SplitView::source_view() noexcept { return source_; }
RichEditHost& SplitView::render_view() noexcept { return render_; }
void SplitView::set_source_only(bool source_only) {
    source_only_ = source_only;
    if (!host_) return;
    ShowWindow(render_.handle(), source_only ? SW_HIDE : SW_SHOW);
    RECT client{};
    GetClientRect(host_, &client);
    Layout(client.right, client.bottom);
}

LRESULT CALLBACK SplitView::HostProcedure(HWND window, UINT message,
                                           WPARAM w_param, LPARAM l_param) {
    auto* self = reinterpret_cast<SplitView*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        self = static_cast<SplitView*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, w_param, l_param);
    if (message == WM_SIZE) {
        self->Layout(LOWORD(l_param), HIWORD(l_param));
        return 0;
    }
    if (message == WM_LBUTTONDOWN && !self->source_only_) {
        const auto x = GET_X_LPARAM(l_param);
        if (x >= self->divider_position_ && x < self->divider_position_ + kDividerWidth) {
            self->dragging_divider_ = true;
            SetCapture(window);
            return 0;
        }
    }
    if (message == WM_MOUSEMOVE && self->dragging_divider_) {
        RECT client{};
        GetClientRect(window, &client);
        const auto minimum = (std::min)(MulDiv(120, GetDpiForWindow(window), 96),
            static_cast<int>(client.right / 3));
        self->divider_position_ = (std::clamp)(GET_X_LPARAM(l_param), minimum,
            (std::max)(minimum, static_cast<int>(client.right) - minimum - kDividerWidth));
        self->Layout(client.right, client.bottom);
        return 0;
    }
    if (message == WM_LBUTTONUP && self->dragging_divider_) {
        self->dragging_divider_ = false;
        ReleaseCapture();
        return 0;
    }
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        self->host_ = nullptr;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

void SplitView::Layout(int width, int height) {
    if (!source_.host_handle() || !render_.handle()) return;
    if (source_only_) {
        MoveWindow(source_.host_handle(), 0, 0, width, height, TRUE);
        return;
    }
    if (divider_position_ <= 0)
        divider_position_ = (std::max)(1, (width - kDividerWidth) / 2);
    const auto minimum = (std::min)(MulDiv(120, host_ ? GetDpiForWindow(host_) : 96, 96), width / 3);
    const auto left = (std::clamp)(divider_position_, minimum,
        (std::max)(minimum, width - minimum - kDividerWidth));
    divider_position_ = left;
    MoveWindow(source_.host_handle(), 0, 0, left, height, TRUE);
    MoveWindow(render_.handle(), left + kDividerWidth, 0,
        (std::max)(1, width - left - kDividerWidth), height, TRUE);
}

void SplitView::RefreshRender(ErrorCode source_result) {
    if (!render_.handle()) return;
    if (source_result == ErrorCode::ok && session_.can_export()) {
        static_cast<void>(render_.project());
        render_.set_read_only(true);
        return;
    }
    std::wstring message = L"当前源码无法渲染";
    if (!source_.diagnostics().empty()) {
        message += L"\r\n\r\n错误位置：第 " +
            std::to_wstring(source_.diagnostics().front().line) + L" 行，第 " +
            std::to_wstring(source_.diagnostics().front().column) + L" 列";
    }
    static_cast<void>(render_.show_status_message(message));
    render_.set_read_only(true);
}

}  // namespace markdownmay::editor
