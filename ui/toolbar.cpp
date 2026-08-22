#include "markdownmay/ui/toolbar.hpp"

#include <dwmapi.h>

#include <commctrl.h>
#include <uxtheme.h>

#include <array>
#include <algorithm>
#include <utility>

namespace markdownmay::ui {
namespace {
void CALLBACK RoundHeadingMenu(HWINEVENTHOOK, DWORD, HWND window, LONG, LONG,
                               DWORD, DWORD) {
    if (!window) return;
    constexpr DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
    static_cast<void>(DwmSetWindowAttribute(window,
        DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference)));
    RECT bounds{};
    if (!GetWindowRect(window, &bounds)) return;
    const auto radius = MulDiv(10, static_cast<int>(GetDpiForWindow(window)), 96);
    const auto region = CreateRoundRectRgn(0, 0, bounds.right - bounds.left + 1,
        bounds.bottom - bounds.top + 1, radius, radius);
    if (region && !SetWindowRgn(window, region, TRUE)) DeleteObject(region);
}
constexpr int kHeadingComboWidth = 112;
constexpr UINT kHeadingMenuFirst = 9200;
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
    case app::CommandId::view_style_yuan_lang: return L"Aa";
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

    constexpr std::array<ButtonDefinition, 16> definitions{{
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
        {app::CommandId::view_style_yuan_lang, Icon(app::CommandId::view_style_yuan_lang), BTNS_BUTTON},
        {app::CommandId::edit_find, Icon(app::CommandId::edit_find), BTNS_BUTTON},
        {app::CommandId::tools_settings, Icon(app::CommandId::tools_settings), BTNS_BUTTON},
    }};
    std::array<TBBUTTON, definitions.size() + 5> buttons{};
    std::size_t output{};
    buttons[output].iBitmap = I_IMAGENONE;
    buttons[output].idCommand = Native(app::CommandId::format_body);
    buttons[output].fsState = TBSTATE_ENABLED;
    buttons[output].fsStyle = BTNS_BUTTON;
    ++output;
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        if (index == 4 || index == 10 || index == 14 || index == 15) {
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
    TBBUTTONINFO heading_info{sizeof(heading_info), TBIF_SIZE};
    heading_info.cx = static_cast<WORD>(MulDiv(kHeadingComboWidth, dpi_, 96));
    SendMessageW(handle_, TB_SETBUTTONINFOW, Native(app::CommandId::format_body),
        reinterpret_cast<LPARAM>(&heading_info));
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
        app::CommandId::view_style_yuan_lang, app::CommandId::tools_settings};
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
    heading_level_ = static_cast<std::uint8_t>(selected);
    const auto body = query_(app::CommandId::format_body);
    SendMessageW(handle_, TB_ENABLEBUTTON, Native(app::CommandId::format_body),
        MAKELONG(body.enabled, 0));
    InvalidateRect(handle_, nullptr, FALSE);
}

bool Toolbar::handle_control(std::uint16_t identifier,
        std::uint16_t notification, HWND control) {
    static_cast<void>(notification);
    if (control != handle_) return false;
    if (identifier == Native(app::CommandId::view_style_yuan_lang)) {
        HMENU menu = CreatePopupMenu();
        if (!menu) return true;
        constexpr std::array labels{L"宋盈", L"元朗", L"明正", L"清晰"};
        for (std::size_t index = 0; index < labels.size(); ++index) {
            const auto command = static_cast<app::CommandId>(
                static_cast<std::uint16_t>(app::CommandId::view_style_song_ying) + index);
            AppendMenuW(menu, MF_STRING | (query_(command).checked ? MF_CHECKED : 0),
                static_cast<UINT>(command), labels[index]);
        }
        RECT bounds{};
        SendMessageW(handle_, TB_GETRECT, Native(app::CommandId::view_style_yuan_lang),
            reinterpret_cast<LPARAM>(&bounds));
        MapWindowPoints(handle_, HWND_DESKTOP, reinterpret_cast<POINT*>(&bounds), 2);
        const auto selected = TrackPopupMenuEx(menu, TPM_LEFTALIGN | TPM_TOPALIGN |
            TPM_RETURNCMD, bounds.left, bounds.bottom, GetParent(handle_), nullptr);
        DestroyMenu(menu);
        if (selected) execute_(static_cast<app::CommandId>(selected));
        refresh();
        return true;
    }
    if (identifier != Native(app::CommandId::format_body)) return false;
    if (!execute_ || !query_(app::CommandId::format_body).enabled) return true;
    HMENU menu = CreatePopupMenu();
    if (!menu) return true;
    constexpr std::array labels{L"正文", L"一级标题", L"二级标题", L"三级标题",
        L"四级标题", L"五级标题", L"六级标题"};
    for (std::size_t index = 0; index < labels.size(); ++index)
        AppendMenuW(menu, MF_OWNERDRAW | (index == heading_level_ ? MF_CHECKED : 0),
            kHeadingMenuFirst + static_cast<UINT>(index), labels[index]);
    RECT bounds{};
    SendMessageW(handle_, TB_GETRECT, Native(app::CommandId::format_body),
        reinterpret_cast<LPARAM>(&bounds));
    MapWindowPoints(handle_, HWND_DESKTOP, reinterpret_cast<POINT*>(&bounds), 2);
    const auto popup_hook = SetWinEventHook(EVENT_SYSTEM_MENUPOPUPSTART,
        EVENT_SYSTEM_MENUPOPUPSTART, nullptr, RoundHeadingMenu,
        GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
    const auto selected = TrackPopupMenuEx(menu, TPM_LEFTALIGN | TPM_TOPALIGN |
        TPM_RETURNCMD, bounds.left, bounds.bottom, GetParent(handle_), nullptr);
    if (popup_hook) UnhookWinEvent(popup_hook);
    DestroyMenu(menu);
    if (selected >= kHeadingMenuFirst && selected < kHeadingMenuFirst + labels.size()) {
        const auto level = selected - kHeadingMenuFirst;
        execute_(level == 0 ? app::CommandId::format_body :
            static_cast<app::CommandId>(static_cast<std::uint16_t>(
                app::CommandId::format_heading1) + level - 1));
    }
    refresh();
    return true;
}

bool Toolbar::measure_heading_menu(MEASUREITEMSTRUCT& item) const {
    if (item.CtlType != ODT_MENU || item.itemID < kHeadingMenuFirst ||
        item.itemID >= kHeadingMenuFirst + 7) return false;
    item.itemWidth = MulDiv(122, static_cast<int>(dpi_), 96);
    item.itemHeight = MulDiv(32, static_cast<int>(dpi_), 96);
    return true;
}

bool Toolbar::draw_heading_menu(const DRAWITEMSTRUCT& item) const {
    if (item.CtlType != ODT_MENU || item.itemID < kHeadingMenuFirst ||
        item.itemID >= kHeadingMenuFirst + 7) return false;
    const bool selected = (item.itemState & ODS_SELECTED) != 0;
    const bool checked = (item.itemState & ODS_CHECKED) != 0;
    const bool dark = IsDark(background_color_);
    const auto fill_color = selected
        ? (dark ? RGB(62, 62, 65) : RGB(235, 235, 235)) : background_color_;
    const auto surface = CreateSolidBrush(background_color_);
    FillRect(item.hDC, &item.rcItem, surface);
    DeleteObject(surface);
    if (selected) {
        RECT highlight = item.rcItem;
        InflateRect(&highlight, -MulDiv(3, dpi_, 96), -MulDiv(2, dpi_, 96));
        const auto fill = CreateSolidBrush(fill_color);
        FillRounded(item.hDC, highlight, MulDiv(7, dpi_, 96), fill);
        DeleteObject(fill);
    }
    const auto old_font = SelectObject(item.hDC, font_);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, text_color_);
    if (checked) {
        RECT check = item.rcItem;
        check.left += MulDiv(9, dpi_, 96);
        check.right = check.left + MulDiv(12, dpi_, 96);
        DrawTextW(item.hDC, L"✓", -1, &check,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    RECT text = item.rcItem;
    text.left += MulDiv(30, dpi_, 96);
    text.right -= MulDiv(12, dpi_, 96);
    DrawTextW(item.hDC, reinterpret_cast<const wchar_t*>(item.itemData), -1,
        &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(item.hDC, old_font);
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
    if (command == app::CommandId::format_body) {
        constexpr std::array labels{L"正文", L"一级标题", L"二级标题", L"三级标题",
            L"四级标题", L"五级标题", L"六级标题"};
        const auto old = SelectObject(draw.nmcd.hdc, font_);
        SetTextColor(draw.nmcd.hdc, disabled ? RGB(150, 150, 150) : text_color_);
        RECT label_rect = rect;
        label_rect.left += MulDiv(8, dpi_, 96);
        label_rect.right -= MulDiv(22, dpi_, 96);
        DrawTextW(draw.nmcd.hdc, labels[(std::min<std::size_t>)(heading_level_, 6)], -1,
            &label_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        const auto pen = CreatePen(PS_SOLID, 1, disabled ? RGB(150,150,150) : text_color_);
        const auto old_pen = SelectObject(draw.nmcd.hdc, pen);
        const auto x = rect.right - MulDiv(12, dpi_, 96);
        const auto y = (rect.top + rect.bottom) / 2;
        MoveToEx(draw.nmcd.hdc, x - 3, y - 1, nullptr);
        LineTo(draw.nmcd.hdc, x, y + 2); LineTo(draw.nmcd.hdc, x + 3, y - 1);
        SelectObject(draw.nmcd.hdc, old_pen); DeleteObject(pen);
        SelectObject(draw.nmcd.hdc, old);
        return CDRF_SKIPDEFAULT;
    }
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

LRESULT CALLBACK Toolbar::SubclassProcedure(HWND window, UINT message,
        WPARAM w_param, LPARAM l_param, UINT_PTR id, DWORD_PTR data) {
    auto* self = reinterpret_cast<Toolbar*>(data);
    if (self && message == WM_COMMAND && self->handle_control(
            LOWORD(w_param), HIWORD(w_param), reinterpret_cast<HWND>(l_param))) return 0;
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, SubclassProcedure, id);
        if (self) {
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
    case app::CommandId::view_style_yuan_lang: return L"渲染风格";
    case app::CommandId::edit_find: return L"查找（Ctrl+F）";
    case app::CommandId::tools_settings: return L"设置（即将提供）";
    default: return L"";
    }
}

}  // namespace markdownmay::ui
