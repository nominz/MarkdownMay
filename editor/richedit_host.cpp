#include "markdownmay/editor/richedit_host.hpp"

#include "markdownmay/fileio/line_endings.hpp"

#include <richedit.h>

#include <algorithm>
#include <string>

namespace markdownmay::editor {
namespace {

std::wstring ToWide(std::string_view value) {
    if (value.empty()) return {};
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string ToUtf8(std::wstring_view value) {
    if (value.empty()) return {};
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::string ReadUtf8(HWND handle, fileio::LineEnding target) {
    const auto length = GetWindowTextLengthW(handle);
    std::wstring value(static_cast<std::size_t>(length) + 1U, L'\0');
    const auto copied = GetWindowTextW(handle, value.data(), length + 1);
    value.resize(static_cast<std::size_t>((std::max)(copied, 0)));
    return fileio::NormalizeLineEndings(ToUtf8(value), target);
}

}  // namespace

RichEditHost::RichEditHost(document::DocumentSession& session)
    : session_(session), editor_(session) {}

RichEditHost::~RichEditHost() {
    if (handle_ && IsWindow(handle_)) DestroyWindow(handle_);
    if (rich_edit_module_) FreeLibrary(rich_edit_module_);
}

ErrorCode RichEditHost::create(HWND parent, const RECT& bounds) {
    if (handle_) return ErrorCode::ok;
    rich_edit_module_ = LoadLibraryExW(L"msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!rich_edit_module_) return ErrorCode::editor_render_projection_failed;
    handle_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
        WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL,
        bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!handle_) return ErrorCode::editor_render_projection_failed;
    SendMessageW(handle_, EM_SETLIMITTEXT, 0, 0);
    SendMessageW(handle_, EM_SETUNDOLIMIT, 0, 0);
    return project();
}

ErrorCode RichEditHost::project() {
    if (!handle_) return ErrorCode::editor_render_projection_failed;
    projecting_ = true;
    const auto source = session_.snapshot().source;
    const auto rich_text = ToWide(fileio::NormalizeLineEndings(source, fileio::LineEnding::crlf));
    const auto success = SetWindowTextW(handle_, rich_text.c_str()) != 0 || rich_text.empty();
    projecting_ = false;
    if (!success) return ErrorCode::editor_render_projection_failed;
    SendMessageW(handle_, EM_SETSEL, static_cast<WPARAM>(rich_text.size()),
                 static_cast<LPARAM>(rich_text.size()));
    return ErrorCode::ok;
}

ErrorCode RichEditHost::synchronize_change() {
    if (projecting_) return ErrorCode::ok;
    const auto before = session_.snapshot().source;
    const auto line_ending = fileio::DetectLineEnding(before);
    const auto after = ReadUtf8(
        handle_, line_ending == fileio::LineEnding::mixed ? fileio::LineEnding::crlf : line_ending);
    if (before == after) return ErrorCode::ok;

    std::size_t prefix{};
    while (prefix < before.size() && prefix < after.size() && before[prefix] == after[prefix]) ++prefix;
    while (prefix > 0 && prefix < before.size() &&
           (static_cast<unsigned char>(before[prefix]) & 0xC0U) == 0x80U) --prefix;
    std::size_t old_suffix = before.size();
    std::size_t new_suffix = after.size();
    while (old_suffix > prefix && new_suffix > prefix &&
           before[old_suffix - 1] == after[new_suffix - 1]) {
        --old_suffix;
        --new_suffix;
    }
    while (old_suffix < before.size() &&
           (static_cast<unsigned char>(before[old_suffix]) & 0xC0U) == 0x80U) ++old_suffix;
    while (new_suffix < after.size() &&
           (static_cast<unsigned char>(after[new_suffix]) & 0xC0U) == 0x80U) ++new_suffix;

    auto result = editor_.set_selection(
        {static_cast<std::uint64_t>(prefix), static_cast<std::uint64_t>(old_suffix)});
    if (result == ErrorCode::ok) {
        result = editor_.insert_text(after.substr(prefix, new_suffix - prefix));
    }
    if (result != ErrorCode::ok) {
        static_cast<void>(project());
        return result;
    }
    return ErrorCode::ok;
}

ErrorCode RichEditHost::undo() {
    const auto result = editor_.undo();
    return result == ErrorCode::ok ? project() : result;
}

ErrorCode RichEditHost::redo() {
    const auto result = editor_.redo();
    return result == ErrorCode::ok ? project() : result;
}

HWND RichEditHost::handle() const noexcept { return handle_; }

}  // namespace markdownmay::editor
