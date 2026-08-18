#include "markdownmay/editor/source_view.hpp"

#include <Scintilla.h>
#include <commctrl.h>

#include <algorithm>
#include <mutex>
#include <string_view>

namespace markdownmay::editor {
namespace {

constexpr wchar_t kSourceHostClass[] = L"MarkdownMay.SourceView.Host";
constexpr UINT_PTR kSynchronizeTimer = 1;
constexpr UINT kDebounceMilliseconds = 150;
constexpr UINT kMaximumLatencyMilliseconds = 500;
constexpr int kErrorIndicator = 8;
constexpr int kFoldMargin = 2;
constexpr int kExpandedMarker = 24;
constexpr int kCollapsedMarker = 25;

LRESULT SendEditor(HWND editor, unsigned int message,
                   WPARAM w_param = 0, LPARAM l_param = 0) {
    return SendMessageW(editor, message, w_param, l_param);
}

bool RegisterSourceClasses() {
    static std::once_flag once;
    static bool available{};
    std::call_once(once, [] {
        const auto instance = GetModuleHandleW(nullptr);
        if (Scintilla_RegisterClasses(instance) == 0) return;
        WNDCLASSEXW value{};
        value.cbSize = sizeof(value);
        value.lpfnWndProc = SourceView::HostProcedure;
        value.hInstance = instance;
        value.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
        value.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        value.lpszClassName = kSourceHostClass;
        available = RegisterClassExW(&value) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    });
    return available;
}

LRESULT CALLBACK SourceEditorSubclass(HWND window, UINT message, WPARAM w_param,
                                      LPARAM l_param, UINT_PTR, DWORD_PTR reference) {
    auto* self = reinterpret_cast<SourceView*>(reference);
    if (message == WM_KEYDOWN && w_param == VK_OEM_4 &&
        (GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
        (GetKeyState(VK_SHIFT) & 0x8000) != 0 && self &&
        self->toggle_heading_fold_at_caret()) return 0;
    return DefSubclassProc(window, message, w_param, l_param);
}

}  // namespace

SourceView::SourceView(document::DocumentSession& session) : session_(session), sync_(session) {}

SourceView::~SourceView() {
    if (editor_ && IsWindow(editor_)) RemoveWindowSubclass(editor_, SourceEditorSubclass, 1);
    if (host_ && IsWindow(host_)) DestroyWindow(host_);
}

ErrorCode SourceView::create(HWND parent, const RECT& bounds) {
    if (host_) return ErrorCode::ok;
    if (!RegisterSourceClasses()) return ErrorCode::editor_source_control_failed;
    host_ = CreateWindowExW(0, kSourceHostClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
        parent, nullptr, GetModuleHandleW(nullptr), this);
    if (!host_) return ErrorCode::editor_source_control_failed;
    editor_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"Scintilla", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL,
        0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top,
        host_, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!editor_) {
        DestroyWindow(host_);
        host_ = nullptr;
        return ErrorCode::editor_source_control_failed;
    }
    Configure();
    if (!SetWindowSubclass(editor_, SourceEditorSubclass, 1,
            reinterpret_cast<DWORD_PTR>(this)))
        return ErrorCode::editor_source_control_failed;
    return project();
}

ErrorCode SourceView::project() {
    if (!editor_) return ErrorCode::editor_source_control_failed;
    projecting_ = true;
    const auto source = session_.snapshot().source;
    SendEditor(editor_, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(source.c_str()));
    SendEditor(editor_, SCI_EMPTYUNDOBUFFER);
    SendEditor(editor_, SCI_SETSAVEPOINT);
    projecting_ = false;
    ApplyStyles();
    ApplyDiagnostics();
    apply_heading_folds();
    return ErrorCode::ok;
}

ErrorCode SourceView::synchronize_now() {
    if (!editor_) return ErrorCode::editor_source_control_failed;
    KillTimer(host_, kSynchronizeTimer);
    pending_since_ = 0;
    last_error_ = sync_.synchronize(ReadSource());
    ApplyStyles();
    ApplyDiagnostics();
    if (synchronized_callback_) synchronized_callback_(last_error_);
    return last_error_;
}

ErrorCode SourceView::save(const std::filesystem::path& target,
                           fileio::TextEncoding encoding,
                           fileio::LineEnding line_ending) {
    const auto synchronized = synchronize_now();
    if (synchronized != ErrorCode::ok &&
        synchronized != ErrorCode::markdown_parse_failed) return synchronized;
    const auto result = sync_.save(target, encoding, line_ending);
    if (result == ErrorCode::ok) SendEditor(editor_, SCI_SETSAVEPOINT);
    last_error_ = result;
    return result;
}

ErrorCode SourceView::go_to_first_error() {
    if (sync_.diagnostics().empty()) return ErrorCode::ok;
    const auto position = static_cast<WPARAM>(sync_.diagnostics().front().begin);
    SendEditor(editor_, SCI_GOTOPOS, position);
    SendEditor(editor_, SCI_SETSEL, position,
        static_cast<LPARAM>(sync_.diagnostics().front().end));
    SetFocus(editor_);
    return ErrorCode::ok;
}

ErrorCode SourceView::last_error() const noexcept { return last_error_; }
const std::vector<SourceDiagnostic>& SourceView::diagnostics() const noexcept {
    return sync_.diagnostics();
}
HWND SourceView::handle() const noexcept { return editor_; }
HWND SourceView::host_handle() const noexcept { return host_; }
TextSelection SourceView::source_selection() const noexcept {
    if (!editor_) return {};
    return {static_cast<std::uint64_t>(SendEditor(editor_, SCI_GETANCHOR)),
            static_cast<std::uint64_t>(SendEditor(editor_, SCI_GETCURRENTPOS))};
}
ErrorCode SourceView::select_source_range(TextSelection selection) {
    if (!editor_) return ErrorCode::editor_source_control_failed;
    const auto length = static_cast<std::uint64_t>(SendEditor(editor_, SCI_GETLENGTH));
    if (selection.anchor > length || selection.caret > length)
        return ErrorCode::editor_selection_mapping_failed;
    SendEditor(editor_, SCI_SETSEL, static_cast<WPARAM>(selection.anchor),
        static_cast<LPARAM>(selection.caret));
    SendEditor(editor_, SCI_SCROLLCARET);
    return ErrorCode::ok;
}

std::pair<std::uint64_t, std::uint64_t> SourceView::scroll_fraction() const {
    if (!editor_) return {0, 1};
    return {static_cast<std::uint64_t>(SendEditor(editor_, SCI_GETFIRSTVISIBLELINE)),
        static_cast<std::uint64_t>((std::max)(sptr_t{1}, SendEditor(editor_, SCI_GETLINECOUNT)))};
}

void SourceView::scroll_to_fraction(std::uint64_t numerator, std::uint64_t denominator) {
    if (!editor_ || denominator == 0) return;
    const auto lines = static_cast<std::uint64_t>((std::max)(
        sptr_t{1}, SendEditor(editor_, SCI_GETLINECOUNT)));
    const auto target = numerator * lines / denominator;
    const auto current = static_cast<std::uint64_t>(
        SendEditor(editor_, SCI_GETFIRSTVISIBLELINE));
    SendEditor(editor_, SCI_LINESCROLL, 0,
        static_cast<LPARAM>(static_cast<std::int64_t>(target) -
            static_cast<std::int64_t>(current)));
}

ErrorCode SourceView::cut() {
    if (!editor_) return ErrorCode::editor_source_control_failed;
    SendEditor(editor_, SCI_CUT);
    return ErrorCode::ok;
}

ErrorCode SourceView::copy() {
    if (!editor_) return ErrorCode::editor_source_control_failed;
    SendEditor(editor_, SCI_COPY);
    return ErrorCode::ok;
}

ErrorCode SourceView::paste() {
    if (!editor_) return ErrorCode::editor_source_control_failed;
    SendEditor(editor_, SCI_PASTE);
    return ErrorCode::ok;
}

ErrorCode SourceView::select_all() {
    if (!editor_) return ErrorCode::editor_source_control_failed;
    SendEditor(editor_, SCI_SELECTALL);
    return ErrorCode::ok;
}
void SourceView::set_synchronized_callback(std::function<void(ErrorCode)> callback) {
    synchronized_callback_ = std::move(callback);
}
void SourceView::set_scroll_callback(
    std::function<void(std::uint64_t, std::uint64_t)> callback) {
    scroll_callback_ = std::move(callback);
}
void SourceView::set_heading_folds(HeadingFoldController* folds) {
    folds_ = folds;
    apply_heading_folds();
}

LRESULT CALLBACK SourceView::HostProcedure(HWND window, UINT message,
                                            WPARAM w_param, LPARAM l_param) {
    auto* self = reinterpret_cast<SourceView*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        self = static_cast<SourceView*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, w_param, l_param);
    if (message == WM_SIZE && self->editor_) {
        MoveWindow(self->editor_, 0, 0, LOWORD(l_param), HIWORD(l_param), TRUE);
        return 0;
    }
    if (message == WM_SETFOCUS && self->editor_) {
        SetFocus(self->editor_);
        return 0;
    }
    if (message == WM_NOTIFY && self->editor_) {
        const auto* notification = reinterpret_cast<SCNotification*>(l_param);
        if (notification && notification->nmhdr.hwndFrom == self->editor_ &&
            notification->nmhdr.code == SCN_MODIFIED && !self->projecting_ &&
            (notification->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) != 0) {
            self->ScheduleSynchronize();
        } else if (notification && notification->nmhdr.hwndFrom == self->editor_ &&
            notification->nmhdr.code == SCN_MARGINCLICK &&
            notification->margin == kFoldMargin && self->folds_) {
            const auto position = static_cast<std::uint64_t>(notification->position);
            const auto found = std::find_if(self->folds_->items().begin(),
                self->folds_->items().end(), [position](const auto& item) {
                    return position >= item.heading_range.begin &&
                        position <= item.heading_range.end;
                });
            if (found != self->folds_->items().end())
                static_cast<void>(self->folds_->toggle(found->node_id));
        } else if (notification && notification->nmhdr.hwndFrom == self->editor_ &&
            notification->nmhdr.code == SCN_UPDATEUI && self->scroll_callback_ &&
            (notification->updated & SC_UPDATE_V_SCROLL) != 0) {
            self->scroll_callback_(
                static_cast<std::uint64_t>(SendEditor(self->editor_, SCI_GETFIRSTVISIBLELINE)),
                static_cast<std::uint64_t>(SendEditor(self->editor_, SCI_GETLINECOUNT)));
        }
    } else if (message == WM_TIMER && w_param == kSynchronizeTimer) {
        static_cast<void>(self->synchronize_now());
        return 0;
    } else if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        self->host_ = nullptr;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

void SourceView::Configure() {
    SendEditor(editor_, SCI_SETCODEPAGE, SC_CP_UTF8);
    SendEditor(editor_, SCI_SETWRAPMODE, SC_WRAP_WORD);
    SendEditor(editor_, SCI_SETMARGINWIDTHN, 0, 48);
    SendEditor(editor_, SCI_SETMARGINTYPEN, kFoldMargin, SC_MARGIN_SYMBOL);
    SendEditor(editor_, SCI_SETMARGINMASKN, kFoldMargin,
        (1 << kExpandedMarker) | (1 << kCollapsedMarker));
    SendEditor(editor_, SCI_SETMARGINSENSITIVEN, kFoldMargin, TRUE);
    SendEditor(editor_, SCI_SETMARGINWIDTHN, kFoldMargin, 18);
    SendEditor(editor_, SCI_MARKERDEFINE, kExpandedMarker, SC_MARK_ARROWDOWN);
    SendEditor(editor_, SCI_MARKERDEFINE, kCollapsedMarker, SC_MARK_ARROW);
    SendEditor(editor_, SCI_MARKERSETFORE, kExpandedMarker, background_color_);
    SendEditor(editor_, SCI_MARKERSETBACK, kExpandedMarker, accent_color_);
    SendEditor(editor_, SCI_MARKERSETFORE, kCollapsedMarker, background_color_);
    SendEditor(editor_, SCI_MARKERSETBACK, kCollapsedMarker, accent_color_);
    SendEditor(editor_, SCI_STYLESETFONT, STYLE_DEFAULT,
        reinterpret_cast<LPARAM>("Microsoft YaHei UI"));
    SendEditor(editor_, SCI_STYLESETSIZE, STYLE_DEFAULT, 11);
    SendEditor(editor_, SCI_STYLECLEARALL);
    SendEditor(editor_, SCI_STYLESETFORE, STYLE_DEFAULT, text_color_);
    SendEditor(editor_, SCI_STYLESETBACK, STYLE_DEFAULT, background_color_);
    SendEditor(editor_, SCI_STYLECLEARALL);
    SendEditor(editor_, SCI_STYLESETFORE, 1, accent_color_);
    SendEditor(editor_, SCI_STYLESETBOLD, 1, TRUE);
    SendEditor(editor_, SCI_STYLESETFORE, 2, RGB(85, 70, 110));
    SendEditor(editor_, SCI_STYLESETFONT, 2, reinterpret_cast<LPARAM>("Consolas"));
    SendEditor(editor_, SCI_STYLESETBACK, 2, RGB(245, 245, 245));
    SendEditor(editor_, SCI_STYLESETFORE, 3, RGB(100, 100, 100));
    SendEditor(editor_, SCI_STYLESETITALIC, 3, TRUE);
    SendEditor(editor_, SCI_STYLESETFORE, 4, RGB(155, 65, 65));
    SendEditor(editor_, SCI_INDICSETSTYLE, kErrorIndicator, INDIC_SQUIGGLE);
    SendEditor(editor_, SCI_INDICSETFORE, kErrorIndicator, RGB(210, 40, 40));
}

void SourceView::apply_appearance(COLORREF text, COLORREF background,
                                  COLORREF accent, UINT dpi) {
    text_color_ = text; background_color_ = background; accent_color_ = accent;
    dpi_ = dpi ? dpi : 96;
    if (!editor_) return;
    SendEditor(editor_, SCI_STYLESETFORE, STYLE_DEFAULT, text_color_);
    SendEditor(editor_, SCI_STYLESETBACK, STYLE_DEFAULT, background_color_);
    SendEditor(editor_, SCI_STYLECLEARALL);
    SendEditor(editor_, SCI_STYLESETFORE, 1, accent_color_);
    SendEditor(editor_, SCI_STYLESETBOLD, 1, TRUE);
    SendEditor(editor_, SCI_STYLESETFORE, 2, text_color_);
    SendEditor(editor_, SCI_STYLESETFONT, 2, reinterpret_cast<LPARAM>("Consolas"));
    SendEditor(editor_, SCI_STYLESETFORE, 3, text_color_);
    SendEditor(editor_, SCI_STYLESETITALIC, 3, TRUE);
    SendEditor(editor_, SCI_STYLESETFORE, 4, accent_color_);
    SendEditor(editor_, SCI_MARKERSETFORE, kExpandedMarker, background_color_);
    SendEditor(editor_, SCI_MARKERSETBACK, kExpandedMarker, accent_color_);
    SendEditor(editor_, SCI_MARKERSETFORE, kCollapsedMarker, background_color_);
    SendEditor(editor_, SCI_MARKERSETBACK, kCollapsedMarker, accent_color_);
    ApplyStyles();
    SendEditor(editor_, SCI_SETCARETFORE, text_color_);
    InvalidateRect(editor_, nullptr, TRUE);
}

void SourceView::apply_heading_folds() {
    if (!editor_) return;
    const auto line_count = static_cast<int>(SendEditor(editor_, SCI_GETLINECOUNT));
    if (line_count > 0) SendEditor(editor_, SCI_SHOWLINES, 0, line_count - 1);
    SendEditor(editor_, SCI_MARKERDELETEALL, kExpandedMarker);
    SendEditor(editor_, SCI_MARKERDELETEALL, kCollapsedMarker);
    if (!folds_) return;
    const auto source_length = static_cast<std::uint64_t>(SendEditor(editor_, SCI_GETLENGTH));
    for (const auto& item : folds_->items()) {
        if (item.heading_range.begin > source_length) continue;
        const auto heading_line = static_cast<int>(SendEditor(editor_, SCI_LINEFROMPOSITION,
            static_cast<WPARAM>(item.heading_range.begin)));
        SendEditor(editor_, SCI_MARKERADD, heading_line,
            item.collapsed ? kCollapsedMarker : kExpandedMarker);
        if (!item.collapsed || item.body_range.end <= item.body_range.begin) continue;
        auto first = heading_line + 1;
        auto end_position = (std::min)(item.body_range.end, source_length);
        auto last = static_cast<int>(SendEditor(editor_, SCI_LINEFROMPOSITION,
            static_cast<WPARAM>(end_position)));
        if (end_position < source_length) --last;
        if (first <= last) SendEditor(editor_, SCI_HIDELINES, first, last);
    }
}

bool SourceView::toggle_heading_fold_at_caret() {
    if (!editor_ || !folds_) return false;
    const auto caret = static_cast<std::uint64_t>(SendEditor(editor_, SCI_GETCURRENTPOS));
    return folds_->toggle_at(caret);
}

void SourceView::ApplyStyles() {
    const auto source = ReadSource();
    SendEditor(editor_, SCI_STARTSTYLING, 0);
    SendEditor(editor_, SCI_SETSTYLING, source.size(), 0);
    bool fenced{};
    for (std::size_t begin = 0; begin < source.size();) {
        auto end = source.find('\n', begin);
        if (end == std::string::npos) end = source.size();
        const auto line = std::string_view(source).substr(begin, end - begin);
        int style{};
        if (line.starts_with("```") || line.starts_with("~~~")) {
            style = 2;
            fenced = !fenced;
        } else if (fenced) style = 2;
        else if (!line.empty() && line.front() == '#') style = 1;
        else if (!line.empty() && line.front() == '>') style = 3;
        else if (line.starts_with("- ") || line.starts_with("* ") ||
                 line.starts_with("+ ")) style = 4;
        if (style != 0) {
            SendEditor(editor_, SCI_STARTSTYLING, begin);
            SendEditor(editor_, SCI_SETSTYLING, end - begin, style);
        }
        begin = end < source.size() ? end + 1 : source.size();
    }
}

void SourceView::ApplyDiagnostics() {
    const auto length = static_cast<WPARAM>(SendEditor(editor_, SCI_GETTEXTLENGTH));
    SendEditor(editor_, SCI_SETINDICATORCURRENT, kErrorIndicator);
    SendEditor(editor_, SCI_INDICATORCLEARRANGE, 0, length);
    for (const auto& diagnostic : sync_.diagnostics()) {
        const auto begin = (std::min)(diagnostic.begin, static_cast<std::uint64_t>(length));
        const auto end = (std::min)((std::max)(diagnostic.end, begin + 1),
                                    static_cast<std::uint64_t>(length));
        if (end > begin) SendEditor(editor_, SCI_INDICATORFILLRANGE,
            static_cast<WPARAM>(begin), static_cast<LPARAM>(end - begin));
    }
}

void SourceView::ScheduleSynchronize() {
    const auto now = GetTickCount64();
    if (pending_since_ == 0) pending_since_ = now;
    const auto elapsed = now - pending_since_;
    const auto delay = elapsed >= kMaximumLatencyMilliseconds
        ? 1U : (std::min)(kDebounceMilliseconds,
              static_cast<UINT>(kMaximumLatencyMilliseconds - elapsed));
    SetTimer(host_, kSynchronizeTimer, delay, nullptr);
}

std::string SourceView::ReadSource() const {
    const auto length = static_cast<std::size_t>(SendEditor(editor_, SCI_GETTEXTLENGTH));
    std::string source(length + 1, '\0');
    SendEditor(editor_, SCI_GETTEXT, length + 1, reinterpret_cast<LPARAM>(source.data()));
    source.resize(length);
    return source;
}

}  // namespace markdownmay::editor
