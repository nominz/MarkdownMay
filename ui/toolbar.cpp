#include "markdownmay/ui/toolbar.hpp"

#include <commctrl.h>
#include <uxtheme.h>

#include <array>
#include <algorithm>
#include <utility>

namespace markdownmay::ui {
namespace {
constexpr int kHeadingComboId = 9100;
constexpr int kHeadingComboWidth = 112;
constexpr int Native(app::CommandId command) noexcept {
    return static_cast<int>(command);
}

struct ButtonDefinition final {
    app::CommandId command;
    const wchar_t* icon;
    BYTE style;
};

constexpr const wchar_t* Icon(app::CommandId command) noexcept {
    switch (command) {
    case app::CommandId::format_bold: return L"\xE8DD";
    case app::CommandId::format_italic: return L"\xE8DB";
    case app::CommandId::format_strike: return L"\xE8DC";
    case app::CommandId::format_inline_code: return L"</>";
    case app::CommandId::format_quote: return L"\xE8B1";
    case app::CommandId::format_unordered_list: return L"";
    case app::CommandId::format_ordered_list: return L"";
    case app::CommandId::format_task_list: return L"\xE739";
    case app::CommandId::format_clear: return L"\xE74D";
    case app::CommandId::insert_table: return L"\xE80A";
    case app::CommandId::view_render: return L"\xE7C3";
    case app::CommandId::view_source: return L"\xE943";
    case app::CommandId::view_split: return L"\xE952";
    case app::CommandId::edit_find: return L"\xE721";
    case app::CommandId::tools_settings: return L"\xE713";
    default: return L"";
    }
}

void FillRounded(HDC dc, const RECT& rect, int radius, HBRUSH brush) {
    const auto region = CreateRoundRectRgn(rect.left, rect.top,
        rect.right + 1, rect.bottom + 1, radius, radius);
    if (region) {
        FillRgn(dc, region, brush);
        DeleteObject(region);
    } else {
        FillRect(dc, &rect, brush);
    }
}

bool IsDark(COLORREF color) noexcept {
    return GetRValue(color) + GetGValue(color) + GetBValue(color) < 384;
}

void DrawListIcon(HDC dc, RECT rect, COLORREF color, bool ordered, UINT dpi) {
    const auto pen = CreatePen(PS_SOLID, (std::max)(1, MulDiv(1, dpi, 96)), color);
    const auto old_pen = SelectObject(dc, pen);
    const auto old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    const auto left = rect.left + MulDiv(7, dpi, 96);
    const auto line_left = rect.left + MulDiv(15, dpi, 96);
    const auto line_right = rect.right - MulDiv(6, dpi, 96);
    const auto first_y = rect.top + MulDiv(8, dpi, 96);
    const auto step = MulDiv(6, dpi, 96);
    for (int row = 0; row < 3; ++row) {
        const auto y = first_y + row * step;
        if (ordered) {
            const wchar_t number[2]{static_cast<wchar_t>(L'1' + row), L'\0'};
            RECT number_rect{left - MulDiv(3, dpi, 96), y - MulDiv(4, dpi, 96),
                line_left - MulDiv(2, dpi, 96), y + MulDiv(4, dpi, 96)};
            DrawTextW(dc, number, 1, &number_rect,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        } else {
            const auto radius = (std::max)(1, MulDiv(1, dpi, 96));
            const auto dot = CreateSolidBrush(color);
            const auto old_dot = SelectObject(dc, dot);
            Ellipse(dc, left - radius, y - radius, left + radius + 1, y + radius + 1);
            SelectObject(dc, old_dot);
            DeleteObject(dot);
        }
        MoveToEx(dc, line_left, y, nullptr);
        LineTo(dc, line_right, y);
    }
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

void DrawAssetIcon(HDC dc, RECT rect, COLORREF color, app::CommandId command, UINT dpi) {
    const auto stroke = (std::max)(1, MulDiv(2, dpi, 96));
    const auto pen = CreatePen(PS_SOLID, stroke, color);
    const auto old_pen = SelectObject(dc, pen);
    const auto old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    InflateRect(&rect, -MulDiv(7, dpi, 96), -MulDiv(7, dpi, 96));
    if (command == app::CommandId::view_split) {
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 3, 3);
        const auto middle = (rect.left + rect.right) / 2;
        MoveToEx(dc, middle, rect.top, nullptr); LineTo(dc, middle, rect.bottom);
    } else if (command == app::CommandId::view_source) {
        Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
        const auto y = rect.top + (rect.bottom - rect.top) / 3;
        MoveToEx(dc, rect.left, y, nullptr); LineTo(dc, rect.right, y);
        MoveToEx(dc, rect.left + 4, y + 5, nullptr); LineTo(dc, rect.left + 8, y + 9);
        LineTo(dc, rect.left + 4, y + 13);
        MoveToEx(dc, rect.right - 4, y + 5, nullptr); LineTo(dc, rect.right - 8, y + 9);
        LineTo(dc, rect.right - 4, y + 13);
    } else if (command == app::CommandId::format_quote) {
        const auto brush = CreateSolidBrush(color); SelectObject(dc, brush);
        RoundRect(dc, rect.left, rect.top + 6, (rect.left + rect.right) / 2 - 1,
            rect.bottom - 2, 3, 3);
        RoundRect(dc, (rect.left + rect.right) / 2 + 2, rect.top + 6,
            rect.right, rect.bottom - 2, 3, 3);
        SelectObject(dc, old_brush); DeleteObject(brush);
    } else if (command == app::CommandId::format_task_list) {
        Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
        MoveToEx(dc, rect.left + 3, rect.top + 6, nullptr);
        LineTo(dc, rect.left + 6, rect.top + 9); LineTo(dc, rect.left + 11, rect.top + 3);
        MoveToEx(dc, rect.left + 13, rect.top + 7, nullptr); LineTo(dc, rect.right - 2, rect.top + 7);
        Ellipse(dc, rect.left + 4, rect.top + 13, rect.left + 10, rect.top + 19);
        MoveToEx(dc, rect.left + 13, rect.top + 16, nullptr); LineTo(dc, rect.right - 2, rect.top + 16);
    }
    SelectObject(dc, old_brush); SelectObject(dc, old_pen); DeleteObject(pen);
}
}

Toolbar::Toolbar(Query query, Execute execute)
    : query_(std::move(query)), execute_(std::move(execute)) {}
Toolbar::~Toolbar() {
    if (handle_ && IsWindow(handle_)) RemoveWindowSubclass(handle_, SubclassProcedure, 1);
    if (font_) DeleteObject(font_);
    if (icon_font_) DeleteObject(icon_font_);
}

bool Toolbar::create(HWND parent) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
    static_cast<void>(InitCommonControlsEx(&controls));
    handle_ = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS |
            CCS_NODIVIDER | CCS_NORESIZE,
        0, 0, 0, height_, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!handle_) return false;
    if (!SetWindowSubclass(handle_, SubclassProcedure, 1,
            reinterpret_cast<DWORD_PTR>(this))) return false;
    SendMessageW(handle_, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(handle_, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS);
    const auto tooltip = reinterpret_cast<HWND>(SendMessageW(handle_, TB_GETTOOLTIPS, 0, 0));
    if (tooltip) SetWindowLongPtrW(tooltip, GWL_STYLE,
        GetWindowLongPtrW(tooltip, GWL_STYLE) | TTS_ALWAYSTIP | TTS_NOPREFIX);

    constexpr std::array<ButtonDefinition, 15> definitions{{
        {app::CommandId::format_bold, Icon(app::CommandId::format_bold), BTNS_BUTTON},
        {app::CommandId::format_italic, Icon(app::CommandId::format_italic), BTNS_BUTTON},
        {app::CommandId::format_strike, Icon(app::CommandId::format_strike), BTNS_BUTTON},
        {app::CommandId::format_inline_code, Icon(app::CommandId::format_inline_code), BTNS_BUTTON},
        {app::CommandId::format_quote, Icon(app::CommandId::format_quote), BTNS_BUTTON},
        {app::CommandId::format_unordered_list, Icon(app::CommandId::format_unordered_list), BTNS_BUTTON},
        {app::CommandId::format_ordered_list, Icon(app::CommandId::format_ordered_list), BTNS_BUTTON},
        {app::CommandId::format_task_list, Icon(app::CommandId::format_task_list), BTNS_BUTTON},
        {app::CommandId::format_clear, Icon(app::CommandId::format_clear), BTNS_BUTTON},
        {app::CommandId::insert_table, Icon(app::CommandId::insert_table), BTNS_BUTTON},
        {app::CommandId::view_render, Icon(app::CommandId::view_render), BTNS_CHECKGROUP},
        {app::CommandId::view_source, Icon(app::CommandId::view_source), BTNS_CHECKGROUP},
        {app::CommandId::view_split, Icon(app::CommandId::view_split), BTNS_CHECKGROUP},
        {app::CommandId::edit_find, Icon(app::CommandId::edit_find), BTNS_BUTTON},
        {app::CommandId::tools_settings, Icon(app::CommandId::tools_settings), BTNS_BUTTON},
    }};
    std::array<TBBUTTON, definitions.size() + 5> buttons{};
    std::size_t output{};
    buttons[output].iBitmap = MulDiv(kHeadingComboWidth, dpi_, 96);
    buttons[output].fsStyle = BTNS_SEP;
    ++output;
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        if (index == 4 || index == 10 || index == 13 || index == 14) {
            buttons[output].fsStyle = BTNS_SEP;
            buttons[output].iBitmap = index == 12 ? MulDiv(120, dpi_, 96) : MulDiv(8, dpi_, 96);
            ++output;
        }
        const auto& definition = definitions[index];
        buttons[output].iBitmap = I_IMAGENONE;
        buttons[output].idCommand = Native(definition.command);
        buttons[output].fsState = TBSTATE_ENABLED;
        buttons[output].fsStyle = definition.style;
        buttons[output].iString = 0;
        ++output;
    }
    if (!SendMessageW(handle_, TB_ADDBUTTONSW, output,
            reinterpret_cast<LPARAM>(buttons.data()))) return false;
    heading_combo_ = CreateWindowExW(0, WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED |
            CBS_HASSTRINGS | WS_VSCROLL,
        MulDiv(6, dpi_, 96), MulDiv(3, dpi_, 96),
        MulDiv(kHeadingComboWidth - 10, dpi_, 96), MulDiv(240, dpi_, 96),
        handle_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kHeadingComboId)),
        GetModuleHandleW(nullptr), nullptr);
    if (!heading_combo_) return false;
    for (const auto* label : {L"正文", L"一级标题", L"二级标题", L"三级标题",
            L"四级标题", L"五级标题", L"六级标题"})
        SendMessageW(heading_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    SendMessageW(heading_combo_, CB_SETCURSEL, 0, 0);
    SendMessageW(heading_combo_, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), MulDiv(28, dpi_, 96));
    SendMessageW(heading_combo_, CB_SETITEMHEIGHT, 0, MulDiv(34, dpi_, 96));
    SendMessageW(heading_combo_, CB_SETDROPPEDWIDTH, MulDiv(180, dpi_, 96), 0);
    SendMessageW(handle_, TB_AUTOSIZE, 0, 0);
    SendMessageW(handle_, TB_SETBUTTONSIZE, 0,
        MAKELPARAM(MulDiv(32, dpi_, 96), MulDiv(30, dpi_, 96)));
    RECT bounds{};
    GetWindowRect(handle_, &bounds);
    height_ = bounds.bottom - bounds.top;
    refresh();
    return true;
}

void Toolbar::resize(int width, int top) {
    if (handle_) MoveWindow(handle_, 0, top, width, height_, TRUE);
    if (heading_combo_) SetWindowPos(heading_combo_, HWND_TOP, MulDiv(6, dpi_, 96),
        MulDiv(3, dpi_, 96), MulDiv(kHeadingComboWidth - 10, dpi_, 96),
        MulDiv(240, dpi_, 96), SWP_SHOWWINDOW | SWP_NOACTIVATE);
    if (heading_combo_) {
        const auto radius = MulDiv(8, dpi_, 96);
        const auto region = CreateRoundRectRgn(0, 0, MulDiv(kHeadingComboWidth - 10, dpi_, 96) + 1,
            MulDiv(30, dpi_, 96) + 1, radius, radius);
        if (region && !SetWindowRgn(heading_combo_, region, TRUE)) DeleteObject(region);
    }
}

void Toolbar::refresh() {
    if (!handle_ || !query_) return;
    constexpr std::array commands{
        app::CommandId::format_bold, app::CommandId::format_italic,
        app::CommandId::format_strike, app::CommandId::format_inline_code,
        app::CommandId::format_quote, app::CommandId::format_unordered_list,
        app::CommandId::format_ordered_list, app::CommandId::format_task_list,
        app::CommandId::format_clear, app::CommandId::insert_table,
        app::CommandId::view_render, app::CommandId::view_source,
        app::CommandId::view_split, app::CommandId::edit_find,
        app::CommandId::tools_settings};
    for (const auto command : commands) {
        const auto state = query_(command);
        SendMessageW(handle_, TB_ENABLEBUTTON, Native(command),
            MAKELONG(state.enabled, 0));
        SendMessageW(handle_, TB_CHECKBUTTON, Native(command),
            MAKELONG(state.checked, 0));
    }
    int selected = 0;
    for (int level = 1; level <= 6; ++level) {
        const auto command = static_cast<app::CommandId>(
            static_cast<std::uint16_t>(app::CommandId::format_heading1) + level - 1);
        if (query_(command).checked) { selected = level; break; }
    }
    if (heading_combo_) {
        SendMessageW(heading_combo_, CB_SETCURSEL, selected, 0);
        EnableWindow(heading_combo_, query_(app::CommandId::format_body).enabled);
    }
}

bool Toolbar::handle_control(std::uint16_t identifier,
        std::uint16_t notification, HWND control) {
    if (identifier != kHeadingComboId || control != heading_combo_) return false;
    if (notification != CBN_SELCHANGE) return true;
    const auto selected = static_cast<int>(SendMessageW(heading_combo_, CB_GETCURSEL, 0, 0));
    if (selected < 0 || selected > 6 || !execute_) return true;
    const auto command = selected == 0 ? app::CommandId::format_body :
        static_cast<app::CommandId>(
            static_cast<std::uint16_t>(app::CommandId::format_heading1) + selected - 1);
    execute_(command);
    refresh();
    return true;
}

HWND Toolbar::handle() const noexcept { return handle_; }
int Toolbar::height() const noexcept { return height_; }
void Toolbar::apply_appearance(COLORREF text, COLORREF background, UINT dpi) {
    text_color_ = text;
    background_color_ = background;
    dpi_ = dpi;
    height_ = MulDiv(34, static_cast<int>(dpi), 96);
    if (font_) DeleteObject(font_);
    if (icon_font_) DeleteObject(icon_font_);
    font_ = CreateFontW(-MulDiv(9, static_cast<int>(dpi), 72), 0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    icon_font_ = CreateFontW(-MulDiv(12, static_cast<int>(dpi), 72), 0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe Fluent Icons");
    if (!handle_) return;
    SendMessageW(handle_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    if (heading_combo_)
        SendMessageW(heading_combo_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    if (heading_combo_) {
        SetWindowTheme(heading_combo_, IsDark(background_color_) ? L"DarkMode_CFD" : L"Explorer", nullptr);
        InvalidateRect(heading_combo_, nullptr, TRUE);
    }
    SendMessageW(handle_, TB_SETBUTTONSIZE, 0,
        MAKELPARAM(MulDiv(32, dpi_, 96), MulDiv(30, dpi_, 96)));
    SendMessageW(handle_, TB_AUTOSIZE, 0, 0);
    InvalidateRect(handle_, nullptr, TRUE);
}

LRESULT Toolbar::custom_draw(NMTBCUSTOMDRAW& draw) {
    if (draw.nmcd.dwDrawStage == CDDS_PREPAINT) {
        const auto brush = CreateSolidBrush(background_color_);
        FillRect(draw.nmcd.hdc, &draw.nmcd.rc, brush);
        DeleteObject(brush);
        return CDRF_NOTIFYITEMDRAW;
    }
    if (draw.nmcd.dwDrawStage != CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;
    RECT rect = draw.nmcd.rc;
    const auto inset = MulDiv(2, dpi_, 96);
    InflateRect(&rect, -inset, -inset);
    const bool hot = (draw.nmcd.uItemState & CDIS_HOT) != 0;
    const bool checked = (draw.nmcd.uItemState & CDIS_CHECKED) != 0;
    const bool disabled = (draw.nmcd.uItemState & CDIS_DISABLED) != 0;
    const bool dark = IsDark(background_color_);
    const auto fill = checked ? (dark ? RGB(61, 78, 96) : RGB(220, 232, 243)) :
        hot ? (dark ? RGB(62, 62, 65) : RGB(235, 235, 235)) : background_color_;
    auto brush = CreateSolidBrush(fill);
    const auto radius = MulDiv(6, dpi_, 96);
    FillRounded(draw.nmcd.hdc, rect, radius, brush);
    DeleteObject(brush);
    const auto command = static_cast<app::CommandId>(draw.nmcd.dwItemSpec);
    const bool literal = command == app::CommandId::format_inline_code;
    const auto old_font = SelectObject(draw.nmcd.hdc, literal ? font_ : icon_font_);
    SetBkMode(draw.nmcd.hdc, TRANSPARENT);
    SetTextColor(draw.nmcd.hdc, disabled ? RGB(150, 150, 150) : text_color_);
    if (command == app::CommandId::format_unordered_list ||
        command == app::CommandId::format_ordered_list) {
        DrawListIcon(draw.nmcd.hdc, rect,
            disabled ? (dark ? RGB(115, 115, 115) : RGB(150, 150, 150)) : text_color_,
            command == app::CommandId::format_ordered_list, dpi_);
    } else if (command == app::CommandId::format_quote ||
               command == app::CommandId::format_task_list ||
               command == app::CommandId::view_source ||
               command == app::CommandId::view_split) {
        DrawAssetIcon(draw.nmcd.hdc, rect,
            disabled ? (dark ? RGB(115, 115, 115) : RGB(150, 150, 150)) : text_color_,
            command, dpi_);
    } else {
        DrawTextW(draw.nmcd.hdc, Icon(command), -1, &rect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(draw.nmcd.hdc, old_font);
    return CDRF_SKIPDEFAULT;
}

bool Toolbar::draw_combo(const DRAWITEMSTRUCT& draw) {
    if (draw.hwndItem != heading_combo_ || draw.CtlType != ODT_COMBOBOX) return false;
    const bool selected = (draw.itemState & ODS_SELECTED) != 0;
    const bool disabled = (draw.itemState & ODS_DISABLED) != 0;
    const bool dark = IsDark(background_color_);
    const auto fill = selected ? (dark ? RGB(62, 62, 65) : RGB(232, 232, 232)) :
        background_color_;
    const auto brush = CreateSolidBrush(fill);
    FillRect(draw.hDC, &draw.rcItem, brush);
    DeleteObject(brush);
    if (draw.itemID != static_cast<UINT>(-1)) {
        wchar_t label[32]{};
        SendMessageW(heading_combo_, CB_GETLBTEXT, draw.itemID,
            reinterpret_cast<LPARAM>(label));
        RECT text_rect = draw.rcItem;
        text_rect.left += MulDiv(7, dpi_, 96);
        const auto old_font = SelectObject(draw.hDC, font_);
        SetBkMode(draw.hDC, TRANSPARENT);
        SetTextColor(draw.hDC, disabled ? (dark ? RGB(125, 125, 125) : RGB(150, 150, 150)) :
            text_color_);
        DrawTextW(draw.hDC, label, -1, &text_rect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(draw.hDC, old_font);
    }
    if (draw.itemState & ODS_FOCUS) DrawFocusRect(draw.hDC, &draw.rcItem);
    return true;
}

LRESULT CALLBACK Toolbar::SubclassProcedure(HWND window, UINT message,
        WPARAM w_param, LPARAM l_param, UINT_PTR id, DWORD_PTR data) {
    auto* self = reinterpret_cast<Toolbar*>(data);
    if (self && message == WM_COMMAND && self->handle_control(
            LOWORD(w_param), HIWORD(w_param), reinterpret_cast<HWND>(l_param))) return 0;
    if (self && message == WM_DRAWITEM && self->draw_combo(
            *reinterpret_cast<DRAWITEMSTRUCT*>(l_param))) return TRUE;
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, SubclassProcedure, id);
        if (self) {
            self->heading_combo_ = nullptr;
            self->handle_ = nullptr;
        }
    }
    return DefSubclassProc(window, message, w_param, l_param);
}

const wchar_t* Toolbar::tooltip(std::uint16_t command) noexcept {
    switch (static_cast<app::CommandId>(command)) {
    case app::CommandId::format_bold: return L"粗体（Ctrl+B）";
    case app::CommandId::format_italic: return L"斜体（Ctrl+I）";
    case app::CommandId::format_strike: return L"删除线";
    case app::CommandId::format_inline_code: return L"行内代码";
    case app::CommandId::format_quote: return L"引用";
    case app::CommandId::format_unordered_list: return L"无序列表";
    case app::CommandId::format_ordered_list: return L"有序列表";
    case app::CommandId::format_task_list: return L"任务列表";
    case app::CommandId::format_clear: return L"清除当前段落格式（Ctrl+\\）";
    case app::CommandId::insert_table: return L"插入 3×3 表格";
    case app::CommandId::view_render: return L"切换到渲染模式（Ctrl+1）";
    case app::CommandId::view_source: return L"切换到源码模式（Ctrl+2）";
    case app::CommandId::view_split: return L"切换到对照模式（Ctrl+3）";
    case app::CommandId::edit_find: return L"查找（Ctrl+F）";
    case app::CommandId::tools_settings: return L"设置（即将提供）";
    default: return L"";
    }
}

}  // namespace markdownmay::ui
