#include "markdownmay/ui/menu_controller.hpp"

#include <array>
#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <utility>

namespace markdownmay::ui {
namespace {
constexpr UINT Native(app::CommandId command) noexcept {
    return static_cast<UINT>(command);
}

bool IsKnown(std::uint16_t value) noexcept {
    return (value >= Native(app::CommandId::file_new) &&
            value <= Native(app::CommandId::file_page_setup)) ||
        (value >= Native(app::CommandId::edit_undo) &&
            value <= Native(app::CommandId::edit_replace)) ||
        (value >= Native(app::CommandId::format_bold) &&
            value <= Native(app::CommandId::format_task_list)) ||
        (value >= Native(app::CommandId::view_render) &&
            value <= Native(app::CommandId::view_theme_dark)) ||
        (value >= Native(app::CommandId::tools_register_association) &&
            value <= Native(app::CommandId::tools_default_apps)) ||
        value == Native(app::CommandId::help_about);
}
}

MenuController::MenuController(Query query, Execute execute)
    : query_(std::move(query)), execute_(std::move(execute)) {}

MenuController::~MenuController() {
    if (accelerator_) DestroyAcceleratorTable(accelerator_);
    if (font_) DeleteObject(font_);
    if (menu_) DestroyMenu(menu_);
}

bool MenuController::create(HWND window) {
    window_ = window;
    menu_ = CreateMenu();
    const auto file = CreatePopupMenu();
    const auto edit = CreatePopupMenu();
    const auto format = CreatePopupMenu();
    const auto view = CreatePopupMenu();
    const auto tools = CreatePopupMenu();
    const auto help = CreatePopupMenu();
    recent_menu_ = CreatePopupMenu();
    if (!menu_ || !file || !edit || !format || !view || !tools || !help || !recent_menu_)
        return false;

    AddCommand(file, app::CommandId::file_new, L"新建(&N)\tCtrl+N");
    AddCommand(file, app::CommandId::file_open, L"打开(&O)...\tCtrl+O");
    AddCommand(file, app::CommandId::file_save, L"保存(&S)\tCtrl+S");
    AddCommand(file, app::CommandId::file_save_as, L"另存为(&A)...\tCtrl+Shift+S");
    AddSeparator(file);
    AddPopup(file, recent_menu_, L"最近文件(&R)");
    AddSeparator(file);
    AddCommand(file, app::CommandId::file_print, L"打印(&P)...\tCtrl+P");
    AddCommand(file, app::CommandId::file_page_setup, L"页面设置(&U)...");
    AddSeparator(file);
    AddCommand(file, app::CommandId::file_exit, L"退出(&X)\tAlt+F4");

    AddCommand(edit, app::CommandId::edit_undo, L"撤销(&U)\tCtrl+Z");
    AddCommand(edit, app::CommandId::edit_redo, L"重做(&R)\tCtrl+Y");
    AddSeparator(edit);
    AddCommand(edit, app::CommandId::edit_cut, L"剪切(&T)\tCtrl+X");
    AddCommand(edit, app::CommandId::edit_copy, L"复制(&C)\tCtrl+C");
    AddCommand(edit, app::CommandId::edit_paste, L"粘贴(&P)\tCtrl+V");
    AddCommand(edit, app::CommandId::edit_select_all, L"全选(&A)\tCtrl+A");
    AddSeparator(edit);
    AddCommand(edit, app::CommandId::edit_find, L"查找(&F)...\tCtrl+F");
    AddCommand(edit, app::CommandId::edit_replace, L"替换(&H)...\tCtrl+H");

    AddCommand(format, app::CommandId::format_bold, L"粗体(&B)\tCtrl+B");
    AddCommand(format, app::CommandId::format_italic, L"斜体(&I)\tCtrl+I");
    AddCommand(format, app::CommandId::format_strike, L"删除线(&S)");
    AddCommand(format, app::CommandId::format_inline_code, L"行内代码(&C)");
    AddSeparator(format);
    AddCommand(format, app::CommandId::format_quote, L"引用(&Q)");
    AddCommand(format, app::CommandId::format_unordered_list, L"无序列表(&U)");
    AddCommand(format, app::CommandId::format_ordered_list, L"有序列表(&O)");
    AddCommand(format, app::CommandId::format_task_list, L"任务列表(&T)");

    AddCommand(view, app::CommandId::view_render, L"渲染模式(&R)\tCtrl+1");
    AddCommand(view, app::CommandId::view_source, L"源码模式(&S)\tCtrl+2");
    AddCommand(view, app::CommandId::view_split, L"对照模式(&P)\tCtrl+3");
    AddSeparator(view);
    AddCommand(view, app::CommandId::view_theme_system, L"主题：跟随系统(&Y)");
    AddCommand(view, app::CommandId::view_theme_light, L"主题：浅色(&L)");
    AddCommand(view, app::CommandId::view_theme_dark, L"主题：深色(&D)");
    AddCommand(tools, app::CommandId::tools_register_association,
        L"注册为 Markdown 候选程序(&R)");
    AddCommand(tools, app::CommandId::tools_unregister_association,
        L"撤销文件关联注册(&U)");
    AddSeparator(tools);
    AddCommand(tools, app::CommandId::tools_default_apps,
        L"打开 Windows 默认应用设置(&D)...");
    AddCommand(help, app::CommandId::help_about, L"关于马冬梅(&A)\tF1");

    const std::array top_definitions{
        std::pair{file, L"文件(&F)"}, std::pair{edit, L"编辑(&E)"},
        std::pair{format, L"格式(&O)"}, std::pair{view, L"视图(&V)"},
        std::pair{tools, L"工具(&T)"}, std::pair{help, L"帮助(&H)"}};
    bar_ = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE,
        0, 0, 0, height_, window_, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!bar_) return false;
    for (std::size_t index = 0; index < top_definitions.size(); ++index) {
        AddPopup(menu_, top_definitions[index].first, top_definitions[index].second);
        const auto* label = KeepLabel(top_definitions[index].second);
        const auto button = CreateWindowExW(0, L"BUTTON", label,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_FLAT,
            0, 0, 0, height_, window_,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(9000 + index)),
            GetModuleHandleW(nullptr), nullptr);
        if (!button) return false;
        top_items_.push_back({button, top_definitions[index].first, label, 0});
    }

    const std::array<ACCEL, 17> keys{{
        {FVIRTKEY | FCONTROL, 'N', static_cast<WORD>(app::CommandId::file_new)},
        {FVIRTKEY | FCONTROL, 'O', static_cast<WORD>(app::CommandId::file_open)},
        {FVIRTKEY | FCONTROL, 'S', static_cast<WORD>(app::CommandId::file_save)},
        {FVIRTKEY | FCONTROL | FSHIFT, 'S', static_cast<WORD>(app::CommandId::file_save_as)},
        {FVIRTKEY | FCONTROL, 'P', static_cast<WORD>(app::CommandId::file_print)},
        {FVIRTKEY | FCONTROL, 'Z', static_cast<WORD>(app::CommandId::edit_undo)},
        {FVIRTKEY | FCONTROL, 'Y', static_cast<WORD>(app::CommandId::edit_redo)},
        {FVIRTKEY | FCONTROL, 'X', static_cast<WORD>(app::CommandId::edit_cut)},
        {FVIRTKEY | FCONTROL, 'C', static_cast<WORD>(app::CommandId::edit_copy)},
        {FVIRTKEY | FCONTROL, 'V', static_cast<WORD>(app::CommandId::edit_paste)},
        {FVIRTKEY | FCONTROL, 'A', static_cast<WORD>(app::CommandId::edit_select_all)},
        {FVIRTKEY | FCONTROL, 'B', static_cast<WORD>(app::CommandId::format_bold)},
        {FVIRTKEY | FCONTROL, 'I', static_cast<WORD>(app::CommandId::format_italic)},
        {FVIRTKEY | FCONTROL, '1', static_cast<WORD>(app::CommandId::view_render)},
        {FVIRTKEY | FCONTROL, '2', static_cast<WORD>(app::CommandId::view_source)},
        {FVIRTKEY | FCONTROL, '3', static_cast<WORD>(app::CommandId::view_split)},
        {FVIRTKEY, VK_F1, static_cast<WORD>(app::CommandId::help_about)},
    }};
    accelerator_ = CreateAcceleratorTableW(
        const_cast<ACCEL*>(keys.data()), static_cast<int>(keys.size()));
    refresh();
    return accelerator_ != nullptr;
}

bool MenuController::dispatch(std::uint16_t native_id) {
    const bool recent = native_id >= Native(app::CommandId::recent_first) &&
        native_id <= Native(app::CommandId::recent_clear);
    if (!IsKnown(native_id) && !recent) return false;
    const auto command = static_cast<app::CommandId>(native_id);
    if (query_ && !query_(command).enabled) return true;
    if (execute_) execute_(command);
    refresh();
    return true;
}

void MenuController::set_recent_files(std::vector<std::filesystem::path> files) {
    if (!recent_menu_) return;
    while (GetMenuItemCount(recent_menu_) > 0)
        DeleteMenu(recent_menu_, 0, MF_BYPOSITION);
    if (files.empty()) {
        AppendMenuW(recent_menu_, MF_STRING | MF_GRAYED, 0, L"（没有最近文件）");
    } else {
        for (std::size_t index = 0; index < files.size() && index < 20; ++index) {
            auto label = std::to_wstring(index + 1) + L"  " + files[index].wstring();
            AppendMenuW(recent_menu_, MF_STRING,
                Native(app::CommandId::recent_first) + static_cast<UINT>(index),
                label.c_str());
        }
        AddSeparator(recent_menu_);
        AddCommand(recent_menu_, app::CommandId::recent_clear, L"清除最近文件");
    }
    if (bar_) InvalidateRect(bar_, nullptr, TRUE);
}

void MenuController::refresh() {
    if (!menu_ || !query_) return;
    for (std::uint16_t value = 100; value <= 702; ++value) {
        if (!IsKnown(value)) continue;
        const auto state = query_(static_cast<app::CommandId>(value));
        EnableMenuItem(menu_, value, MF_BYCOMMAND |
            (state.enabled ? MF_ENABLED : MF_GRAYED));
        CheckMenuItem(menu_, value, MF_BYCOMMAND |
            (state.checked ? MF_CHECKED : MF_UNCHECKED));
    }
    if (bar_) InvalidateRect(bar_, nullptr, TRUE);
}

HACCEL MenuController::accelerator() const noexcept { return accelerator_; }

void MenuController::AddCommand(HMENU menu, app::CommandId command,
                                const wchar_t* text) {
    AppendMenuW(menu, MF_STRING, Native(command), text);
}

void MenuController::AddPopup(HMENU menu, HMENU popup, const wchar_t* text) {
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(popup), text);
}

void MenuController::AddSeparator(HMENU menu) {
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
}

const wchar_t* MenuController::KeepLabel(std::wstring text) {
    labels_.push_back(std::move(text));
    return labels_.back().c_str();
}

void MenuController::apply_appearance(COLORREF text, COLORREF surface, UINT dpi) {
    text_color_ = text;
    surface_color_ = surface;
    dpi_ = dpi ? dpi : 96;
    height_ = MulDiv(36, static_cast<int>(dpi_), 96);
    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi_);
    if (font_) DeleteObject(font_);
    font_ = CreateFontIndirectW(&metrics.lfMenuFont);
    HDC dc = GetDC(bar_ ? bar_ : window_);
    const auto old = SelectObject(dc, font_);
    for (auto& item : top_items_) {
        SIZE size{};
        GetTextExtentPoint32W(dc, item.label, static_cast<int>(wcslen(item.label)), &size);
        item.width = size.cx + MulDiv(24, static_cast<int>(dpi_), 96);
        SendMessageW(item.button, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
    }
    SelectObject(dc, old);
    ReleaseDC(bar_ ? bar_ : window_, dc);
    resize(0);
    if (bar_) InvalidateRect(bar_, nullptr, TRUE);
}

bool MenuController::draw(const DRAWITEMSTRUCT& item) const {
    if (item.CtlType != ODT_BUTTON) return false;
    const auto found = std::find_if(top_items_.begin(), top_items_.end(),
        [&item](const TopItem& value) { return value.button == item.hwndItem; });
    if (found == top_items_.end()) return false;
    const bool selected = (item.itemState & (ODS_SELECTED | ODS_HOTLIGHT)) != 0;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    HIGHCONTRASTW contrast{sizeof(contrast)};
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
    const bool high_contrast = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    const auto selected_gray = GetRValue(surface_color_) < 128
        ? RGB(70, 70, 70) : RGB(232, 232, 232);
    const auto background = selected
        ? high_contrast ? GetSysColor(COLOR_HIGHLIGHT) : selected_gray
        : surface_color_;
    const auto foreground = disabled ? GetSysColor(COLOR_GRAYTEXT) :
        selected && high_contrast ? GetSysColor(COLOR_HIGHLIGHTTEXT) : text_color_;
    const auto surface = CreateSolidBrush(surface_color_);
    FillRect(item.hDC, &item.rcItem, surface);
    DeleteObject(surface);
    if (selected) {
        RECT highlight = item.rcItem;
        InflateRect(&highlight, -MulDiv(2, dpi_, 96), -MulDiv(3, dpi_, 96));
        const auto brush = CreateSolidBrush(background);
        const auto old_brush = SelectObject(item.hDC, brush);
        const auto old_pen = SelectObject(item.hDC, GetStockObject(NULL_PEN));
        RoundRect(item.hDC, highlight.left, highlight.top, highlight.right, highlight.bottom,
            MulDiv(8, dpi_, 96), MulDiv(8, dpi_, 96));
        SelectObject(item.hDC, old_pen); SelectObject(item.hDC, old_brush); DeleteObject(brush);
    }
    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi_);
    metrics.lfMenuFont.lfHeight = -MulDiv(14, static_cast<int>(dpi_), 96);
    const auto font = CreateFontIndirectW(&metrics.lfMenuFont);
    const auto old = SelectObject(item.hDC, font);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, foreground);
    RECT text = item.rcItem;
    DrawTextW(item.hDC, found->label, -1, &text,
        DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_HIDEPREFIX);
    SelectObject(item.hDC, old);
    DeleteObject(font);
    return true;
}

bool MenuController::handle_control(std::uint16_t native_id, HWND control) {
    if (native_id < 9000 || native_id >= 9000 + top_items_.size()) return false;
    const auto index = static_cast<std::size_t>(native_id - 9000);
    if (top_items_[index].button != control) return false;
    OpenPopup(index);
    return true;
}

bool MenuController::handle_syschar(wchar_t character) {
    constexpr std::array mnemonics{L'F', L'E', L'O', L'V', L'T', L'H'};
    character = static_cast<wchar_t>(towupper(character));
    const auto found = std::find(mnemonics.begin(), mnemonics.end(), character);
    if (found == mnemonics.end()) return false;
    OpenPopup(static_cast<std::size_t>(found - mnemonics.begin()));
    return true;
}

void MenuController::OpenPopup(std::size_t index) {
    refresh();
    RECT bounds{};
    GetWindowRect(top_items_[index].button, &bounds);
    const auto command = TrackPopupMenuEx(top_items_[index].popup,
        TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, bounds.left, bounds.bottom,
        window_, nullptr);
    if (command) static_cast<void>(dispatch(static_cast<std::uint16_t>(command)));
    InvalidateRect(top_items_[index].button, nullptr, TRUE);
}

void MenuController::resize(int width) {
    if (!bar_) return;
    if (width <= 0) { RECT client{}; GetClientRect(window_, &client); width = client.right; }
    MoveWindow(bar_, 0, 0, width, height_, TRUE);
    int left = MulDiv(4, static_cast<int>(dpi_), 96);
    for (auto& item : top_items_) {
        MoveWindow(item.button, left, 0, item.width, height_, TRUE);
        left += item.width;
    }
}

HWND MenuController::handle() const noexcept { return bar_; }
int MenuController::height() const noexcept { return height_; }

}  // namespace markdownmay::ui
