#include "markdownmay/editor/view_mode_controller.hpp"

#include "markdownmay/fileio/line_endings.hpp"

#include <algorithm>
#include <mutex>
#include <utility>

namespace markdownmay::editor {
namespace {

constexpr wchar_t kModeHostClass[] = L"MarkdownMay.ViewMode.Host";
constexpr UINT kSynchronizeRenderMessage = WM_APP + 1;

bool RegisterModeClass() {
    static std::once_flag once;
    static bool available{};
    std::call_once(once, [] {
        WNDCLASSEXW value{};
        value.cbSize = sizeof(value);
        value.lpfnWndProc = ViewModeController::HostProcedure;
        value.hInstance = GetModuleHandleW(nullptr);
        value.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        value.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        value.lpszClassName = kModeHostClass;
        available = RegisterClassExW(&value) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    });
    return available;
}

}  // namespace

ViewModeController::ViewModeController(document::DocumentSession& session)
    : session_(session), render_(session), split_(session),
      observed_source_(session.snapshot().source) {
    const std::weak_ptr<int> lifetime(lifetime_);
    session_.subscribe([this, lifetime](const document::DocumentEvent& event) {
        if (!lifetime.expired()) ObserveChange(event);
    });
}

ViewModeController::~ViewModeController() {
    lifetime_.reset();
    if (host_ && IsWindow(host_)) DestroyWindow(host_);
}

ErrorCode ViewModeController::create(HWND parent, const RECT& bounds) {
    if (host_) return ErrorCode::ok;
    if (!RegisterModeClass()) return ErrorCode::editor_render_projection_failed;
    host_ = CreateWindowExW(0, kModeHostClass, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
        parent, nullptr, GetModuleHandleW(nullptr), this);
    if (!host_) return ErrorCode::editor_render_projection_failed;
    RECT child{0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top};
    if (render_.create(host_, child) != ErrorCode::ok ||
        split_.create(host_, child) != ErrorCode::ok) return ErrorCode::editor_split_control_failed;
    render_.set_read_only(false);
    ShowWindow(split_.handle(), SW_HIDE);
    ShowWindow(render_.handle(), SW_SHOW);
    mode_ = ViewMode::render;
    return ErrorCode::ok;
}

ErrorCode ViewModeController::switch_to(ViewMode target) {
    if (target == mode_) return ErrorCode::ok;
    auto selection = CaptureSelection();
    ErrorCode synchronized = ErrorCode::ok;
    if (mode_ == ViewMode::render) synchronized = render_.synchronize_change();
    else synchronized = split_.source_view().synchronize_now();
    if (synchronized != ErrorCode::ok && target == ViewMode::render)
        return ErrorCode::editor_cannot_enter_render_mode;
    if (target == ViewMode::render && !session_.can_export())
        return ErrorCode::editor_cannot_enter_render_mode;

    if (mode_ == ViewMode::render && target != ViewMode::render) {
        const auto result = split_.project();
        if (result != ErrorCode::ok) return result;
    } else if (target == ViewMode::render) {
        const auto result = render_.project();
        if (result != ErrorCode::ok) return result;
        render_.set_read_only(false);
    }

    ShowWindow(render_.handle(), target == ViewMode::render ? SW_SHOW : SW_HIDE);
    ShowWindow(split_.handle(), target == ViewMode::render ? SW_HIDE : SW_SHOW);
    if (target != ViewMode::render) split_.set_source_only(target == ViewMode::source);
    mode_ = target;
    RestoreSelection(selection);
    if (mode_ == ViewMode::render) SetFocus(render_.handle());
    else SetFocus(split_.source_view().handle());
    return ErrorCode::ok;
}

ErrorCode ViewModeController::undo() {
    if (mode_ != ViewMode::render) {
        const auto synchronized = split_.source_view().synchronize_now();
        if (synchronized != ErrorCode::ok) return synchronized;
    } else {
        const auto synchronized = render_.synchronize_change();
        if (synchronized != ErrorCode::ok) return synchronized;
    }
    if (undo_.empty()) return ErrorCode::ok;
    const auto entry = undo_.back();
    const auto result = ApplyHistory(entry.before, document::EditOrigin::undo);
    if (result != ErrorCode::ok) return result;
    undo_.pop_back();
    redo_.push_back(entry);
    return RefreshActive();
}

ErrorCode ViewModeController::redo() {
    if (mode_ != ViewMode::render) {
        const auto synchronized = split_.source_view().synchronize_now();
        if (synchronized != ErrorCode::ok) return synchronized;
    } else {
        const auto synchronized = render_.synchronize_change();
        if (synchronized != ErrorCode::ok) return synchronized;
    }
    if (redo_.empty()) return ErrorCode::ok;
    const auto entry = redo_.back();
    const auto result = ApplyHistory(entry.after, document::EditOrigin::redo);
    if (result != ErrorCode::ok) return result;
    redo_.pop_back();
    undo_.push_back(entry);
    return RefreshActive();
}

ErrorCode ViewModeController::cut() {
    return mode_ == ViewMode::render ? render_.cut() : split_.source_view().cut();
}

ErrorCode ViewModeController::copy() {
    return mode_ == ViewMode::render ? render_.copy() : split_.source_view().copy();
}

ErrorCode ViewModeController::paste() {
    return mode_ == ViewMode::render
        ? render_.paste_from_clipboard() : split_.source_view().paste();
}

ErrorCode ViewModeController::select_all() {
    return mode_ == ViewMode::render
        ? render_.select_all() : split_.source_view().select_all();
}

ErrorCode ViewModeController::execute(EditorCommand command) {
    if (mode_ != ViewMode::render) return ErrorCode::document_invalid_state;
    return render_.execute(command);
}

ErrorCode ViewModeController::save(const std::filesystem::path& target,
                                   fileio::TextEncoding encoding,
                                   fileio::LineEnding line_ending,
                                   fileio::BeforeAtomicReplace before_replace) {
    const auto synchronized = SynchronizeActive();
    const bool source_mode = mode_ != ViewMode::render;
    if (synchronized != ErrorCode::ok &&
        !(source_mode && synchronized == ErrorCode::markdown_parse_failed)) {
        return synchronized;
    }
    if (!source_mode && !session_.can_export())
        return ErrorCode::document_invariant_failed;

    const auto snapshot = session_.snapshot();
    const auto written = fileio::SaveTextFileAtomic(
        {target, snapshot.source, encoding, line_ending}, std::move(before_replace));
    if (written != ErrorCode::ok) return written;

    const auto reopened = fileio::LoadTextFile(target);
    if (!reopened.is_ok() || reopened.value().encoding != encoding ||
        reopened.value().line_ending != line_ending ||
        reopened.value().source != fileio::NormalizeLineEndings(
            snapshot.source, line_ending)) {
        return ErrorCode::file_read_failed;
    }
    return session_.mark_saved(snapshot.source_revision);
}

ErrorCode ViewModeController::reload(std::string source) {
    const auto result = session_.reload(std::move(source));
    if (result != ErrorCode::ok) return result;
    if (session_.can_export()) {
        render_.reset_to_start();
        if (render_.project() != ErrorCode::ok) return ErrorCode::editor_render_projection_failed;
        render_.reset_to_start();
        render_.set_read_only(false);
        ShowWindow(split_.handle(), SW_HIDE);
        ShowWindow(render_.handle(), SW_SHOW);
        mode_ = ViewMode::render;
        return ErrorCode::ok;
    }
    if (split_.project() != ErrorCode::ok) return ErrorCode::editor_split_control_failed;
    split_.set_source_only(true);
    ShowWindow(render_.handle(), SW_HIDE);
    ShowWindow(split_.handle(), SW_SHOW);
    mode_ = ViewMode::source;
    return ErrorCode::ok;
}

void ViewModeController::set_document_path(std::filesystem::path path) {
    render_.set_document_path(path);
    split_.render_view().set_document_path(std::move(path));
}

bool ViewModeController::can_undo() const noexcept { return !undo_.empty(); }
bool ViewModeController::can_redo() const noexcept { return !redo_.empty(); }
ViewMode ViewModeController::mode() const noexcept { return mode_; }
HWND ViewModeController::handle() const noexcept { return host_; }
RichEditHost& ViewModeController::render_view() noexcept { return render_; }
SourceView& ViewModeController::source_view() noexcept { return split_.source_view(); }
SplitView& ViewModeController::split_view() noexcept { return split_; }
void ViewModeController::apply_appearance(COLORREF text, COLORREF background,
                                          COLORREF accent, UINT dpi) {
    render_.apply_appearance(text, background, dpi);
    split_.source_view().apply_appearance(text, background, accent, dpi);
    split_.render_view().apply_appearance(text, background, dpi);
}

LRESULT CALLBACK ViewModeController::HostProcedure(HWND window, UINT message,
                                                    WPARAM w_param, LPARAM l_param) {
    auto* self = reinterpret_cast<ViewModeController*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        self = static_cast<ViewModeController*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, w_param, l_param);
    if (message == WM_SIZE) {
        self->Layout(LOWORD(l_param), HIWORD(l_param));
        return 0;
    }
    if (message == WM_COMMAND &&
        reinterpret_cast<HWND>(l_param) == self->render_.handle() &&
        HIWORD(w_param) == EN_CHANGE) {
        PostMessageW(window, kSynchronizeRenderMessage, 0, 0);
        return 0;
    }
    if (message == kSynchronizeRenderMessage) {
        static_cast<void>(self->render_.synchronize_change());
        return 0;
    }
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        self->host_ = nullptr;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

void ViewModeController::Layout(int width, int height) {
    if (render_.handle()) MoveWindow(render_.handle(), 0, 0, width, height, TRUE);
    if (split_.handle()) MoveWindow(split_.handle(), 0, 0, width, height, TRUE);
}

void ViewModeController::ObserveChange(const document::DocumentEvent& event) {
    const auto current = session_.snapshot().source;
    if (event.origin == document::EditOrigin::file_reload) {
        undo_.clear();
        redo_.clear();
        observed_source_ = current;
        return;
    }
    if (current == observed_source_) return;
    if (!applying_history_) {
        undo_.push_back({observed_source_, current});
        redo_.clear();
    }
    observed_source_ = current;
}

TextSelection ViewModeController::CaptureSelection() {
    if (mode_ == ViewMode::render) {
        const auto selected = render_.source_selection();
        if (selected.is_ok()) return selected.value();
        return {};
    }
    return split_.source_view().source_selection();
}

void ViewModeController::RestoreSelection(TextSelection selection) {
    const auto size = static_cast<std::uint64_t>(session_.snapshot().source.size());
    selection.anchor = (std::min)(selection.anchor, size);
    selection.caret = (std::min)(selection.caret, size);
    if (mode_ == ViewMode::render)
        static_cast<void>(render_.select_source_range(selection));
    else static_cast<void>(split_.source_view().select_source_range(selection));
}

ErrorCode ViewModeController::RefreshActive() {
    const auto selection = CaptureSelection();
    ErrorCode result{};
    if (mode_ == ViewMode::render) {
        result = render_.project();
        render_.set_read_only(false);
    } else {
        result = split_.project();
        split_.set_source_only(mode_ == ViewMode::source);
    }
    if (result == ErrorCode::ok) RestoreSelection(selection);
    return result;
}

ErrorCode ViewModeController::ApplyHistory(std::string source,
                                           document::EditOrigin origin) {
    const auto snapshot = session_.snapshot();
    applying_history_ = true;
    document::EditTransaction transaction{next_transaction_++, snapshot.source_revision, origin,
        {{{0, static_cast<std::uint64_t>(snapshot.source.size())}, std::move(source)}}};
    const auto result = session_.commit(transaction);
    applying_history_ = false;
    if (result == ErrorCode::ok) observed_source_ = session_.snapshot().source;
    return result;
}

ErrorCode ViewModeController::SynchronizeActive() {
    return mode_ == ViewMode::render
        ? render_.synchronize_change()
        : split_.source_view().synchronize_now();
}

}  // namespace markdownmay::editor
