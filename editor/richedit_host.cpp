#include "markdownmay/editor/richedit_host.hpp"

#include "markdownmay/fileio/line_endings.hpp"

#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <shlwapi.h>
#include <wrl/client.h>

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
        text.substr(0, static_cast<std::size_t>(bounded)), fileio::LineEnding::lf);
    return static_cast<LONG>(ToWide(prefix).size());
}

std::size_t PrefixUtf8Size(HWND handle, LONG position, fileio::LineEnding target) {
    if (position <= 0) return 0;
    std::wstring buffer(static_cast<std::size_t>(position) * 2U + 2U, L'\0');
    TEXTRANGEW range{{0, position}, buffer.data()};
    const auto copied = static_cast<LONG>(SendMessageW(
        handle, EM_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&range)));
    buffer.resize(static_cast<std::size_t>((std::max)(copied, 0L)));
    return fileio::NormalizeLineEndings(ToUtf8(buffer), target).size();
}

ErrorCode MapControlSelection(HWND handle, const RichProjection& projection,
                              ParagraphEditor& editor) {
    CHARRANGE selected{};
    SendMessageW(handle, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selected));
    if (selected.cpMin < 0 || selected.cpMax < selected.cpMin ||
        selected.cpMax > GetWindowTextLengthW(handle))
        return ErrorCode::editor_selection_mapping_failed;
    const auto line_ending = fileio::DetectLineEnding(projection.text);
    const auto begin = PrefixUtf8Size(handle, selected.cpMin, line_ending);
    const auto end = PrefixUtf8Size(handle, selected.cpMax, line_ending);
    if (begin >= projection.source_offsets.size() || end >= projection.source_offsets.size())
        return ErrorCode::editor_selection_mapping_failed;
    return editor.set_selection(
        {projection.source_offsets[begin], projection.source_offsets[end]});
}

void SelectSourceRange(HWND handle, const RichProjection& projection, TextSelection source) {
    std::size_t begin{}, end{};
    while (begin + 1 < projection.source_offsets.size() &&
           projection.source_offsets[begin] < source.anchor) ++begin;
    end = begin;
    while (end + 1 < projection.source_offsets.size() &&
           projection.source_offsets[end] < source.caret) ++end;
    CHARRANGE selected{Utf16Length(projection.text, begin), Utf16Length(projection.text, end)};
    SendMessageW(handle, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selected));
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
    } else if (span.kind == document::NodeKind::heading) {
        format.dwMask = CFM_BOLD | CFM_SIZE;
        format.dwEffects = CFE_BOLD;
        format.yHeight = static_cast<LONG>((28 - (std::min)(span.heading_level, std::uint8_t{6}) * 2) * 20);
    } else if (span.kind == document::NodeKind::code_block) {
        format.dwMask = CFM_FACE | CFM_BACKCOLOR;
        format.crBackColor = RGB(245, 245, 245);
        wcscpy_s(format.szFaceName, L"Consolas");
    } else if (span.kind == document::NodeKind::quote) {
        format.dwMask = CFM_COLOR;
        format.crTextColor = RGB(96, 96, 96);
    } else if (span.kind == document::NodeKind::thematic_break) {
        format.dwMask = CFM_COLOR;
        format.crTextColor = RGB(150, 150, 150);
    } else if (span.kind == document::NodeKind::list_item && span.task) {
        format.dwMask = CFM_COLOR;
        format.crTextColor = span.checked ? RGB(90, 130, 90) : RGB(70, 70, 70);
    } else if (span.kind == document::NodeKind::image) {
        format.dwMask = CFM_BACKCOLOR | CFM_COLOR;
        format.crBackColor = span.image_state == ImageDisplayState::ready
            ? RGB(235, 245, 252) : RGB(250, 240, 230);
        format.crTextColor = span.image_state == ImageDisplayState::ready
            ? RGB(35, 90, 125) : RGB(145, 80, 45);
    } else if (span.kind == document::NodeKind::table_cell) {
        format.dwMask = CFM_BACKCOLOR;
        format.crBackColor = span.table_row == 0 ? RGB(230, 236, 242) : RGB(248, 248, 248);
    }
    SendMessageW(handle, EM_SETCHARFORMAT, SCF_SELECTION,
                 reinterpret_cast<LPARAM>(&format));
    if (span.kind == document::NodeKind::image &&
        span.image_state == ImageDisplayState::ready && !span.image_path.empty()) {
        Microsoft::WRL::ComPtr<IStream> stream;
        Microsoft::WRL::ComPtr<IRichEditOle> rich_ole;
        Microsoft::WRL::ComPtr<ITextDocument2> text_document;
        Microsoft::WRL::ComPtr<ITextRange2> range;
        RECT client{};
        GetClientRect(handle, &client);
        auto width = MulDiv(static_cast<int>(span.image_width), 2540, 96) *
            span.image_display_percent / 100L;
        auto height = MulDiv(static_cast<int>(span.image_height), 2540, 96) *
            span.image_display_percent / 100L;
        const auto maximum = MulDiv((std::max)(1L, client.right - client.left - 24L), 2540, 96);
        if (width > maximum) { height = height * maximum / width; width = maximum; }
        if (SUCCEEDED(SHCreateStreamOnFileEx(span.image_path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE,
                FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, &stream)) &&
            SendMessageW(handle, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(rich_ole.GetAddressOf())) &&
            SUCCEEDED(rich_ole.As(&text_document)) &&
            SUCCEEDED(text_document->Range2(begin, end, &range))) {
            const auto alternative = span.image_path.filename().wstring();
            const auto text = SysAllocString(alternative.c_str());
            static_cast<void>(range->InsertImage(width, height, height, TA_BASELINE, text, stream.Get()));
            SysFreeString(text);
        }
    }
    if (span.kind == document::NodeKind::quote ||
        span.kind == document::NodeKind::thematic_break ||
        span.kind == document::NodeKind::list_item ||
        span.kind == document::NodeKind::table) {
        PARAFORMAT2 paragraph{};
        paragraph.cbSize = sizeof(paragraph);
        paragraph.dwMask = span.kind == document::NodeKind::thematic_break
            ? PFM_ALIGNMENT : span.kind == document::NodeKind::table
            ? PFM_TABSTOPS : PFM_STARTINDENT;
        if (span.kind == document::NodeKind::quote) paragraph.dxStartIndent = 360;
        else if (span.kind == document::NodeKind::list_item)
            paragraph.dxStartIndent = 360 + static_cast<LONG>(span.list_depth) * 360;
        else if (span.kind == document::NodeKind::table) {
            paragraph.cTabCount = 8;
            for (LONG index = 0; index < paragraph.cTabCount; ++index)
                paragraph.rgxTabs[index] = (index + 1) * 1440;
        } else paragraph.wAlignment = PFA_CENTER;
        SendMessageW(handle, EM_SETPARAFORMAT, 0,
                     reinterpret_cast<LPARAM>(&paragraph));
    }
}

}  // namespace

RichEditHost::RichEditHost(document::DocumentSession& session)
    : session_(session), editor_(session), formatter_(session, editor_),
      block_formatter_(session, editor_), list_editor_(session, editor_),
      image_controller_(session, editor_), table_editor_(session, editor_),
      clipboard_controller_(session, editor_, image_controller_),
      find_replace_controller_(session, editor_) {}

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
    projection_ = BuildInlineProjection(*snapshot.semantic, snapshot.source, document_path_);
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
        const auto replacement = after.substr(prefix, new_suffix - prefix);
        const auto expected_eol = line_ending == fileio::LineEnding::lf ? "\n" : "\r\n";
        if (projection_.source_offsets[prefix] == projection_.source_offsets[old_suffix] &&
            replacement == expected_eol) {
            result = list_editor_.continue_item();
            if (result == ErrorCode::editor_selection_mapping_failed)
                result = editor_.insert_text(replacement);
        } else {
            result = editor_.insert_text(replacement);
        }
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
    const auto begin = PrefixUtf8Size(handle_, selected.cpMin, target);
    const auto end = PrefixUtf8Size(handle_, selected.cpMax, target);
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
    const auto begin = PrefixUtf8Size(handle_, selected.cpMin, line_ending);
    const auto end = PrefixUtf8Size(handle_, selected.cpMax, line_ending);
    if (begin >= projection_.source_offsets.size() || end >= projection_.source_offsets.size())
        return ErrorCode::editor_selection_mapping_failed;
    auto result = editor_.set_selection(
        {projection_.source_offsets[begin], projection_.source_offsets[end]});
    if (result == ErrorCode::ok) result = formatter_.set_link(target, title);
    return result == ErrorCode::ok ? project() : result;
}

ErrorCode RichEditHost::set_heading(std::uint8_t level) {
    if (!handle_) return ErrorCode::editor_render_projection_failed;
    CHARRANGE selected{};
    SendMessageW(handle_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selected));
    const auto visible = ReadWide(handle_);
    if (selected.cpMin < 0 || selected.cpMax < selected.cpMin ||
        static_cast<std::size_t>(selected.cpMax) > visible.size())
        return ErrorCode::editor_selection_mapping_failed;
    const auto line_ending = fileio::DetectLineEnding(projection_.text);
    const auto begin = PrefixUtf8Size(handle_, selected.cpMin, line_ending);
    const auto end = PrefixUtf8Size(handle_, selected.cpMax, line_ending);
    if (begin >= projection_.source_offsets.size() || end >= projection_.source_offsets.size())
        return ErrorCode::editor_selection_mapping_failed;
    auto result = editor_.set_selection(
        {projection_.source_offsets[begin], projection_.source_offsets[end]});
    if (result == ErrorCode::ok) result = block_formatter_.set_heading(level);
    return result == ErrorCode::ok ? project() : result;
}

ErrorCode RichEditHost::toggle_quote() {
    if (!handle_) return ErrorCode::editor_render_projection_failed;
    CHARRANGE selected{};
    SendMessageW(handle_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selected));
    const auto visible = ReadWide(handle_);
    const auto line_ending = fileio::DetectLineEnding(projection_.text);
    if (selected.cpMin < 0 || selected.cpMax < selected.cpMin ||
        static_cast<std::size_t>(selected.cpMax) > visible.size())
        return ErrorCode::editor_selection_mapping_failed;
    const auto begin = PrefixUtf8Size(handle_, selected.cpMin, line_ending);
    const auto end = PrefixUtf8Size(handle_, selected.cpMax, line_ending);
    if (begin >= projection_.source_offsets.size() || end >= projection_.source_offsets.size())
        return ErrorCode::editor_selection_mapping_failed;
    auto result = editor_.set_selection(
        {projection_.source_offsets[begin], projection_.source_offsets[end]});
    if (result == ErrorCode::ok) result = block_formatter_.toggle_quote();
    return result == ErrorCode::ok ? project() : result;
}

ErrorCode RichEditHost::toggle_code_block(std::string_view language) {
    if (!handle_) return ErrorCode::editor_render_projection_failed;
    CHARRANGE selected{};
    SendMessageW(handle_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selected));
    const auto visible = ReadWide(handle_);
    const auto line_ending = fileio::DetectLineEnding(projection_.text);
    if (selected.cpMin < 0 || selected.cpMax < selected.cpMin ||
        static_cast<std::size_t>(selected.cpMax) > visible.size())
        return ErrorCode::editor_selection_mapping_failed;
    const auto begin = fileio::NormalizeLineEndings(
        ToUtf8(std::wstring_view(visible).substr(0, static_cast<std::size_t>(selected.cpMin))), line_ending).size();
    const auto end = fileio::NormalizeLineEndings(
        ToUtf8(std::wstring_view(visible).substr(0, static_cast<std::size_t>(selected.cpMax))), line_ending).size();
    if (begin >= projection_.source_offsets.size() || end >= projection_.source_offsets.size())
        return ErrorCode::editor_selection_mapping_failed;
    auto result = editor_.set_selection(
        {projection_.source_offsets[begin], projection_.source_offsets[end]});
    if (result == ErrorCode::ok) result = block_formatter_.toggle_code_block(language);
    return result == ErrorCode::ok ? project() : result;
}

ErrorCode RichEditHost::insert_thematic_break() {
    if (!handle_) return ErrorCode::editor_render_projection_failed;
    CHARRANGE selected{};
    SendMessageW(handle_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selected));
    const auto visible = ReadWide(handle_);
    const auto line_ending = fileio::DetectLineEnding(projection_.text);
    if (selected.cpMax < 0 || static_cast<std::size_t>(selected.cpMax) > visible.size())
        return ErrorCode::editor_selection_mapping_failed;
    const auto caret = PrefixUtf8Size(handle_, selected.cpMax, line_ending);
    if (caret >= projection_.source_offsets.size()) return ErrorCode::editor_selection_mapping_failed;
    auto result = editor_.set_selection(
        {projection_.source_offsets[caret], projection_.source_offsets[caret]});
    if (result == ErrorCode::ok) result = block_formatter_.insert_thematic_break();
    return result == ErrorCode::ok ? project() : result;
}

ErrorCode RichEditHost::toggle_unordered_list() {
    auto result = MapControlSelection(handle_, projection_, editor_);
    if (result == ErrorCode::ok) result = list_editor_.toggle_unordered();
    return result == ErrorCode::ok ? project() : result;
}

ErrorCode RichEditHost::toggle_ordered_list(std::uint32_t start) {
    auto result = MapControlSelection(handle_, projection_, editor_);
    if (result == ErrorCode::ok) result = list_editor_.toggle_ordered(start);
    return result == ErrorCode::ok ? project() : result;
}

ErrorCode RichEditHost::toggle_task_list() {
    auto result = MapControlSelection(handle_, projection_, editor_);
    if (result == ErrorCode::ok) result = list_editor_.toggle_task();
    return result == ErrorCode::ok ? project() : result;
}

ErrorCode RichEditHost::toggle_task_checked() {
    auto result = MapControlSelection(handle_, projection_, editor_);
    if (result == ErrorCode::ok) result = list_editor_.toggle_checked();
    return result == ErrorCode::ok ? project() : result;
}

ErrorCode RichEditHost::indent_list() {
    auto result = MapControlSelection(handle_, projection_, editor_);
    if (result == ErrorCode::ok) result = list_editor_.indent();
    return result == ErrorCode::ok ? project() : result;
}

ErrorCode RichEditHost::outdent_list() {
    auto result = MapControlSelection(handle_, projection_, editor_);
    if (result == ErrorCode::ok) result = list_editor_.outdent();
    return result == ErrorCode::ok ? project() : result;
}

void RichEditHost::set_document_path(std::filesystem::path path) {
    document_path_ = std::move(path);
    if (handle_) static_cast<void>(project());
}

ErrorCode RichEditHost::insert_image_reference(std::string_view target,
    std::string_view alternative, std::string_view title) {
    auto result = MapControlSelection(handle_, projection_, editor_);
    if (result == ErrorCode::ok) result = image_controller_.insert_reference(target, alternative, title);
    return result == ErrorCode::ok ? project() : result;
}

ErrorCode RichEditHost::insert_image_file(const std::filesystem::path& image,
    bool copy_to_assets, std::string_view alternative) {
    if (document_path_.empty()) return ErrorCode::image_import_failed;
    auto result = MapControlSelection(handle_, projection_, editor_);
    if (result == ErrorCode::ok)
        result = image_controller_.insert_file(document_path_, image, copy_to_assets, alternative);
    return result == ErrorCode::ok ? project() : result;
}

ErrorCode RichEditHost::replace_image(document::NodeId image, std::string_view target,
    std::string_view alternative, std::string_view title) {
    const auto result = image_controller_.replace(image, target, alternative, title);
    return result == ErrorCode::ok ? project() : result;
}

ErrorCode RichEditHost::resize_image(document::NodeId image, std::uint16_t percent) {
    const auto result = image_controller_.set_display_percent(image, percent);
    return result == ErrorCode::ok ? project() : result;
}

ErrorCode RichEditHost::remove_image(document::NodeId image) {
    const auto result = document_path_.empty()
        ? image_controller_.remove(image)
        : image_controller_.remove_managed(document_path_, image);
    return result == ErrorCode::ok ? project() : result;
}

ErrorCode RichEditHost::insert_table(std::size_t rows, std::size_t columns) {
    auto result = MapControlSelection(handle_, projection_, editor_);
    if (result == ErrorCode::ok) result = table_editor_.insert(rows, columns);
    return result == ErrorCode::ok ? project() : result;
}
ErrorCode RichEditHost::set_table_cell(document::NodeId table, TablePosition cell,
                                       std::string_view text) {
    const auto result = table_editor_.set_cell(table, cell, text);
    return result == ErrorCode::ok ? project() : result;
}
Result<TablePosition> RichEditHost::navigate_table(document::NodeId table,
    TablePosition cell, bool forward) {
    auto result = table_editor_.navigate(table, cell, forward);
    if (!result.is_ok()) return result;
    const auto position = result.value();
    const auto projected = project();
    return projected == ErrorCode::ok ? Result<TablePosition>::success(position)
                                      : Result<TablePosition>::failure(projected);
}
ErrorCode RichEditHost::insert_table_row(document::NodeId table, std::size_t before) {
    const auto result = table_editor_.insert_row(table, before);
    return result == ErrorCode::ok ? project() : result;
}
ErrorCode RichEditHost::delete_table_row(document::NodeId table, std::size_t row) {
    const auto result = table_editor_.delete_row(table, row);
    return result == ErrorCode::ok ? project() : result;
}
ErrorCode RichEditHost::insert_table_column(document::NodeId table, std::size_t before) {
    const auto result = table_editor_.insert_column(table, before);
    return result == ErrorCode::ok ? project() : result;
}
ErrorCode RichEditHost::delete_table_column(document::NodeId table, std::size_t column) {
    const auto result = table_editor_.delete_column(table, column);
    return result == ErrorCode::ok ? project() : result;
}
ErrorCode RichEditHost::paste_table(document::NodeId table, TablePosition start,
                                    std::string_view tsv) {
    const auto result = table_editor_.paste_tsv(table, start, tsv);
    return result == ErrorCode::ok ? project() : result;
}
ErrorCode RichEditHost::remove_table(document::NodeId table) {
    const auto result = table_editor_.remove(table);
    return result == ErrorCode::ok ? project() : result;
}

Result<TextSelection> RichEditHost::find_text(std::string_view query, bool forward,
    bool case_sensitive, bool wrap) {
    auto result = find_replace_controller_.find(query, forward, case_sensitive, wrap);
    if (result.is_ok()) SelectSourceRange(handle_, projection_, result.value());
    return result;
}
ErrorCode RichEditHost::replace_text(std::string_view query,
    std::string_view replacement, bool case_sensitive) {
    const auto result = find_replace_controller_.replace_current(query, replacement, case_sensitive);
    return result == ErrorCode::ok ? project() : result;
}
Result<std::size_t> RichEditHost::replace_all_text(std::string_view query,
    std::string_view replacement, bool case_sensitive) {
    auto result = find_replace_controller_.replace_all(query, replacement, case_sensitive);
    if (!result.is_ok()) return result;
    const auto count = result.value();
    const auto projected = project();
    return projected == ErrorCode::ok ? Result<std::size_t>::success(count)
                                      : Result<std::size_t>::failure(projected);
}
ErrorCode RichEditHost::paste_plain(std::string_view text) {
    auto result = MapControlSelection(handle_, projection_, editor_);
    if (result == ErrorCode::ok) result = clipboard_controller_.paste_plain(text);
    return result == ErrorCode::ok ? project() : result;
}
ErrorCode RichEditHost::paste_html(std::string_view html) {
    auto result = MapControlSelection(handle_, projection_, editor_);
    if (result == ErrorCode::ok) result = clipboard_controller_.paste_html(html);
    return result == ErrorCode::ok ? project() : result;
}
Result<DropResult> RichEditHost::drop_files(
    std::span<const std::filesystem::path> files, bool copy_images_to_assets) {
    auto mapped = MapControlSelection(handle_, projection_, editor_);
    if (mapped != ErrorCode::ok) return Result<DropResult>::failure(mapped);
    auto result = clipboard_controller_.drop_files(document_path_, files, copy_images_to_assets);
    if (!result.is_ok()) return result;
    const auto value = result.value();
    const auto projected = project();
    return projected == ErrorCode::ok ? Result<DropResult>::success(value)
                                      : Result<DropResult>::failure(projected);
}
ErrorCode RichEditHost::paste_from_clipboard() {
    const auto mapped = MapControlSelection(handle_, projection_, editor_);
    if (mapped != ErrorCode::ok) return mapped;
    if (!OpenClipboard(handle_)) return ErrorCode::editor_unmapped_rich_edit_change;
    ErrorCode result = ErrorCode::editor_unmapped_rich_edit_change;
    if (const auto bitmap = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP))) {
        result = clipboard_controller_.paste_bitmap(document_path_, bitmap);
    } else if (const auto html_format = RegisterClipboardFormatW(L"HTML Format");
               html_format != 0 && IsClipboardFormatAvailable(html_format)) {
        const auto data = GetClipboardData(html_format);
        const auto* bytes = data ? static_cast<const char*>(GlobalLock(data)) : nullptr;
        if (bytes) {
            std::string html(bytes);
            const auto begin = html.find("<!--StartFragment-->");
            const auto end = html.find("<!--EndFragment-->");
            if (begin != std::string::npos && end != std::string::npos && end >= begin + 20)
                html = html.substr(begin + 20, end - begin - 20);
            result = clipboard_controller_.paste_html(html);
            GlobalUnlock(data);
        }
    } else if (const auto data = GetClipboardData(CF_UNICODETEXT)) {
        const auto* text = static_cast<const wchar_t*>(GlobalLock(data));
        if (text) { result = clipboard_controller_.paste_plain(ToUtf8(text)); GlobalUnlock(data); }
    }
    CloseClipboard();
    return result == ErrorCode::ok ? project() : result;
}
ErrorCode RichEditHost::copy() { SendMessageW(handle_, WM_COPY, 0, 0); return ErrorCode::ok; }
ErrorCode RichEditHost::cut() {
    static_cast<void>(copy());
    auto result = MapControlSelection(handle_, projection_, editor_);
    if (result != ErrorCode::ok) {
        CHARRANGE selected{};
        SendMessageW(handle_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selected));
        if (selected.cpMin == 0 && selected.cpMax >= GetWindowTextLengthW(handle_)) {
            const auto size = session_.snapshot().source.size();
            result = editor_.set_selection({0, size});
        }
    }
    if (result == ErrorCode::ok) result = editor_.insert_text({});
    return result == ErrorCode::ok ? project() : result;
}
ErrorCode RichEditHost::select_all() {
    SendMessageW(handle_, EM_SETSEL, 0, -1); return ErrorCode::ok;
}
ErrorCode RichEditHost::execute(EditorCommand command) {
    switch (command) {
        case EditorCommand::bold: return toggle_inline(InlineFormat::bold);
        case EditorCommand::italic: return toggle_inline(InlineFormat::italic);
        case EditorCommand::strike: return toggle_inline(InlineFormat::strike);
        case EditorCommand::inline_code: return toggle_inline(InlineFormat::code);
        case EditorCommand::quote: return toggle_quote();
        case EditorCommand::unordered_list: return toggle_unordered_list();
        case EditorCommand::ordered_list: return toggle_ordered_list();
        case EditorCommand::task_list: return toggle_task_list();
    }
    return ErrorCode::editor_unmapped_rich_edit_change;
}

ErrorCode RichEditHost::undo() {
    const auto result = image_controller_.undo();
    if (result == ErrorCode::ok || result == ErrorCode::image_restore_name_conflict) {
        const auto projected = project();
        return projected == ErrorCode::ok ? result : projected;
    }
    return result;
}

ErrorCode RichEditHost::redo() {
    const auto result = image_controller_.redo();
    return result == ErrorCode::ok ? project() : result;
}

HWND RichEditHost::handle() const noexcept { return handle_; }

}  // namespace markdownmay::editor
