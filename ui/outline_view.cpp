#include "markdownmay/ui/outline_view.hpp"

#include <algorithm>
#include <string>

namespace markdownmay::ui {
namespace {
void AppendVisibleText(const document::Node& node, std::string& text) {
    if (node.kind == document::NodeKind::text ||
        node.kind == document::NodeKind::inline_code) text += node.text;
    for (const auto& child : node.children) AppendVisibleText(*child, text);
}

std::wstring ToWide(std::string_view text) {
    if (text.empty()) return {};
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return L"（无标题）";
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), result.data(), size);
    return result;
}

void Collect(const document::Node& node,
             std::vector<std::pair<std::wstring, OutlineView::Item>>& output) {
    if (node.kind == document::NodeKind::heading) {
        const auto* heading = std::get_if<document::HeadingAttributes>(&node.attributes);
        std::string visible;
        AppendVisibleText(node, visible);
        const auto level = heading ? (std::max)(std::uint8_t{1}, heading->level) : 1;
        std::wstring label(static_cast<std::size_t>(level - 1) * 2, L' ');
        label += visible.empty() ? L"（无标题）" : ToWide(visible);
        output.push_back({std::move(label), {node.source.begin}});
    }
    for (const auto& child : node.children) Collect(*child, output);
}
}

OutlineView::OutlineView(document::DocumentSession& session,
                         editor::ViewModeController& modes)
    : session_(session), modes_(modes) {
    const std::weak_ptr<int> lifetime(lifetime_);
    session_.subscribe([this, lifetime](const document::DocumentEvent&) {
        if (!lifetime.expired()) refresh();
    });
}

OutlineView::~OutlineView() {
    lifetime_.reset();
    if (font_) DeleteObject(font_);
}

bool OutlineView::create(HWND parent) {
    handle_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY |
            LBS_NOINTEGRALHEIGHT,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(4100),
        GetModuleHandleW(nullptr), nullptr);
    if (!handle_) return false;
    refresh();
    return true;
}

void OutlineView::resize(const RECT& bounds) {
    if (handle_) MoveWindow(handle_, bounds.left, bounds.top,
        bounds.right - bounds.left, bounds.bottom - bounds.top, TRUE);
}

void OutlineView::refresh() {
    if (!handle_) return;
    SendMessageW(handle_, WM_SETREDRAW, FALSE, 0);
    SendMessageW(handle_, LB_RESETCONTENT, 0, 0);
    items_.clear();
    int horizontal_extent = 0;
    const auto snapshot = session_.snapshot();
    if (snapshot.semantic && snapshot.parsed_revision == snapshot.source_revision) {
        std::vector<std::pair<std::wstring, Item>> values;
        Collect(*snapshot.semantic->root(), values);
        for (auto& [label, item] : values) {
            SendMessageW(handle_, LB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(label.c_str()));
            HDC dc = GetDC(handle_);
            if (dc) {
                const auto old = SelectObject(dc, font_ ? font_ : GetStockObject(DEFAULT_GUI_FONT));
                SIZE size{};
                GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &size);
                SelectObject(dc, old);
                ReleaseDC(handle_, dc);
                horizontal_extent = (std::max)(horizontal_extent,
                    static_cast<int>(size.cx) + MulDiv(20, dpi_, 96));
            }
            items_.push_back(item);
        }
    }
    SendMessageW(handle_, LB_SETHORIZONTALEXTENT, horizontal_extent, 0);
    if (items_.empty()) {
        const wchar_t* message = snapshot.semantic ? L"当前文档没有标题" : L"大纲暂不可用";
        SendMessageW(handle_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(message));
    }
    SendMessageW(handle_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(handle_, nullptr, TRUE);
}

bool OutlineView::handle_control(HWND control, std::uint16_t notification) {
    if (control != handle_ || notification != LBN_SELCHANGE) return false;
    const auto selected = static_cast<int>(SendMessageW(handle_, LB_GETCURSEL, 0, 0));
    if (selected >= 0 && static_cast<std::size_t>(selected) < items_.size())
        static_cast<void>(modes_.navigate_to_source(items_[selected].source_offset));
    return true;
}

HWND OutlineView::handle() const noexcept { return handle_; }
int OutlineView::width() const noexcept { return MulDiv(width_, static_cast<int>(dpi_), 96); }
void OutlineView::set_width(int width) noexcept {
    width_ = (std::max)(120, MulDiv(width, 96, static_cast<int>(dpi_)));
}
bool OutlineView::has_headings() const noexcept { return !items_.empty(); }

void OutlineView::apply_appearance(COLORREF, COLORREF, UINT dpi) {
    dpi_ = dpi ? dpi : 96;
    if (font_) DeleteObject(font_);
    font_ = CreateFontW(-MulDiv(10, static_cast<int>(dpi_), 72), 0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
        L"Microsoft YaHei UI");
    if (handle_) SendMessageW(handle_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
}

}  // namespace markdownmay::ui
