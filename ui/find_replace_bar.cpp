#include "markdownmay/ui/find_replace_bar.hpp"

#include <windowsx.h>

#include <algorithm>
#include <string>
#include <vector>

namespace markdownmay::ui {
namespace {
constexpr int kFind = 8101;
constexpr int kReplace = 8102;
constexpr int kFindNext = 8103;
constexpr int kReplaceOne = 8104;
constexpr int kReplaceAll = 8105;
constexpr int kCase = 8106;
constexpr int kWildcard = 8107;
constexpr int kSpecial = 8108;
constexpr int kClose = 8109;

HWND Child(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style, int id) {
    return CreateWindowExW(0, type, text, WS_CHILD | style, 0, 0, 0, 0, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
}
}

FindReplaceBar::~FindReplaceBar() {
    if (font_) DeleteObject(font_);
}

bool FindReplaceBar::create(HWND parent) {
    parent_ = parent;
    bar_ = Child(parent, L"STATIC", L"", WS_CLIPCHILDREN, 0);
    if (!bar_) return false;
    Child(bar_, L"STATIC", L"查找：", WS_VISIBLE | SS_CENTERIMAGE, 0);
    find_edit_ = Child(bar_, L"EDIT", L"", WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, kFind);
    replace_label_ = Child(bar_, L"STATIC", L"替换为：", SS_CENTERIMAGE, 0);
    replace_edit_ = Child(bar_, L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, kReplace);
    find_button_ = Child(bar_, L"BUTTON", L"查找下一个", WS_VISIBLE | WS_TABSTOP, kFindNext);
    replace_button_ = Child(bar_, L"BUTTON", L"替换", WS_TABSTOP, kReplaceOne);
    replace_all_button_ = Child(bar_, L"BUTTON", L"全部替换", WS_TABSTOP, kReplaceAll);
    case_box_ = Child(bar_, L"BUTTON", L"区分大小写", WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, kCase);
    wildcard_box_ = Child(bar_, L"BUTTON", L"使用通配符", WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, kWildcard);
    special_button_ = Child(bar_, L"BUTTON", L"特殊格式", WS_VISIBLE | WS_TABSTOP, kSpecial);
    close_button_ = Child(bar_, L"BUTTON", L"关闭", WS_VISIBLE | WS_TABSTOP, kClose);
    status_ = Child(bar_, L"STATIC", L"^p 段落标记；^t 制表符（Tab）", WS_VISIBLE | SS_CENTERIMAGE, 0);
    return find_edit_ && replace_label_ && replace_edit_ && find_button_ &&
        replace_button_ && replace_all_button_ && case_box_ && wildcard_box_ &&
        special_button_ && close_button_ && status_;
}

void FindReplaceBar::show(bool replace_mode) {
    replace_mode_ = replace_mode;
    ShowWindow(bar_, SW_SHOW);
    ShowWindow(replace_label_, replace_mode ? SW_SHOW : SW_HIDE);
    ShowWindow(replace_edit_, replace_mode ? SW_SHOW : SW_HIDE);
    ShowWindow(replace_button_, replace_mode ? SW_SHOW : SW_HIDE);
    ShowWindow(replace_all_button_, replace_mode ? SW_SHOW : SW_HIDE);
    RECT client{}; GetClientRect(parent_, &client); resize(client);
    SetFocus(find_edit_);
    SendMessageW(find_edit_, EM_SETSEL, 0, -1);
}
void FindReplaceBar::hide() {
    ShowWindow(bar_, SW_HIDE);
    if (parent_) SendMessageW(parent_, WM_SIZE, 0, 0);
}
bool FindReplaceBar::visible() const noexcept { return bar_ && IsWindowVisible(bar_); }
int FindReplaceBar::height() const noexcept { return visible() ? MulDiv(replace_mode_ ? 92 : 62, dpi_, 96) : 0; }

void FindReplaceBar::resize(const RECT& bounds) {
    if (!bar_) return;
    const int width = bounds.right - bounds.left;
    const int row = MulDiv(28, dpi_, 96), gap = MulDiv(6, dpi_, 96);
    const int label = MulDiv(70, dpi_, 96), edit = (std::max)(MulDiv(180, dpi_, 96), width - MulDiv(650, dpi_, 96));
    const int button = MulDiv(92, dpi_, 96);
    MoveWindow(bar_, bounds.left, bounds.top, width, height(), TRUE);
    auto controls = std::vector<HWND>{};
    EnumChildWindows(bar_, [](HWND window, LPARAM value) -> BOOL {
        reinterpret_cast<std::vector<HWND>*>(value)->push_back(window); return TRUE;
    }, reinterpret_cast<LPARAM>(&controls));
    HWND find_label = controls.empty() ? nullptr : controls.front();
    int x = gap, y = gap;
    if (find_label) MoveWindow(find_label, x, y, label, row, TRUE);
    x += label; MoveWindow(find_edit_, x, y, edit, row, TRUE); x += edit + gap;
    MoveWindow(find_button_, x, y, button, row, TRUE); x += button + gap;
    MoveWindow(case_box_, x, y, MulDiv(105, dpi_, 96), row, TRUE); x += MulDiv(110, dpi_, 96);
    MoveWindow(wildcard_box_, x, y, MulDiv(105, dpi_, 96), row, TRUE); x += MulDiv(110, dpi_, 96);
    MoveWindow(special_button_, x, y, MulDiv(82, dpi_, 96), row, TRUE); x += MulDiv(88, dpi_, 96);
    MoveWindow(close_button_, x, y, MulDiv(58, dpi_, 96), row, TRUE);
    if (replace_mode_) {
        y += row + gap; x = gap;
        MoveWindow(replace_label_, x, y, label, row, TRUE); x += label;
        MoveWindow(replace_edit_, x, y, edit, row, TRUE); x += edit + gap;
        MoveWindow(replace_button_, x, y, button, row, TRUE); x += button + gap;
        MoveWindow(replace_all_button_, x, y, button, row, TRUE); x += button + gap;
        MoveWindow(status_, x, y, (std::max)(0, width - x - gap), row, TRUE);
    } else {
        MoveWindow(status_, gap, gap + row, width - gap * 2, MulDiv(22, dpi_, 96), TRUE);
    }
}

bool FindReplaceBar::handle_control(HWND control, std::uint16_t notification) {
    if (!bar_ || GetParent(control) != bar_ || notification != BN_CLICKED) return false;
    const auto id = GetDlgCtrlID(control);
    if (id == kFindNext) FindNext();
    else if (id == kReplaceOne) {
        const auto result = modes_.replace_current_text(ReadUtf8(find_edit_), ReadUtf8(replace_edit_),
            Button_GetCheck(case_box_) == BST_CHECKED, Button_GetCheck(wildcard_box_) == BST_CHECKED);
        if (result == ErrorCode::ok) { SetStatus(L"已替换当前匹配项"); FindNext(); }
        else SetStatus(L"当前选区不是匹配项");
    } else if (id == kReplaceAll) {
        const auto result = modes_.replace_all_text(ReadUtf8(find_edit_), ReadUtf8(replace_edit_),
            Button_GetCheck(case_box_) == BST_CHECKED, Button_GetCheck(wildcard_box_) == BST_CHECKED);
        if (result.is_ok()) {
            const auto message = L"已替换 " + std::to_wstring(result.value()) + L" 处";
            SetStatus(message.c_str());
        } else SetStatus(L"无法执行全部替换");
    } else if (id == kSpecial) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 1, L"段落标记  ^p");
        AppendMenuW(menu, MF_STRING, 2, L"制表符 (Tab)  ^t");
        RECT rect{}; GetWindowRect(special_button_, &rect);
        const auto selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN,
            rect.left, rect.bottom, 0, parent_, nullptr);
        DestroyMenu(menu);
        if (selected == 1) InsertSpecial(L"^p"); else if (selected == 2) InsertSpecial(L"^t");
    } else if (id == kClose) { hide(); SetFocus(modes_.handle()); }
    else return id == kCase || id == kWildcard;
    return true;
}

std::string FindReplaceBar::ReadUtf8(HWND control) const {
    const auto length = GetWindowTextLengthW(control);
    std::wstring wide(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, wide.data(), length + 1); wide.resize(static_cast<std::size_t>(length));
    if (wide.empty()) return {};
    const auto bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(), length, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), length, result.data(), bytes, nullptr, nullptr);
    return result;
}
void FindReplaceBar::SetStatus(const wchar_t* text) { SetWindowTextW(status_, text); }
void FindReplaceBar::FindNext() {
    const auto result = modes_.find_text(ReadUtf8(find_edit_), true,
        Button_GetCheck(case_box_) == BST_CHECKED, Button_GetCheck(wildcard_box_) == BST_CHECKED);
    SetStatus(result.is_ok() ? L"已找到匹配项" : L"未找到匹配项");
}
void FindReplaceBar::InsertSpecial(const wchar_t* token) {
    auto target = GetFocus();
    if (target != find_edit_ && target != replace_edit_) target = find_edit_;
    SendMessageW(target, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(token));
    SetFocus(target);
}
void FindReplaceBar::apply_appearance(COLORREF text, COLORREF background, UINT dpi) {
    dpi_ = dpi ? dpi : 96;
    if (!bar_) return;
    if (font_) DeleteObject(font_);
    font_ = CreateFontW(-MulDiv(14, dpi_, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, L"Microsoft YaHei UI");
    EnumChildWindows(bar_, [](HWND window, LPARAM font) -> BOOL {
        SendMessageW(window, WM_SETFONT, static_cast<WPARAM>(font), TRUE); return TRUE;
    }, reinterpret_cast<LPARAM>(font_));
    if (bar_) InvalidateRect(bar_, nullptr, TRUE);
    static_cast<void>(background);
    static_cast<void>(text);
}

}  // namespace markdownmay::ui
