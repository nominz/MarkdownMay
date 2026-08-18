#include "markdownmay/editor/richedit_host.hpp"

#include "markdownmay/fileio/line_endings.hpp"

#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <commctrl.h>
#include <windowsx.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstdlib>
#include <regex>
#include <string>

namespace markdownmay::editor {
namespace {

constexpr int kSelectionMarginDips = 8;
constexpr LONG kFoldGutterTwips = 360;
constexpr int kFoldCenterDips = 16;
constexpr int kFoldHitRightDips = 30;

LRESULT CALLBACK RichEditSubclass(HWND window, UINT message, WPARAM w_param,
                                  LPARAM l_param, UINT_PTR, DWORD_PTR reference) {
    auto* self = reinterpret_cast<RichEditHost*>(reference);
    if (message == WM_KEYDOWN && w_param == VK_OEM_4 &&
        (GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
        (GetKeyState(VK_SHIFT) & 0x8000) != 0 && self &&
        self->toggle_heading_fold_at_caret()) return 0;
    if (message == WM_LBUTTONDOWN && self &&
        self->handle_heading_fold_click({GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)}))
        return 0;
    if (message == WM_CHAR && w_param == L'-') {
        CHARRANGE selected{};
        SendMessageW(window, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selected));
        const auto line = static_cast<LONG>(SendMessageW(window, EM_LINEFROMCHAR,
            static_cast<WPARAM>(selected.cpMin), 0));
        const auto line_begin = static_cast<LONG>(SendMessageW(window, EM_LINEINDEX, line, 0));
        if (selected.cpMin == selected.cpMax && selected.cpMin - line_begin == 2) {
            wchar_t markers[3]{};
            TEXTRANGEW range{{line_begin, selected.cpMin}, markers};
            SendMessageW(window, EM_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&range));
            if (markers[0] == L'-' && markers[1] == L'-') {
                if (self && self->complete_thematic_break() == ErrorCode::ok) return 0;
            }
        }
    }
    if (message == WM_MOUSEWHEEL) {
        const auto delta = GET_WHEEL_DELTA_WPARAM(w_param);
        if (delta != 0) {
            const auto lines = -3 * delta / WHEEL_DELTA;
            SendMessageW(window, EM_LINESCROLL, 0, lines);
            return 0;
        }
    }
    const auto result = DefSubclassProc(window, message, w_param, l_param);
    if (self && message == WM_PAINT) {
        const auto dc = GetDC(window);
        if (dc) {
            self->draw_table_grid(dc);
            self->draw_heading_folds(dc);
            ReleaseDC(window, dc);
        }
        // RichEdit may scroll the caret into view while laying out hidden text.
        // Restore only after that paint/layout pass has completed.
        self->restore_heading_fold_scroll();
    } else if (self && message == WM_PRINTCLIENT) {
        self->draw_table_grid(reinterpret_cast<HDC>(w_param));
        self->draw_heading_folds(reinterpret_cast<HDC>(w_param));
    }
    if (message == WM_SIZE) {
        RECT formatting{};
        GetClientRect(window, &formatting);
        const auto inset = MulDiv(8, static_cast<int>(GetDpiForWindow(window)), 96);
        formatting.left += MulDiv(kSelectionMarginDips,
            static_cast<int>(GetDpiForWindow(window)), 96);
        formatting.top += inset;
        formatting.right -= inset;
        formatting.bottom -= inset;
        SendMessageW(window, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&formatting));
    }
    return result;
}

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

std::vector<LONG> BuildUtf16Positions(std::string_view text) {
    std::vector<LONG> positions(text.size() + 1U);
    LONG utf16{};
    for (std::size_t index = 0; index < text.size();) {
        positions[index] = utf16;
        const auto first = static_cast<unsigned char>(text[index]);
        if (first == '\r' && index + 1U < text.size() && text[index + 1U] == '\n') {
            positions[index + 1U] = utf16 + 1;
            utf16 += 1;
            index += 2U;
            positions[index] = utf16;
            continue;
        }
        std::size_t bytes = 1U;
        LONG units = 1;
        if ((first & 0xf8U) == 0xf0U && index + 3U < text.size()) {
            bytes = 4U; units = 2;
        } else if ((first & 0xf0U) == 0xe0U && index + 2U < text.size()) {
            bytes = 3U;
        } else if ((first & 0xe0U) == 0xc0U && index + 1U < text.size()) {
            bytes = 2U;
        }
        for (std::size_t byte = 1U; byte < bytes; ++byte)
            positions[index + byte] = utf16;
        index += bytes;
        utf16 += units;
        positions[index] = utf16;
    }
    positions.back() = utf16;
    return positions;
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

bool HasSameFormattingStructure(const RichProjection& left,
                                const RichProjection& right) {
    if (left.spans.size() != right.spans.size()) return false;
    for (std::size_t index = 0; index < left.spans.size(); ++index) {
        const auto& a = left.spans[index];
        const auto& b = right.spans[index];
        if (a.kind != b.kind || a.heading_level != b.heading_level ||
            a.list_depth != b.list_depth || a.task != b.task ||
            a.checked != b.checked || a.image_state != b.image_state ||
            a.image_width != b.image_width || a.image_height != b.image_height ||
            a.image_display_percent != b.image_display_percent ||
            a.image_path != b.image_path || a.table_row != b.table_row ||
            a.table_column != b.table_column || a.table_columns != b.table_columns) return false;
    }
    return true;
}

class RichEditFreeze final {
public:
    explicit RichEditFreeze(HWND handle) {
        Microsoft::WRL::ComPtr<IRichEditOle> rich_ole;
        if (SendMessageW(handle, EM_GETOLEINTERFACE, 0,
                reinterpret_cast<LPARAM>(rich_ole.GetAddressOf())) &&
            SUCCEEDED(rich_ole.As(&document_))) {
            static_cast<void>(document_->Freeze(&count_));
        }
    }
    ~RichEditFreeze() {
        if (document_) static_cast<void>(document_->Unfreeze(&count_));
    }
    RichEditFreeze(const RichEditFreeze&) = delete;
    RichEditFreeze& operator=(const RichEditFreeze&) = delete;

private:
    Microsoft::WRL::ComPtr<ITextDocument2> document_;
    long count_{};
};

Microsoft::WRL::ComPtr<ITextDocument2> TextDocumentFor(HWND handle) {
    Microsoft::WRL::ComPtr<IRichEditOle> rich_ole;
    Microsoft::WRL::ComPtr<ITextDocument2> document;
    if (SendMessageW(handle, EM_GETOLEINTERFACE, 0,
            reinterpret_cast<LPARAM>(rich_ole.GetAddressOf())))
        static_cast<void>(rich_ole.As(&document));
    return document;
}

int HeadingVerticalCenter(HWND handle, ITextDocument2* document, LONG position,
                          std::uint8_t level, UINT dpi) {
    if (document) {
        Microsoft::WRL::ComPtr<ITextRange2> range;
        long top{}, bottom{}, unused{};
        if (SUCCEEDED(document->Range2(position, position + 1, &range)) && range &&
            SUCCEEDED(range->GetPoint(tomStart | tomClientCoord | tomAllowOffClient |
                TA_TOP, &unused, &top)) &&
            SUCCEEDED(range->GetPoint(tomStart | tomClientCoord | tomAllowOffClient |
                TA_BOTTOM, &unused, &bottom)) && bottom > top)
            return static_cast<int>(top + (bottom - top) / 2);
    }
    POINT point{};
    SendMessageW(handle, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&point), position);
    const auto points = 28 - (std::min)(level, std::uint8_t{6}) * 2;
    return point.y + MulDiv(points, static_cast<int>(dpi), 144);
}

void ApplySpan(HWND handle, const ProjectionSpan& span,
        std::span<const LONG> utf16_positions, COLORREF text_color,
        COLORREF background_color, bool insert_image) {
    const bool dark = GetRValue(background_color) + GetGValue(background_color) +
        GetBValue(background_color) < 384;
    const auto begin = utf16_positions[(std::min)(
        static_cast<std::size_t>(span.begin), utf16_positions.size() - 1U)];
    const auto end = utf16_positions[(std::min)(
        static_cast<std::size_t>(span.end), utf16_positions.size() - 1U)];
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
        format.crBackColor = dark ? RGB(55, 55, 58) : RGB(238, 238, 238);
        wcscpy_s(format.szFaceName, L"Consolas");
    } else if (span.kind == document::NodeKind::link) {
        format.dwMask = CFM_UNDERLINE | CFM_COLOR;
        format.dwEffects = CFE_UNDERLINE;
        format.crTextColor = dark ? RGB(105, 175, 245) : RGB(0, 102, 204);
    } else if (span.kind == document::NodeKind::heading) {
        format.dwMask = CFM_BOLD | CFM_SIZE;
        format.dwEffects = CFE_BOLD;
        format.yHeight = static_cast<LONG>((28 - (std::min)(span.heading_level, std::uint8_t{6}) * 2) * 20);
    } else if (span.kind == document::NodeKind::code_block) {
        format.dwMask = CFM_FACE | CFM_BACKCOLOR;
        format.crBackColor = dark ? RGB(42, 42, 45) : RGB(245, 245, 245);
        wcscpy_s(format.szFaceName, L"Consolas");
    } else if (span.kind == document::NodeKind::unknown_block) {
        format.dwMask = CFM_FACE | CFM_BACKCOLOR | CFM_COLOR;
        format.crBackColor = dark ? RGB(58, 50, 40) : RGB(250, 245, 235);
        format.crTextColor = dark ? RGB(225, 194, 155) : RGB(105, 80, 55);
        wcscpy_s(format.szFaceName, L"Consolas");
    } else if (span.kind == document::NodeKind::quote) {
        format.dwMask = CFM_COLOR;
        format.crTextColor = dark ? RGB(185, 185, 185) : RGB(96, 96, 96);
    } else if (span.kind == document::NodeKind::thematic_break) {
        format.dwMask = CFM_COLOR;
        format.crTextColor = dark ? RGB(135, 135, 135) : RGB(150, 150, 150);
    } else if (span.kind == document::NodeKind::list_item && span.task) {
        format.dwMask = CFM_COLOR;
        format.crTextColor = span.checked ? (dark ? RGB(130, 190, 130) : RGB(90, 130, 90)) :
            (dark ? text_color : RGB(70, 70, 70));
    } else if (span.kind == document::NodeKind::image) {
        format.dwMask = CFM_BACKCOLOR | CFM_COLOR;
        format.crBackColor = span.image_state == ImageDisplayState::ready
            ? (dark ? RGB(35, 55, 68) : RGB(235, 245, 252))
            : (dark ? RGB(67, 49, 38) : RGB(250, 240, 230));
        format.crTextColor = span.image_state == ImageDisplayState::ready
            ? (dark ? RGB(145, 205, 235) : RGB(35, 90, 125))
            : (dark ? RGB(230, 165, 120) : RGB(145, 80, 45));
    } else if (span.kind == document::NodeKind::table_cell) {
        format.dwMask = CFM_BACKCOLOR | CFM_BOLD;
        if (span.table_row == 0) {
            format.dwEffects = CFE_BOLD;
            format.crBackColor = dark ? RGB(54, 61, 68) : RGB(232, 232, 232);
        } else {
            format.dwEffects = 0;
            format.crBackColor = span.table_row % 2 == 0
                ? (dark ? RGB(44, 44, 47) : RGB(242, 242, 242))
                : background_color;
        }
    }
    SendMessageW(handle, EM_SETCHARFORMAT, SCF_SELECTION,
                 reinterpret_cast<LPARAM>(&format));
    if (insert_image && span.kind == document::NodeKind::image &&
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
            ? PFM_TABSTOPS | PFM_SPACEBEFORE | PFM_SPACEAFTER : PFM_STARTINDENT;
        if (span.kind == document::NodeKind::quote)
            paragraph.dxStartIndent = kFoldGutterTwips + 360;
        else if (span.kind == document::NodeKind::list_item)
            paragraph.dxStartIndent = kFoldGutterTwips + 360 +
                static_cast<LONG>(span.list_depth) * 360;
        else if (span.kind == document::NodeKind::table) {
            RECT formatting{};
            SendMessageW(handle, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&formatting));
            const auto columns = (std::max)(1L, static_cast<LONG>(span.table_columns));
            const auto width_twips = (std::max)(1440L, static_cast<LONG>(MulDiv(
                formatting.right - formatting.left, 1440,
                static_cast<int>(GetDpiForWindow(handle)))));
            paragraph.cTabCount = static_cast<SHORT>((std::min)(columns - 1, 31L));
            for (LONG index = 0; index < paragraph.cTabCount; ++index)
                paragraph.rgxTabs[index] = (index + 1) * width_twips / columns;
            paragraph.dySpaceBefore = 100;
            paragraph.dySpaceAfter = 100;
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
    if (handle_ && IsWindow(handle_)) {
        RemoveWindowSubclass(handle_, RichEditSubclass, 1);
        DestroyWindow(handle_);
    }
    if (rich_edit_module_) FreeLibrary(rich_edit_module_);
}

ErrorCode RichEditHost::create(HWND parent, const RECT& bounds) {
    if (handle_) return ErrorCode::ok;
    rich_edit_module_ = LoadLibraryExW(L"msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!rich_edit_module_) return ErrorCode::editor_render_projection_failed;
    handle_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
        WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL,
        bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!handle_) return ErrorCode::editor_render_projection_failed;
    if (!SetWindowSubclass(handle_, RichEditSubclass, 1,
            reinterpret_cast<DWORD_PTR>(this)))
        return ErrorCode::editor_render_projection_failed;
    const auto event_mask = static_cast<DWORD>(
        SendMessageW(handle_, EM_GETEVENTMASK, 0, 0));
    SendMessageW(handle_, EM_SETEVENTMASK, 0, event_mask | ENM_CHANGE | ENM_SELCHANGE);
    SendMessageW(handle_, EM_SETLIMITTEXT, 0, 0);
    SendMessageW(handle_, EM_SETUNDOLIMIT, 0, 0);
    RECT client{};
    GetClientRect(handle_, &client);
    SendMessageW(handle_, WM_SIZE, 0, MAKELPARAM(client.right, client.bottom));
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
    CHARRANGE selection{};
    SendMessageW(handle_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    POINT scroll{};
    SendMessageW(handle_, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
    SendMessageW(handle_, WM_SETREDRAW, FALSE, 0);
    RichEditFreeze freeze(handle_);
    projection_ = BuildInlineProjection(*snapshot.semantic, snapshot.source, document_path_);
    const auto utf16_positions = BuildUtf16Positions(projection_.text);
    const auto rich_text = ToWide(fileio::NormalizeLineEndings(
        projection_.text, fileio::LineEnding::crlf));
    const auto success = SetWindowTextW(handle_, rich_text.c_str()) != 0 || rich_text.empty();
    if (!success) {
        SendMessageW(handle_, WM_SETREDRAW, TRUE, 0);
        projecting_ = false;
        return ErrorCode::editor_render_projection_failed;
    }
    for (const auto& span : projection_.spans) {
        if (span.kind == document::NodeKind::image)
            ApplySpan(handle_, span, utf16_positions,
                text_color_, background_color_, true);
    }
    apply_appearance(text_color_, background_color_, dpi_);
    apply_heading_folds();
    const auto length = static_cast<LONG>(rich_text.size());
    selection.cpMin = (std::min)(selection.cpMin, length);
    selection.cpMax = (std::min)(selection.cpMax, length);
    SendMessageW(handle_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    SendMessageW(handle_, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
    projecting_ = false;
    SendMessageW(handle_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(handle_, nullptr, TRUE);
    return ErrorCode::ok;
}

void RichEditHost::apply_appearance(COLORREF text, COLORREF background, UINT dpi) {
    text_color_ = text; background_color_ = background; dpi_ = dpi ? dpi : 96;
    if (!handle_) return;
    const auto was_projecting = projecting_;
    projecting_ = true;
    RichEditFreeze freeze(handle_);
    SendMessageW(handle_, EM_SETBKGNDCOLOR, 0, background_color_);
    CHARRANGE selection{};
    SendMessageW(handle_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    CHARFORMAT2W format{};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR | CFM_BACKCOLOR | CFM_FACE | CFM_SIZE;
    format.crTextColor = text_color_;
    format.crBackColor = background_color_;
    format.yHeight = 220;
    wcscpy_s(format.szFaceName, L"Microsoft YaHei UI");
    SendMessageW(handle_, EM_SETCHARFORMAT, SCF_DEFAULT,
        reinterpret_cast<LPARAM>(&format));
    CHARFORMAT2W base{};
    base.cbSize = sizeof(base);
    base.dwMask = CFM_COLOR | CFM_BACKCOLOR;
    base.crTextColor = text_color_;
    base.crBackColor = background_color_;
    SendMessageW(handle_, EM_SETSEL, 0, -1);
    SendMessageW(handle_, EM_SETCHARFORMAT, SCF_SELECTION,
        reinterpret_cast<LPARAM>(&base));
    PARAFORMAT2 base_paragraph{};
    base_paragraph.cbSize = sizeof(base_paragraph);
    base_paragraph.dwMask = PFM_STARTINDENT;
    base_paragraph.dxStartIndent = kFoldGutterTwips;
    SendMessageW(handle_, EM_SETPARAFORMAT, 0,
        reinterpret_cast<LPARAM>(&base_paragraph));
    const auto utf16_positions = BuildUtf16Positions(projection_.text);
    for (const auto& span : projection_.spans)
        ApplySpan(handle_, span, utf16_positions,
            text_color_, background_color_, false);
    apply_heading_folds();
    SendMessageW(handle_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    projecting_ = was_projecting;
    InvalidateRect(handle_, nullptr, TRUE);
}

void RichEditHost::set_heading_folds(HeadingFoldController* folds) {
    folds_ = folds;
    apply_heading_folds();
}

void RichEditHost::apply_heading_folds() {
    if (!handle_) return;
    POINT scroll{};
    CHARRANGE selection{};
    SendMessageW(handle_, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
    SendMessageW(handle_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    SendMessageW(handle_, WM_SETREDRAW, FALSE, 0);
    {
        RichEditFreeze freeze(handle_);
        const auto document = TextDocumentFor(handle_);
        const auto set_hidden = [&document](LONG begin, LONG end, long hidden) {
            if (!document || begin > end) return;
            Microsoft::WRL::ComPtr<ITextRange2> range;
            Microsoft::WRL::ComPtr<ITextFont2> font;
            if (SUCCEEDED(document->Range2(begin, end, &range)) && range &&
                SUCCEEDED(range->GetFont2(&font)) && font)
                static_cast<void>(font->SetHidden(hidden));
        };
        set_hidden(0, static_cast<LONG>(GetWindowTextLengthW(handle_)), tomFalse);
        if (document && folds_ &&
            folds_->revision() == session_.snapshot().source_revision) {
            const auto utf16 = BuildUtf16Positions(projection_.text);
            for (const auto& item : folds_->items()) {
                if (!item.collapsed) continue;
                const auto begin_it = std::lower_bound(projection_.source_offsets.begin(),
                    projection_.source_offsets.end(), item.body_range.begin);
                const auto end_it = std::lower_bound(projection_.source_offsets.begin(),
                    projection_.source_offsets.end(), item.body_range.end);
                const auto begin = static_cast<std::size_t>(
                    begin_it - projection_.source_offsets.begin());
                const auto end = static_cast<std::size_t>(
                    end_it - projection_.source_offsets.begin());
                if (begin >= end || begin >= utf16.size() || end >= utf16.size()) continue;
                set_hidden(utf16[begin], utf16[end], tomTrue);
            }
        }
    }
    SendMessageW(handle_, WM_SETREDRAW, TRUE, 0);
    SendMessageW(handle_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    pending_fold_scroll_ = scroll;
    fold_scroll_pending_ = true;
    InvalidateRect(handle_, nullptr, TRUE);
    SendMessageW(handle_, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
}

void RichEditHost::restore_heading_fold_scroll() {
    if (!handle_ || !fold_scroll_pending_) return;
    fold_scroll_pending_ = false;
    SendMessageW(handle_, EM_SETSCROLLPOS, 0,
        reinterpret_cast<LPARAM>(&pending_fold_scroll_));
    InvalidateRect(handle_, nullptr, TRUE);
}

void RichEditHost::draw_heading_folds(HDC dc) const {
    if (!handle_ || !dc || !folds_) return;
    const auto utf16 = BuildUtf16Positions(projection_.text);
    const auto size = (std::max)(8, MulDiv(10, static_cast<int>(dpi_), 96));
    const auto color = GetRValue(background_color_) < 128
        ? RGB(220, 220, 220) : RGB(70, 70, 70);
    const auto brush = CreateSolidBrush(color);
    const auto old_brush = SelectObject(dc, brush);
    const auto old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    const auto document = TextDocumentFor(handle_);
    RECT client{};
    GetClientRect(handle_, &client);
    for (const auto& item : folds_->items()) {
        const auto hidden_by_parent = std::any_of(folds_->items().begin(),
            folds_->items().end(), [&item](const auto& parent) {
                return parent.collapsed && parent.node_id != item.node_id &&
                    item.heading_range.begin >= parent.body_range.begin &&
                    item.heading_range.begin < parent.body_range.end;
            });
        if (hidden_by_parent) continue;
        const auto position = std::lower_bound(projection_.source_offsets.begin(),
            projection_.source_offsets.end(), item.heading_range.begin);
        const auto index = static_cast<std::size_t>(position - projection_.source_offsets.begin());
        if (index >= utf16.size()) continue;
        POINT approximate{};
        SendMessageW(handle_, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&approximate),
            utf16[index]);
        if (approximate.y < client.top - size * 2 ||
            approximate.y > client.bottom + size * 2) continue;
        const auto x = MulDiv(kFoldCenterDips, static_cast<int>(dpi_), 96);
        const auto y = HeadingVerticalCenter(handle_, document.Get(), utf16[index],
            item.level, dpi_);
        POINT triangle[3]{};
        if (item.collapsed) {
            triangle[0] = {x, y - size / 2};
            triangle[1] = {x, y + size / 2};
            triangle[2] = {x + size, y};
        } else {
            triangle[0] = {x - size / 2, y - size / 3};
            triangle[1] = {x + size / 2, y - size / 3};
            triangle[2] = {x, y + size / 2};
        }
        Polygon(dc, triangle, 3);
    }
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(brush);
}

bool RichEditHost::handle_heading_fold_click(POINT point) {
    const auto left = MulDiv(kSelectionMarginDips, static_cast<int>(dpi_), 96);
    const auto right = MulDiv(kFoldHitRightDips, static_cast<int>(dpi_), 96);
    if (!handle_ || !folds_ || point.x < left || point.x > right) return false;
    const auto utf16 = BuildUtf16Positions(projection_.text);
    const auto tolerance = MulDiv(10, static_cast<int>(dpi_), 96);
    const auto document = TextDocumentFor(handle_);
    for (const auto& item : folds_->items()) {
        const auto hidden_by_parent = std::any_of(folds_->items().begin(),
            folds_->items().end(), [&item](const auto& parent) {
                return parent.collapsed && parent.node_id != item.node_id &&
                    item.heading_range.begin >= parent.body_range.begin &&
                    item.heading_range.begin < parent.body_range.end;
            });
        if (hidden_by_parent) continue;
        const auto position = std::lower_bound(projection_.source_offsets.begin(),
            projection_.source_offsets.end(), item.heading_range.begin);
        const auto index = static_cast<std::size_t>(position - projection_.source_offsets.begin());
        if (index >= utf16.size()) continue;
        POINT approximate{};
        SendMessageW(handle_, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&approximate),
            utf16[index]);
        if (std::abs(approximate.y - point.y) > tolerance * 3) continue;
        const auto center = HeadingVerticalCenter(handle_, document.Get(), utf16[index],
            item.level, dpi_);
        if (point.y >= center - tolerance && point.y <= center + tolerance)
            return folds_->toggle(item.node_id);
    }
    return false;
}

bool RichEditHost::toggle_heading_fold_at_caret() {
    if (!folds_) return false;
    const auto selection = source_selection();
    return selection.is_ok() && folds_->toggle_at(selection.value().caret);
}

void RichEditHost::draw_table_grid(HDC dc) const {
    if (!handle_ || !dc || projection_.spans.empty()) return;
    const auto utf16 = BuildUtf16Positions(projection_.text);
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    const auto padding = MulDiv(5, static_cast<int>(dpi_), 96);
    const auto line_color = GetRValue(background_color_) < 128
        ? RGB(112, 112, 116) : RGB(176, 176, 176);
    const auto pen = CreatePen(PS_SOLID, (std::max)(1, MulDiv(1,
        static_cast<int>(dpi_), 96)), line_color);
    const auto old_pen = SelectObject(dc, pen);
    RECT client{};
    GetClientRect(handle_, &client);

    for (const auto& table : projection_.spans) {
        if (table.kind != document::NodeKind::table) continue;
        const auto table_index = (std::min)(static_cast<std::size_t>(table.begin),
            projection_.source_offsets.empty() ? std::size_t{} :
                projection_.source_offsets.size() - 1U);
        const auto table_source = projection_.source_offsets.empty()
            ? std::uint64_t{} : projection_.source_offsets[table_index];
        const auto hidden_by_fold = folds_ && std::any_of(folds_->items().begin(),
            folds_->items().end(), [table_source](const auto& item) {
                return item.collapsed && table_source >= item.body_range.begin &&
                    table_source < item.body_range.end;
            });
        if (hidden_by_fold) continue;
        std::uint32_t rows{}, columns{};
        for (const auto& cell : projection_.spans) {
            if (cell.kind != document::NodeKind::table_cell ||
                cell.begin < table.begin || cell.end > table.end) continue;
            rows = (std::max)(rows, cell.table_row + 1);
            columns = (std::max)(columns, cell.table_column + 1);
        }
        if (!rows || !columns) continue;
        std::vector<int> verticals(columns + 1, INT_MIN);
        std::vector<int> tops(rows, INT_MAX);
        std::vector<int> bottoms(rows, INT_MIN);
        for (const auto& cell : projection_.spans) {
            if (cell.kind != document::NodeKind::table_cell ||
                cell.begin < table.begin || cell.end > table.end ||
                cell.begin >= utf16.size() || cell.end >= utf16.size()) continue;
            POINT begin{}, end{};
            SendMessageW(handle_, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&begin),
                utf16[static_cast<std::size_t>(cell.begin)]);
            SendMessageW(handle_, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&end),
                utf16[static_cast<std::size_t>(cell.end)]);
            const auto column = static_cast<std::size_t>(cell.table_column);
            const auto row = static_cast<std::size_t>(cell.table_row);
            const auto left = static_cast<int>(begin.x) - padding;
            verticals[column] = verticals[column] == INT_MIN
                ? left : (std::min)(verticals[column], left);
            verticals[columns] = (std::max)(verticals[columns],
                static_cast<int>(end.x) + padding);
            tops[row] = (std::min)(tops[row], static_cast<int>(begin.y) - padding / 2);
            bottoms[row] = (std::max)(bottoms[row],
                static_cast<int>(begin.y + metrics.tmHeight) + padding / 2);
        }
        for (std::size_t column = 1; column < columns; ++column) {
            if (verticals[column] == INT_MIN)
                verticals[column] = verticals[column - 1] + MulDiv(96,
                    static_cast<int>(dpi_), 96);
        }
        if (verticals[0] == INT_MIN || verticals[columns] == INT_MIN) continue;
        for (std::size_t column = 1; column <= columns; ++column)
            verticals[column] = (std::max)(verticals[column], verticals[column - 1] + padding * 2);
        const auto top = tops.front();
        const auto bottom = bottoms.back();
        if (top == INT_MAX || bottom == INT_MIN || bottom < client.top || top > client.bottom)
            continue;
        for (const auto x : verticals) {
            MoveToEx(dc, x, top, nullptr);
            LineTo(dc, x, bottom);
        }
        MoveToEx(dc, verticals.front(), top, nullptr);
        LineTo(dc, verticals.back(), top);
        for (const auto row_bottom : bottoms) {
            if (row_bottom == INT_MIN) continue;
            MoveToEx(dc, verticals.front(), row_bottom, nullptr);
            LineTo(dc, verticals.back(), row_bottom);
        }
    }
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

ErrorCode RichEditHost::show_status_message(std::wstring_view message) {
    if (!handle_) return ErrorCode::editor_render_projection_failed;
    projecting_ = true;
    const std::wstring value(message);
    const auto success = SetWindowTextW(handle_, value.c_str()) != 0 || value.empty();
    projection_ = {};
    projecting_ = false;
    return success ? ErrorCode::ok : ErrorCode::editor_render_projection_failed;
}

void RichEditHost::set_read_only(bool read_only) {
    if (handle_) SendMessageW(handle_, EM_SETREADONLY, read_only ? TRUE : FALSE, 0);
}

void RichEditHost::scroll_to_fraction(std::uint64_t numerator, std::uint64_t denominator) {
    if (!handle_ || denominator == 0) return;
    const auto lines = static_cast<LONG>((std::max)(LRESULT{1},
        SendMessageW(handle_, EM_GETLINECOUNT, 0, 0)));
    const auto target = static_cast<LONG>((numerator * static_cast<std::uint64_t>(lines)) /
                                          denominator);
    const auto current = static_cast<LONG>(SendMessageW(handle_, EM_GETFIRSTVISIBLELINE, 0, 0));
    SendMessageW(handle_, EM_LINESCROLL, 0, target - current);
}

std::pair<std::uint64_t, std::uint64_t> RichEditHost::scroll_fraction() const {
    if (!handle_) return {0, 1};
    return {static_cast<std::uint64_t>(SendMessageW(handle_, EM_GETFIRSTVISIBLELINE, 0, 0)),
        static_cast<std::uint64_t>((std::max)(LRESULT{1},
            SendMessageW(handle_, EM_GETLINECOUNT, 0, 0)))};
}

void RichEditHost::reset_to_start() {
    if (!handle_) return;
    CHARRANGE selection{0, 0};
    POINT scroll{};
    SendMessageW(handle_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    SendMessageW(handle_, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
}

Result<TextSelection> RichEditHost::source_selection() {
    if (!handle_) return Result<TextSelection>::failure(ErrorCode::editor_render_projection_failed);
    const auto result = MapControlSelection(handle_, projection_, editor_);
    if (result != ErrorCode::ok) return Result<TextSelection>::failure(result);
    return Result<TextSelection>::success(editor_.selection());
}

ErrorCode RichEditHost::select_source_range(TextSelection selection) {
    if (!handle_ || selection.anchor > session_.snapshot().source.size() ||
        selection.caret > session_.snapshot().source.size())
        return ErrorCode::editor_selection_mapping_failed;
    const auto result = editor_.set_selection(selection);
    if (result != ErrorCode::ok) return result;
    SelectSourceRange(handle_, projection_, selection);
    SendMessageW(handle_, EM_SCROLLCARET, 0, 0);
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
    auto source_begin = projection_.source_offsets[prefix];
    auto source_end = projection_.source_offsets[old_suffix];
    auto replacement = after.substr(prefix, new_suffix - prefix);
    const auto expected_eol = line_ending == fileio::LineEnding::lf ? "\n" : "\r\n";
    if (replacement == expected_eol && control_selection.cpMin == control_selection.cpMax) {
        const auto target_ending = line_ending == fileio::LineEnding::mixed
            ? fileio::LineEnding::crlf : line_ending;
        const auto caret_after = PrefixUtf8Size(handle_, control_selection.cpMin, target_ending);
        if (caret_after >= replacement.size()) {
            const auto visual_insertion = caret_after - replacement.size();
            if (visual_insertion < projection_.source_offsets.size()) {
                source_begin = projection_.source_offsets[visual_insertion];
                source_end = source_begin;
            }
        }
    }
    bool continued_list{};
    bool completed_thematic_break{};
    auto result = editor_.set_selection({source_begin, source_end});
    if (result == ErrorCode::ok) {
        const auto visible_line_begin = prefix == 0 ? 0 : after.rfind('\n', prefix - 1) + 1;
        auto visible_line_end = after.find('\n', new_suffix);
        if (visible_line_end == std::string::npos) visible_line_end = after.size();
        auto visible_content_end = visible_line_end;
        if (visible_content_end > visible_line_begin &&
            after[visible_content_end - 1] == '\r') --visible_content_end;
        completed_thematic_break =
            after.substr(visible_line_begin, visible_content_end - visible_line_begin) == "---";
        auto source_line_begin = source.rfind('\n');
        source_line_begin = source_line_begin == std::string::npos ? 0 : source_line_begin + 1;
        if (completed_thematic_break && source_line_begin > 0) {
            result = editor_.set_selection(
                {source_line_begin, static_cast<std::uint64_t>(source.size())});
            if (result == ErrorCode::ok)
                result = editor_.insert_text(std::string(expected_eol) + "---");
        } else if (replacement == expected_eol) {
            result = list_editor_.continue_item();
            continued_list = result == ErrorCode::ok;
            if (result == ErrorCode::editor_selection_mapping_failed)
                result = editor_.insert_text(replacement);
        } else {
            result = editor_.insert_text(replacement);
        }
    }
    if (result == ErrorCode::ok) {
        const auto current = session_.snapshot().source;
        const auto marker = current.rfind("---");
        if (marker != std::string::npos && marker > 0) {
            const auto marker_end = marker + 3;
            const bool edited_marker = completed_thematic_break ||
                (source_begin >= marker && source_begin <= marker_end);
            const bool line_end = marker_end == current.size() || current[marker_end] == '\r' ||
                current[marker_end] == '\n';
            auto line_begin = current.rfind('\n', marker - 1);
            line_begin = line_begin == std::string::npos ? 0 : line_begin + 1;
            if (edited_marker && line_begin == marker && line_end) {
                auto previous_end = marker;
                while (previous_end > 0 &&
                    (current[previous_end - 1] == '\r' || current[previous_end - 1] == '\n'))
                    --previous_end;
                auto previous_begin = previous_end == 0 ? std::string::npos :
                    current.rfind('\n', previous_end - 1);
                previous_begin = previous_begin == std::string::npos ? 0 : previous_begin + 1;
                if (previous_end > previous_begin) {
                    const auto ending = line_ending == fileio::LineEnding::lf ? "\n" : "\r\n";
                    const auto selected = editor_.selection();
                    const auto shifted = selected.caret + std::char_traits<char>::length(ending);
                    result = editor_.replace_source_range(marker, marker, ending,
                        {shifted, shifted});
                }
            }
        }
    }
    if (result != ErrorCode::ok) {
        static_cast<void>(project());
        return result;
    }
    const auto updated = session_.snapshot();
    if (updated.semantic) {
        auto next_projection = BuildInlineProjection(
            *updated.semantic, updated.source, document_path_);
        if (next_projection.text == after &&
            HasSameFormattingStructure(projection_, next_projection)) {
            projection_ = std::move(next_projection);
            apply_heading_folds();
            return ErrorCode::ok;
        }
    }
    const auto projected = project();
    if (projected == ErrorCode::ok) {
        if (replacement == expected_eol && !continued_list) {
            const auto length = static_cast<LONG>(GetWindowTextLengthW(handle_));
            control_selection.cpMin = (std::min)(control_selection.cpMin, length);
            control_selection.cpMax = (std::min)(control_selection.cpMax, length);
            SendMessageW(handle_, EM_EXSETSEL, 0,
                reinterpret_cast<LPARAM>(&control_selection));
        } else {
            SelectSourceRange(handle_, projection_, editor_.selection());
        }
    }
    return projected;
}

ErrorCode RichEditHost::complete_thematic_break() {
    if (!handle_) return ErrorCode::editor_render_projection_failed;
    auto result = MapControlSelection(handle_, projection_, editor_);
    if (result != ErrorCode::ok) return result;
    const auto snapshot = session_.snapshot();
    const auto caret = editor_.selection().caret;
    auto line_begin = caret == 0 ? std::string::npos :
        snapshot.source.rfind('\n', static_cast<std::size_t>(caret - 1));
    line_begin = line_begin == std::string::npos ? 0 : line_begin + 1;
    const auto ending = fileio::DetectLineEnding(snapshot.source) == fileio::LineEnding::lf
        ? std::string("\n") : std::string("\r\n");
    const auto replacement = (line_begin > 0 ? ending : std::string{}) + "---";
    const auto next = static_cast<std::uint64_t>(line_begin + replacement.size());
    result = editor_.replace_source_range(line_begin, caret, replacement, {next, next});
    return result == ErrorCode::ok ? project() : result;
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
        case EditorCommand::clear_format: return clear_paragraph_formatting();
    }
    return ErrorCode::editor_unmapped_rich_edit_change;
}

ErrorCode RichEditHost::clear_paragraph_formatting() {
    auto mapped = MapControlSelection(handle_, projection_, editor_);
    if (mapped != ErrorCode::ok) return mapped;
    const auto snapshot = session_.snapshot();
    auto selection = editor_.selection();
    auto begin = static_cast<std::size_t>((std::min)(selection.anchor, selection.caret));
    auto end = static_cast<std::size_t>((std::max)(selection.anchor, selection.caret));
    begin = begin == 0 ? 0 : snapshot.source.rfind('\n', begin - 1) + 1;
    const auto line_end = snapshot.source.find('\n', end);
    end = line_end == std::string::npos ? snapshot.source.size() : line_end;
    std::string text = snapshot.source.substr(begin, end - begin);

    // HTML/XML tags are visually unmistakable; strip every complete tag token,
    // independently of whether a matching closing tag exists.
    text = std::regex_replace(text, std::regex(R"(<[^>\r\n]+>)"), "");
    // Block markers apply to the paragraph, one line at a time.
    text = std::regex_replace(text,
        std::regex(R"((^|\n)[ \t]{0,3}(?:#{1,6}[ \t]+|>[ \t]?|[-+*][ \t]+|\d+[.)][ \t]+))"), "$1");
    text = std::regex_replace(text,
        std::regex(R"((^|\n)[ \t]{0,3}\[[ xX]\][ \t]+)"), "$1");
    // Inline Markdown is removed only when both delimiters exist.
    const std::array<std::regex, 7> closed{{
        std::regex(R"(!\[([^\]]*)\]\([^\r\n)]*\))"),
        std::regex(R"(\[([^\]]+)\]\([^\r\n)]*\))"),
        std::regex(R"(\*\*([^\r\n]+?)\*\*)"),
        std::regex(R"(__([^\r\n]+?)__)"),
        std::regex(R"(~~([^\r\n]+?)~~)"),
        std::regex(R"(`([^\r\n`]+)`)"),
        std::regex(R"(\*([^\r\n*]+)\*|_([^\r\n_]+)_)")}};
    text = std::regex_replace(text, closed[0], "$1");
    text = std::regex_replace(text, closed[1], "$1");
    for (std::size_t index = 2; index < 6; ++index)
        text = std::regex_replace(text, closed[index], "$1");
    text = std::regex_replace(text, closed[6], "$1$2");
    const auto next = static_cast<std::uint64_t>(begin + text.size());
    const auto result = editor_.replace_source_range(begin, end, std::move(text), {next, next});
    return result == ErrorCode::ok ? project() : result;
}

bool RichEditHost::inline_active(InlineFormat format) const noexcept {
    if (!handle_) return false;
    CHARFORMAT2W value{};
    value.cbSize = sizeof(value);
    DWORD mask{};
    DWORD effect{};
    switch (format) {
    case InlineFormat::bold: mask = CFM_BOLD; effect = CFE_BOLD; break;
    case InlineFormat::italic: mask = CFM_ITALIC; effect = CFE_ITALIC; break;
    case InlineFormat::strike: mask = CFM_STRIKEOUT; effect = CFE_STRIKEOUT; break;
    case InlineFormat::code: return false;
    }
    value.dwMask = mask;
    SendMessageW(handle_, EM_GETCHARFORMAT, SCF_SELECTION,
        reinterpret_cast<LPARAM>(&value));
    return (value.dwMask & mask) != 0 && (value.dwEffects & effect) != 0;
}

std::uint8_t RichEditHost::heading_level() {
    const auto selected = source_selection();
    if (!selected.is_ok()) return 0;
    const auto caret = selected.value().caret;
    const auto snapshot = session_.snapshot();
    if (!snapshot.semantic) return 0;
    std::uint8_t level{};
    std::function<void(const document::Node&)> visit = [&](const document::Node& node) {
        if (level || caret < node.source.begin || caret > node.source.end) return;
        if (node.kind == document::NodeKind::heading) {
            if (const auto* heading = std::get_if<document::HeadingAttributes>(&node.attributes))
                level = heading->level;
            return;
        }
        for (const auto& child : node.children) visit(*child);
    };
    visit(*snapshot.semantic->root());
    return level;
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
