#include "markdownmay/ui/status_bar.hpp"

#include <commctrl.h>

#include <array>
#include <string>

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

bool StatusBar::create(HWND parent) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
    static_cast<void>(InitCommonControlsEx(&controls));
    handle_ = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!handle_) return false;
    RECT bounds{};
    GetWindowRect(handle_, &bounds);
    height_ = bounds.bottom - bounds.top;
    refresh();
    return true;
}

void StatusBar::resize(int width, int client_height) {
    if (!handle_) return;
    const std::array<int, 5> parts{140, 250, 340, width - 150, -1};
    SendMessageW(handle_, SB_SETPARTS, parts.size(),
        reinterpret_cast<LPARAM>(parts.data()));
    MoveWindow(handle_, 0, client_height - height_, width, height_, TRUE);
}

void StatusBar::refresh() {
    if (!handle_) return;
    const auto snapshot = session_.snapshot();
    const auto characters = CountCharacters(snapshot.source);
    const auto count = std::to_wstring(characters) + L" 字";
    SendMessageW(handle_, SB_SETTEXTW, 0,
        reinterpret_cast<LPARAM>(session_.is_dirty() ? L"未保存" : L"已保存"));
    SendMessageW(handle_, SB_SETTEXTW, 1,
        reinterpret_cast<LPARAM>(EncodingName(encoding_)));
    SendMessageW(handle_, SB_SETTEXTW, 2,
        reinterpret_cast<LPARAM>(LineEndingName(line_ending_)));
    SendMessageW(handle_, SB_SETTEXTW, 3, reinterpret_cast<LPARAM>(count.c_str()));
    SendMessageW(handle_, SB_SETTEXTW, 4,
        reinterpret_cast<LPARAM>(ModeName(modes_.mode())));
}

void StatusBar::set_file_format(fileio::TextEncoding encoding,
                                fileio::LineEnding line_ending) {
    encoding_ = encoding;
    line_ending_ = line_ending;
    refresh();
}

HWND StatusBar::handle() const noexcept { return handle_; }
int StatusBar::height() const noexcept { return height_; }

}  // namespace markdownmay::ui
