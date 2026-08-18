#include "markdownmay/ui/outline_view.hpp"

#include <commctrl.h>

#include <array>
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
             std::vector<OutlineView::Item>& output) {
    if (node.kind == document::NodeKind::heading) {
        const auto* heading = std::get_if<document::HeadingAttributes>(&node.attributes);
        std::string visible;
        AppendVisibleText(node, visible);
        const auto level = heading
            ? (std::max)(std::uint8_t{1}, heading->level) : std::uint8_t{1};
        auto label = visible.empty() ? std::wstring(L"（无标题）") : ToWide(visible);
        output.push_back({node.source.begin, level, std::move(label)});
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
    handle_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL |
            TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
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
    TreeView_DeleteAllItems(handle_);
    items_.clear();
    int horizontal_extent = 0;
    const auto snapshot = session_.snapshot();
    if (snapshot.semantic && snapshot.parsed_revision == snapshot.source_revision) {
        Collect(*snapshot.semantic->root(), items_);
        std::array<HTREEITEM, 6> parents{};
        for (std::size_t index = 0; index < items_.size(); ++index) {
            auto& item = items_[index];
            const auto level_index = static_cast<std::size_t>((std::clamp)(
                item.level, std::uint8_t{1}, std::uint8_t{6}) - 1);
            HTREEITEM parent = TVI_ROOT;
            for (std::size_t ancestor = level_index; ancestor > 0; --ancestor) {
                if (parents[ancestor - 1]) { parent = parents[ancestor - 1]; break; }
            }
            TVINSERTSTRUCTW insertion{};
            insertion.hParent = parent;
            insertion.hInsertAfter = TVI_LAST;
            insertion.item.mask = TVIF_TEXT | TVIF_PARAM;
            insertion.item.pszText = item.label.data();
            insertion.item.lParam = static_cast<LPARAM>(item.source_offset);
            const auto tree_item = TreeView_InsertItem(handle_, &insertion);
            if (parent != TVI_ROOT) TreeView_Expand(handle_, parent, TVE_EXPAND);
            parents[level_index] = tree_item;
            for (std::size_t deeper = level_index + 1; deeper < parents.size(); ++deeper)
                parents[deeper] = nullptr;
            HDC dc = GetDC(handle_);
            if (dc) {
                const auto old = SelectObject(dc, font_ ? font_ : GetStockObject(DEFAULT_GUI_FONT));
                SIZE size{};
                GetTextExtentPoint32W(dc, item.label.c_str(),
                    static_cast<int>(item.label.size()), &size);
                SelectObject(dc, old);
                ReleaseDC(handle_, dc);
                horizontal_extent = (std::max)(horizontal_extent,
                    static_cast<int>(size.cx) + MulDiv(20, dpi_, 96));
            }
        }
        if (const auto root = TreeView_GetRoot(handle_))
            TreeView_Expand(handle_, root, TVE_EXPAND);
    }
    static_cast<void>(horizontal_extent);
    if (items_.empty()) {
        const wchar_t* message = snapshot.semantic ? L"当前文档没有标题" : L"大纲暂不可用";
        TVINSERTSTRUCTW insertion{};
        insertion.hParent = TVI_ROOT;
        insertion.hInsertAfter = TVI_LAST;
        insertion.item.mask = TVIF_TEXT | TVIF_PARAM;
        insertion.item.pszText = const_cast<wchar_t*>(message);
        insertion.item.lParam = -1;
        TreeView_InsertItem(handle_, &insertion);
    }
    SendMessageW(handle_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(handle_, nullptr, TRUE);
}

bool OutlineView::handle_control(HWND control, std::uint16_t notification) {
    static_cast<void>(control);
    static_cast<void>(notification);
    return false;
}

bool OutlineView::handle_notify(const NMHDR& notification) {
    if (notification.hwndFrom != handle_ ||
        (notification.code != TVN_SELCHANGEDW && notification.code != TVN_SELCHANGEDA))
        return false;
    const auto& changed = reinterpret_cast<const NMTREEVIEWW&>(notification);
    TVITEMW item{};
    item.mask = TVIF_PARAM;
    item.hItem = changed.itemNew.hItem;
    if (!item.hItem || !TreeView_GetItem(handle_, &item)) return true;
    if (item.lParam < 0) return true;
    return modes_.navigate_to_source(static_cast<std::uint64_t>(item.lParam)) ==
        ErrorCode::ok;
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
