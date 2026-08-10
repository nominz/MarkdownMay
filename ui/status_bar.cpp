#include "markdownmay/ui/status_bar.hpp"

#include <commctrl.h>
#include <windowsx.h>

#include <array>
#include <string>
#include <utility>

namespace markdownmay::ui {
namespace {
std::uint64_t CountCharacters(std::string_view source) noexcept {
    std::uint64_t count{};
    for (std::size_t index = 0; index < source.size(); ++index) {
        const auto byte = static_cast<unsigned char>(source[index]);
        if ((byte & 0xc0U) == 0x80U) continue;
        if (byte < 0x80U && (byte == ' ' || byte == '\t' ||
            byte == '\r' || byte == '\n')) continue;
        ++count;
    }
    return count;
}

const wchar_t* ModeName(editor::ViewMode mode) noexcept {
    switch (mode) {
    case editor::ViewMode::source: return L"源码模式";
    case editor::ViewMode::split: return L"对照模式";
    default: return L"渲染模式";
    }
}

const wchar_t* EncodingName(fileio::TextEncoding encoding) noexcept {
    switch (encoding) {
    case fileio::TextEncoding::utf8_bom: return L"UTF-8 BOM";
    case fileio::TextEncoding::utf16_le: return L"UTF-16 LE";
    case fileio::TextEncoding::utf16_be: return L"UTF-16 BE";
    default: return L"UTF-8";
    }
}

const wchar_t* LineEndingName(fileio::LineEnding line_ending) noexcept {
    switch (line_ending) {
    case fileio::LineEnding::lf: return L"LF";
    case fileio::LineEnding::mixed: return L"混合换行";
    default: return L"CRLF";
    }
}
}

StatusBar::StatusBar(document::DocumentSession& session,
                     editor::ViewModeController& modes)
    : session_(session), modes_(modes) {}
StatusBar::~StatusBar() {
    if (handle_) RemoveWindowSubclass(handle_, SubclassProcedure, 1);
    if (font_) DeleteObject(font_);
}

bool StatusBar::create(HWND parent) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
    static_cast<void>(InitCommonControlsEx(&controls));
    handle_ = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!handle_) return false;
    if (!SetWindowSubclass(handle_, SubclassProcedure, 1,
            reinterpret_cast<DWORD_PTR>(this))) return false;
    RECT bounds{};
    GetWindowRect(handle_, &bounds);
    height_ = bounds.bottom - bounds.top;
    refresh();
    return true;
}

void StatusBar::resize(int width, int client_height) {
    if (!handle_) return;
    const std::array<int, 6> parts{90, 230, 340, 430, width - 150, -1};
    SendMessageW(handle_, SB_SETPARTS, parts.size(),
        reinterpret_cast<LPARAM>(parts.data()));
    MoveWindow(handle_, 0, client_height - height_, width, height_, TRUE);
}

void StatusBar::refresh() {
    if (!handle_) return;
    const auto snapshot = session_.snapshot();
    const auto characters = CountCharacters(snapshot.source);
    const auto count = std::to_wstring(characters) + L" 字";
    labels_ = {L"大纲", session_.is_dirty() ? L"未保存" : L"已保存",
        EncodingName(encoding_), LineEndingName(line_ending_), count,
        ModeName(modes_.mode())};
    for (std::size_t index = 0; index < labels_.size(); ++index)
        SendMessageW(handle_, SB_SETTEXTW, index,
            reinterpret_cast<LPARAM>(labels_[index].c_str()));
    InvalidateRect(handle_, nullptr, FALSE);
}

void StatusBar::set_file_format(fileio::TextEncoding encoding,
                                fileio::LineEnding line_ending) {
    encoding_ = encoding;
    line_ending_ = line_ending;
    refresh();
}

HWND StatusBar::handle() const noexcept { return handle_; }
int StatusBar::height() const noexcept { return height_; }
void StatusBar::apply_appearance(COLORREF text, COLORREF background, UINT dpi) {
    text_color_ = text;
    background_color_ = background;
    dpi_ = dpi;
    height_ = MulDiv(27, static_cast<int>(dpi), 96);
    if (font_) DeleteObject(font_);
    font_ = CreateFontW(-MulDiv(9, static_cast<int>(dpi), 72), 0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    if (!handle_) return;
    SendMessageW(handle_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    SendMessageW(handle_, SB_SETBKCOLOR, 0, background);
    InvalidateRect(handle_, nullptr, TRUE);
}
void StatusBar::set_outline_callbacks(std::function<bool()> visible,
                                      std::function<void()> toggle) {
    outline_visible_ = std::move(visible);
    toggle_outline_ = std::move(toggle);
}

LRESULT CALLBACK StatusBar::SubclassProcedure(HWND window, UINT message,
        WPARAM w_param, LPARAM l_param, UINT_PTR id, DWORD_PTR data) {
    auto* self = reinterpret_cast<StatusBar*>(data);
    if (message == WM_PAINT && self) {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(window, &paint);
        self->Paint(dc);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_LBUTTONUP && self && self->toggle_outline_) {
        RECT part{};
        SendMessageW(window, SB_GETRECT, 0, reinterpret_cast<LPARAM>(&part));
        POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        if (PtInRect(&part, point)) {
            self->toggle_outline_();
            self->refresh();
            return 0;
        }
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, SubclassProcedure, id);
        if (self) self->handle_ = nullptr;
    }
    return DefSubclassProc(window, message, w_param, l_param);
}

void StatusBar::Paint(HDC dc) {
    RECT client{};
    GetClientRect(handle_, &client);
    const auto background = CreateSolidBrush(background_color_);
    FillRect(dc, &client, background);
    DeleteObject(background);
    const auto top_pen = CreatePen(PS_SOLID, 1,
        GetRValue(background_color_) + GetGValue(background_color_) +
            GetBValue(background_color_) < 384 ? RGB(72, 72, 74) : RGB(205, 205, 205));
    const auto old_top_pen = SelectObject(dc, top_pen);
    MoveToEx(dc, client.left, client.top, nullptr);
    LineTo(dc, client.right, client.top);
    SelectObject(dc, old_top_pen);
    DeleteObject(top_pen);
    const auto old_font = SelectObject(dc, font_);
    SetBkMode(dc, TRANSPARENT);
    const auto padding = MulDiv(12, dpi_, 96);
    const auto separator_inset = MulDiv(6, dpi_, 96);
    const auto separator_pen = CreatePen(PS_SOLID, 1, RGB(224, 224, 224));
    const auto old_pen = SelectObject(dc, separator_pen);
    for (std::size_t index = 0; index < labels_.size(); ++index) {
        SetTextColor(dc, index == 0 && outline_visible_ && !outline_visible_()
            ? GetSysColor(COLOR_GRAYTEXT) : text_color_);
        RECT part{};
        SendMessageW(handle_, SB_GETRECT, index, reinterpret_cast<LPARAM>(&part));
        part.left += padding;
        part.right -= padding;
        DrawTextW(dc, labels_[index].c_str(), -1, &part,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (index + 1 < labels_.size()) {
            const auto x = part.right + padding;
            MoveToEx(dc, x, part.top + separator_inset, nullptr);
            LineTo(dc, x, part.bottom - separator_inset);
        }
    }
    SelectObject(dc, old_pen);
    DeleteObject(separator_pen);
    SelectObject(dc, old_font);
}

}  // namespace markdownmay::ui
