#include "markdownmay/ui/document_window.hpp"

#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>

namespace markdownmay::ui {

DocumentWindow::DocumentWindow(document::DocumentSession& session)
    : modes_(session), outline_(session, modes_) {}

ErrorCode DocumentWindow::create(HWND parent, const RECT& bounds) {
    bounds_ = bounds;
    if (!outline_.create(parent)) return ErrorCode::editor_split_control_failed;
    outline_divider_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_NOTIFY,
        0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!outline_divider_ || !SetWindowSubclass(outline_divider_, DividerProcedure, 1,
            reinterpret_cast<DWORD_PTR>(this))) return ErrorCode::editor_split_control_failed;
    const auto result = modes_.create(parent, bounds);
    resize(bounds);
    return result;
}

void DocumentWindow::resize(const RECT& bounds) {
    bounds_ = bounds;
    if (!modes_.handle()) return;
    const auto available = static_cast<int>((std::max)(0L,
        (bounds.right - bounds.left) / 2));
    const bool effective_visible = outline_visible_ && outline_.has_headings();
    const auto outline_width = effective_visible
        ? (std::min)(outline_.width(), available) : 0;
    if (outline_.handle()) {
        ShowWindow(outline_.handle(), effective_visible ? SW_SHOW : SW_HIDE);
        outline_.resize({bounds.left, bounds.top, bounds.left + outline_width, bounds.bottom});
    }
    const auto divider = effective_visible ? MulDiv(5, GetDpiForWindow(modes_.handle()), 96) : 0;
    if (outline_divider_) {
        ShowWindow(outline_divider_, effective_visible ? SW_SHOW : SW_HIDE);
        MoveWindow(outline_divider_, bounds.left + outline_width, bounds.top,
            divider, bounds.bottom - bounds.top, TRUE);
    }
    MoveWindow(modes_.handle(), bounds.left + outline_width + divider, bounds.top,
        bounds.right - bounds.left - outline_width - divider, bounds.bottom - bounds.top, TRUE);
}

HWND DocumentWindow::handle() const noexcept { return modes_.handle(); }
editor::ViewModeController& DocumentWindow::modes() noexcept { return modes_; }
void DocumentWindow::set_outline_visible(bool visible) {
    outline_visible_ = visible;
    resize(bounds_);
}
void DocumentWindow::toggle_outline() { set_outline_visible(!outline_visible_); }
bool DocumentWindow::outline_visible() const noexcept {
    return outline_visible_ && outline_.has_headings();
}
void DocumentWindow::refresh_outline_state() { resize(bounds_); }
HWND DocumentWindow::outline_handle() const noexcept { return outline_.handle(); }
bool DocumentWindow::handle_control(HWND control, std::uint16_t notification) {
    return outline_.handle_control(control, notification);
}

ErrorCode DocumentWindow::new_document() {
    const auto result = modes_.reload("");
    if (result != ErrorCode::ok) return result;
    path_.clear();
    encoding_ = fileio::TextEncoding::utf8;
    line_ending_ = fileio::LineEnding::crlf;
    read_only_ = false;
    disk_source_.clear();
    modes_.set_document_path({});
    return ErrorCode::ok;
}

ErrorCode DocumentWindow::open_document(const std::filesystem::path& path) {
    const auto loaded = fileio::LoadTextFile(path);
    if (!loaded.is_ok()) return loaded.error();
    const auto result = modes_.reload(loaded.value().source);
    if (result != ErrorCode::ok) return result;
    path_ = loaded.value().path;
    encoding_ = loaded.value().encoding;
    line_ending_ = loaded.value().line_ending;
    const auto attributes = GetFileAttributesW(path_.c_str());
    read_only_ = attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_READONLY) != 0;
    disk_source_ = loaded.value().source;
    modes_.set_document_path(path_);
    return ErrorCode::ok;
}

ErrorCode DocumentWindow::save_document() {
    if (path_.empty()) return ErrorCode::document_invalid_state;
    if (read_only_) return ErrorCode::file_read_only;
    const auto result = modes_.save(path_, encoding_, line_ending_);
    if (result == ErrorCode::ok) acknowledge_external_change();
    return result;
}

ErrorCode DocumentWindow::save_document_as(const std::filesystem::path& path) {
    const auto result = modes_.save(path, encoding_, line_ending_);
    if (result != ErrorCode::ok) return result;
    path_ = std::filesystem::absolute(path).lexically_normal();
    read_only_ = false;
    acknowledge_external_change();
    modes_.set_document_path(path_);
    return ErrorCode::ok;
}

ErrorCode DocumentWindow::reload_document() {
    if (path_.empty()) return ErrorCode::document_invalid_state;
    return open_document(path_);
}

bool DocumentWindow::is_named() const noexcept { return !path_.empty(); }
bool DocumentWindow::is_read_only() const noexcept { return read_only_; }
bool DocumentWindow::has_external_change() const {
    if (path_.empty()) return false;
    const auto loaded = fileio::LoadTextFile(path_);
    return !loaded.is_ok() || loaded.value().source != disk_source_;
}
void DocumentWindow::acknowledge_external_change() {
    if (path_.empty()) { disk_source_.clear(); return; }
    const auto loaded = fileio::LoadTextFile(path_);
    if (loaded.is_ok()) disk_source_ = loaded.value().source;
}
const std::filesystem::path& DocumentWindow::path() const noexcept { return path_; }
fileio::TextEncoding DocumentWindow::encoding() const noexcept { return encoding_; }
fileio::LineEnding DocumentWindow::line_ending() const noexcept { return line_ending_; }
void DocumentWindow::set_line_ending(fileio::LineEnding line_ending) noexcept {
    line_ending_ = line_ending;
}
void DocumentWindow::apply_appearance(COLORREF text, COLORREF background,
                                      COLORREF accent, UINT dpi) {
    modes_.apply_appearance(text, background, accent, dpi);
    outline_.apply_appearance(text, background, dpi);
    resize(bounds_);
}

LRESULT CALLBACK DocumentWindow::DividerProcedure(HWND window, UINT message,
        WPARAM w_param, LPARAM l_param, UINT_PTR id, DWORD_PTR data) {
    auto* self = reinterpret_cast<DocumentWindow*>(data);
    if (message == WM_SETCURSOR) { SetCursor(LoadCursorW(nullptr, IDC_SIZEWE)); return TRUE; }
    if (message == WM_LBUTTONDOWN && self) {
        self->dragging_outline_ = true; SetCapture(window); return 0;
    }
    if (message == WM_MOUSEMOVE && self && self->dragging_outline_) {
        POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        ClientToScreen(window, &point);
        const auto parent = GetParent(window);
        ScreenToClient(parent, &point);
        const auto maximum = (std::max)(120L, (self->bounds_.right - self->bounds_.left) / 2);
        self->outline_.set_width(static_cast<int>((std::clamp)(
            point.x - self->bounds_.left, 120L, maximum)));
        self->resize(self->bounds_); return 0;
    }
    if (message == WM_LBUTTONUP && self && self->dragging_outline_) {
        self->dragging_outline_ = false; ReleaseCapture(); return 0;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, DividerProcedure, id);
    return DefSubclassProc(window, message, w_param, l_param);
}

}  // namespace markdownmay::ui
