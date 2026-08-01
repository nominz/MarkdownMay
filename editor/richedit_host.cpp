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

std::wstring ReadWide(HWND handle) {
    const auto length = GetWindowTextLengthW(handle);
    std::wstring value(static_cast<std::size_t>(length) + 1U, L'\0');
    const auto copied = GetWindowTextW(handle, value.data(), length + 1);
    value.resize(static_cast<std::size_t>((std::max)(copied, 0)));
    return value;
}

LONG Utf16Length(std::string_view text, std::uint64_t utf8_end) {
    const auto bounded = (std::min)(utf8_end, static_cast<std::uint64_t>(text.size()));
    const auto prefix = fileio::NormalizeLineEndings(
        text.substr(0, static_cast<std::size_t>(bounded)), fileio::LineEnding::crlf);
    return static_cast<LONG>(ToWide(prefix).size());
}

void ApplySpan(HWND handle, const RichProjection& projection, const ProjectionSpan& span) {
    const auto begin = Utf16Length(projection.text, span.begin);
    const auto end = Utf16Length(projection.text, span.end);
    SendMessageW(handle, EM_SETSEL, static_cast<WPARAM>(begin), static_cast<LPARAM>(end));
    CHARFORMAT2W format{};
    format.cbSize = sizeof(format);
    if (span.kind == document::NodeKind::strong) {
        format.dwMask = CFM_BOLD; format.dwEffects = CFE_BOLD;
    } else if (span.kind == document::NodeKind::emphasis) {
        format.dwMask = CFM_ITALIC; format.dwEffects = CFE_ITALIC;
    } else if (span.kind == document::NodeKind::strike) {
        format.dwMask = CFM_STRIKEOUT; format.dwEffects = CFE_STRIKEOUT;
    } else if (span.kind == document::NodeKind::inline_code) {
        format.dwMask = CFM_FACE | CFM_BACKCOLOR;
        format.crBackColor = RGB(238, 238, 238);
        wcscpy_s(format.szFaceName, L"Consolas");
    } else if (span.kind == document::NodeKind::link) {
        format.dwMask = CFM_UNDERLINE | CFM_COLOR;
        format.dwEffects = CFE_UNDERLINE;
        format.crTextColor = RGB(0, 102, 204);
    }
    SendMessageW(handle, EM_SETCHARFORMAT, SCF_SELECTION,
                 reinterpret_cast<LPARAM>(&format));
}

}  // namespace

RichEditHost::RichEditHost(document::DocumentSession& session)
    : session_(session), editor_(session), formatter_(session, editor_) {}

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
    const auto snapshot = session_.snapshot();
    if (!snapshot.semantic) {
        projecting_ = false;
        return ErrorCode::editor_render_projection_failed;
    }
    projection_ = BuildInlineProjection(*snapshot.semantic, snapshot.source);
    const auto rich_text = ToWide(fileio::NormalizeLineEndings(
        projection_.text, fileio::LineEnding::crlf));
    const auto success = SetWindowTextW(handle_, rich_text.c_str()) != 0 || rich_text.empty();
    projecting_ = false;
    if (!success) return ErrorCode::editor_render_projection_failed;
    for (const auto& span : projection_.spans) ApplySpan(handle_, projection_, span);
    SendMessageW(handle_, EM_SETSEL, static_cast<WPARAM>(rich_text.size()),
                 static_cast<LPARAM>(rich_text.size()));
    return ErrorCode::ok;
}

ErrorCode RichEditHost::synchronize_change() {
    if (projecting_) return ErrorCode::ok;
    CHARRANGE control_selection{};
    SendMessageW(handle_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&control_selection));
    const auto source = session_.snapshot().source;
    const auto before = projection_.text;
    const auto line_ending = fileio::DetectLineEnding(source);
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

    if (prefix >= projection_.source_offsets.size() ||
        old_suffix >= projection_.source_offsets.size()) {
        static_cast<void>(project());
        return ErrorCode::editor_selection_mapping_failed;
    }
    auto result = editor_.set_selection(
        {projection_.source_offsets[prefix], projection_.source_offsets[old_suffix]});
    if (result == ErrorCode::ok) {
        result = editor_.insert_text(after.substr(prefix, new_suffix - prefix));
    }
    if (result != ErrorCode::ok) {
        static_cast<void>(project());
        return result;
    }
    const auto projected = project();
    if (projected == ErrorCode::ok) {
        const auto length = static_cast<LONG>(GetWindowTextLengthW(handle_));
        control_selection.cpMin = (std::min)(control_selection.cpMin, length);
        control_selection.cpMax = (std::min)(control_selection.cpMax, length);
        SendMessageW(handle_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&control_selection));
    }
    return projected;
}

ErrorCode RichEditHost::toggle_inline(InlineFormat format) {
    if (!handle_) return ErrorCode::editor_render_projection_failed;
    CHARRANGE selected{};
    SendMessageW(handle_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selected));
    const auto visible = ReadWide(handle_);
    if (selected.cpMin < 0 || selected.cpMax < selected.cpMin ||
        static_cast<std::size_t>(selected.cpMax) > visible.size())
        return ErrorCode::editor_selection_mapping_failed;
    const auto target = fileio::DetectLineEnding(projection_.text);
    const auto begin = fileio::NormalizeLineEndings(
        ToUtf8(std::wstring_view(visible).substr(0, static_cast<std::size_t>(selected.cpMin))), target).size();
    const auto end = fileio::NormalizeLineEndings(
        ToUtf8(std::wstring_view(visible).substr(0, static_cast<std::size_t>(selected.cpMax))), target).size();
    if (begin >= projection_.source_offsets.size() || end >= projection_.source_offsets.size())
        return ErrorCode::editor_selection_mapping_failed;
    auto result = editor_.set_selection(
        {projection_.source_offsets[begin], projection_.source_offsets[end]});
    if (result == ErrorCode::ok) result = formatter_.toggle(format);
    return result == ErrorCode::ok ? project() : result;
}

ErrorCode RichEditHost::set_link(std::string_view target, std::string_view title) {
    if (!handle_) return ErrorCode::editor_render_projection_failed;
    CHARRANGE selected{};
    SendMessageW(handle_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selected));
    const auto visible = ReadWide(handle_);
    if (selected.cpMin < 0 || selected.cpMax < selected.cpMin ||
        static_cast<std::size_t>(selected.cpMax) > visible.size())
        return ErrorCode::editor_selection_mapping_failed;
    const auto line_ending = fileio::DetectLineEnding(projection_.text);
    const auto begin = fileio::NormalizeLineEndings(
        ToUtf8(std::wstring_view(visible).substr(0, static_cast<std::size_t>(selected.cpMin))), line_ending).size();
    const auto end = fileio::NormalizeLineEndings(
        ToUtf8(std::wstring_view(visible).substr(0, static_cast<std::size_t>(selected.cpMax))), line_ending).size();
    if (begin >= projection_.source_offsets.size() || end >= projection_.source_offsets.size())
        return ErrorCode::editor_selection_mapping_failed;
    auto result = editor_.set_selection(
        {projection_.source_offsets[begin], projection_.source_offsets[end]});
    if (result == ErrorCode::ok) result = formatter_.set_link(target, title);
    return result == ErrorCode::ok ? project() : result;
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
