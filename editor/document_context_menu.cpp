#include "markdownmay/editor/document_context_menu.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace markdownmay::editor {
namespace {

constexpr UINT kFirstCommand = 7301;

struct Item final {
    DocumentContextCommand command;
    std::wstring_view label;
    std::wstring_view shortcut;
    bool enabled;
};

struct ActiveMenu final {
    UINT dpi{96};
    COLORREF text{};
    COLORREF background{};
    std::array<Item, 7> items{};
};

thread_local ActiveMenu* active_menu{};

int Scale(int value, UINT dpi) { return MulDiv(value, static_cast<int>(dpi), 96); }

void DrawIcon(HDC dc, DocumentContextCommand command, RECT box, COLORREF color) {
    const auto pen = CreatePen(PS_SOLID, (std::max)(1, Scale(1, active_menu->dpi)), color);
    const auto old_pen = SelectObject(dc, pen);
    const auto old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    const int cx = (box.left + box.right) / 2;
    const int cy = (box.top + box.bottom) / 2;
    const int r = Scale(7, active_menu->dpi);
    switch (command) {
    case DocumentContextCommand::undo:
    case DocumentContextCommand::redo: {
        const bool reverse = command == DocumentContextCommand::redo;
        Arc(dc, cx - r, cy - r, cx + r, cy + r,
            reverse ? cx + r : cx - r, cy, reverse ? cx : cx - r / 2, cy - r);
        MoveToEx(dc, reverse ? cx + r : cx - r, cy, nullptr);
        LineTo(dc, reverse ? cx + r - Scale(4, active_menu->dpi) : cx - r + Scale(4, active_menu->dpi), cy - Scale(4, active_menu->dpi));
        break;
    }
    case DocumentContextCommand::cut:
        MoveToEx(dc, cx - r, cy - r, nullptr); LineTo(dc, cx + r, cy + r);
        MoveToEx(dc, cx + r, cy - r, nullptr); LineTo(dc, cx - r, cy + r);
        Ellipse(dc, cx - r - 2, cy + r - 2, cx - r + 4, cy + r + 4);
        Ellipse(dc, cx + r - 4, cy + r - 2, cx + r + 2, cy + r + 4);
        break;
    case DocumentContextCommand::copy:
        Rectangle(dc, cx - r, cy - r, cx + Scale(4, active_menu->dpi), cy + Scale(5, active_menu->dpi));
        Rectangle(dc, cx - Scale(4, active_menu->dpi), cy - Scale(4, active_menu->dpi), cx + r, cy + r);
        break;
    case DocumentContextCommand::paste:
        Rectangle(dc, cx - r, cy - Scale(5, active_menu->dpi), cx + r, cy + r);
        Rectangle(dc, cx - Scale(4, active_menu->dpi), cy - r, cx + Scale(4, active_menu->dpi), cy - Scale(3, active_menu->dpi));
        break;
    case DocumentContextCommand::remove:
        Rectangle(dc, cx - Scale(5, active_menu->dpi), cy - Scale(5, active_menu->dpi), cx + Scale(5, active_menu->dpi), cy + r);
        MoveToEx(dc, cx - r, cy - Scale(7, active_menu->dpi), nullptr); LineTo(dc, cx + r, cy - Scale(7, active_menu->dpi));
        break;
    case DocumentContextCommand::select_all:
        Rectangle(dc, cx - r, cy - r, cx + r, cy + r);
        Rectangle(dc, cx - Scale(3, active_menu->dpi), cy - Scale(3, active_menu->dpi), cx + Scale(3, active_menu->dpi), cy + Scale(3, active_menu->dpi));
        break;
    }
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

}  // namespace

bool ShowDocumentContextMenu(HWND owner, POINT screen_point, UINT dpi,
        COLORREF text, COLORREF background, const DocumentContextMenuState& state,
        const DocumentContextCommandHandler& handler) {
    if (!owner || !handler) return false;
    ActiveMenu menu_data{dpi ? dpi : 96, text, background, {{
        {DocumentContextCommand::undo, L"撤销", L"Ctrl+Z", state.undo},
        {DocumentContextCommand::redo, L"重做", L"Ctrl+Y", state.redo},
        {DocumentContextCommand::cut, L"剪切", L"Ctrl+X", state.cut},
        {DocumentContextCommand::copy, L"复制", L"Ctrl+C", state.copy},
        {DocumentContextCommand::paste, L"粘贴", L"Ctrl+V", state.paste},
        {DocumentContextCommand::remove, L"删除", L"Delete", state.remove},
        {DocumentContextCommand::select_all, L"全选", L"Ctrl+A", state.select_all},
    }}};
    const auto popup = CreatePopupMenu();
    if (!popup) return false;
    active_menu = &menu_data;
    for (std::size_t index = 0; index < menu_data.items.size(); ++index) {
        if (index == 2 || index == 6) AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
        const auto flags = MF_OWNERDRAW |
            (menu_data.items[index].enabled ? MF_ENABLED : MF_GRAYED);
        AppendMenuW(popup, flags, kFirstCommand + static_cast<UINT>(index),
            reinterpret_cast<LPCWSTR>(&menu_data.items[index]));
    }
    const auto selected = static_cast<UINT>(TrackPopupMenuEx(popup,
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
        screen_point.x, screen_point.y, owner, nullptr));
    active_menu = nullptr;
    DestroyMenu(popup);
    if (selected >= kFirstCommand && selected < kFirstCommand + menu_data.items.size())
        handler(menu_data.items[selected - kFirstCommand].command);
    return true;
}

bool HandleDocumentContextMenuMessage(UINT message, WPARAM, LPARAM l_param,
                                      LRESULT& result) {
    if (!active_menu) return false;
    if (message == WM_MEASUREITEM) {
        auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(l_param);
        if (!measure || measure->CtlType != ODT_MENU || !measure->itemData) return false;
        measure->itemWidth = Scale(304, active_menu->dpi);
        measure->itemHeight = Scale(44, active_menu->dpi);
        result = TRUE;
        return true;
    }
    if (message != WM_DRAWITEM) return false;
    auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
    if (!draw || draw->CtlType != ODT_MENU || !draw->itemData) return false;
    const auto* item = reinterpret_cast<const Item*>(draw->itemData);
    const bool selected = (draw->itemState & ODS_SELECTED) != 0;
    const bool disabled = (draw->itemState & ODS_DISABLED) != 0;
    const auto accent = GetSysColor(COLOR_HIGHLIGHT);
    const auto fill = selected ? accent : active_menu->background;
    const auto foreground = disabled ? GetSysColor(COLOR_GRAYTEXT)
        : selected ? GetSysColor(COLOR_HIGHLIGHTTEXT) : active_menu->text;
    const auto brush = CreateSolidBrush(fill);
    FillRect(draw->hDC, &draw->rcItem, brush);
    DeleteObject(brush);
    RECT icon{draw->rcItem.left + Scale(16, active_menu->dpi), draw->rcItem.top,
        draw->rcItem.left + Scale(44, active_menu->dpi), draw->rcItem.bottom};
    DrawIcon(draw->hDC, item->command, icon, foreground);
    SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC, foreground);
    auto font = CreateFontW(-Scale(16, active_menu->dpi), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    const auto old_font = SelectObject(draw->hDC, font);
    RECT label{draw->rcItem.left + Scale(58, active_menu->dpi), draw->rcItem.top,
        draw->rcItem.right - Scale(82, active_menu->dpi), draw->rcItem.bottom};
    DrawTextW(draw->hDC, item->label.data(), static_cast<int>(item->label.size()),
        &label, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
    RECT shortcut{label.right, draw->rcItem.top,
        draw->rcItem.right - Scale(18, active_menu->dpi), draw->rcItem.bottom};
    DrawTextW(draw->hDC, item->shortcut.data(), static_cast<int>(item->shortcut.size()),
        &shortcut, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
    SelectObject(draw->hDC, old_font);
    DeleteObject(font);
    result = TRUE;
    return true;
}

}  // namespace markdownmay::editor
