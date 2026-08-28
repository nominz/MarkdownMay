#include "markdownmay/editor/richedit_host.hpp"
#include "markdownmay/editor/table_layout.hpp"

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
#include <cstring>
#include <cstdlib>
#include <limits>
#include <regex>
#include <string>

namespace markdownmay::editor {
namespace {

constexpr int kSelectionMarginDips = 8;
constexpr LONG kBlockGutterTwips = 1200;
constexpr LONG kMaxNativeTableWidthTwips = 14400;
constexpr int kFoldCenterDips = 16;
constexpr int kFoldHitRightDips = 30;
constexpr int kBlockTypeLeftDips = 31;
constexpr int kBlockTypeRightDips = 53;
constexpr int kBlockHandleLeftDips = 55;
constexpr int kBlockHandleRightDips = 77;
constexpr int kBlockTypeControlId = 6101;
constexpr int kBlockHandleControlId = 6102;
constexpr UINT kBlockMenuFirst = 6201;
constexpr UINT kReprojectNativeTableMessage = WM_APP + 0x31;
constexpr UINT kProjectionNotificationsSettledMessage = WM_APP + 0x32;

std::wstring TableTracePath() {
    wchar_t directory[MAX_PATH]{};
    const auto length = GetTempPathW(MAX_PATH, directory);
    if (!length || length >= MAX_PATH) return L"MarkdownMay_table_trace.log";
    return std::wstring(directory, length) + L"MarkdownMay_table_trace.log";
}

void TraceTable(std::string_view event) {
    static const auto path = TableTracePath();
    static const bool initialized = [] {
        DeleteFileW(TableTracePath().c_str());
        return true;
    }();
    static_cast<void>(initialized);
    const auto file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    const auto prefix = std::to_string(GetTickCount64()) + " pid=" +
        std::to_string(GetCurrentProcessId()) + " ";
    DWORD written{};
    WriteFile(file, prefix.data(), static_cast<DWORD>(prefix.size()), &written, nullptr);
    WriteFile(file, event.data(), static_cast<DWORD>(event.size()), &written, nullptr);
    static constexpr char newline[] = "\r\n";
    WriteFile(file, newline, 2, &written, nullptr);
    CloseHandle(file);
}

struct RtfResetStream final {
    std::string_view text;
    std::size_t offset{};
};

DWORD CALLBACK ReadRtfReset(DWORD_PTR cookie, LPBYTE buffer, LONG capacity, LONG* copied) {
    auto* stream = reinterpret_cast<RtfResetStream*>(cookie);
    if (!stream || !buffer || capacity < 0 || !copied) return 1;
    const auto remaining = stream->text.size() - stream->offset;
    const auto count = (std::min)(remaining, static_cast<std::size_t>(capacity));
    if (count) std::memcpy(buffer, stream->text.data() + stream->offset, count);
    stream->offset += count;
    *copied = static_cast<LONG>(count);
    return 0;
}

bool ResetRichEditDocument(HWND handle) {
    // Streaming an empty RTF document replaces both text and the backing row/cell
    // format tree. SetWindowText/TOM New can leave native table descriptors alive.
    static constexpr std::string_view empty_rtf = "{\\rtf1\\ansi\\deff0\\pard }";
    RtfResetStream reset{empty_rtf};
    EDITSTREAM stream{reinterpret_cast<DWORD_PTR>(&reset), 0, ReadRtfReset};
    SendMessageW(handle, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&stream));
    return stream.dwError == 0;
}

std::string ConvertBlockSource(std::string source, const std::uint8_t heading_level) {
    std::size_t marker{};
    while (marker < source.size() && marker < 6 && source[marker] == '#') ++marker;
    if (marker > 0 && marker < source.size() && source[marker] == ' ')
        source.erase(0, marker + 1);
    if (heading_level > 0)
        source.insert(0, std::string(heading_level, '#') + " ");
    return source;
}

std::string ShiftBlockIndent(std::string_view source, const bool increase) {
    std::string result;
    std::size_t cursor{};
    while (cursor <= source.size()) {
        const auto next = source.find('\n', cursor);
        const auto end = next == std::string_view::npos ? source.size() : next;
        auto line = std::string(source.substr(cursor, end - cursor));
        if (increase) line.insert(0, "    ");
        else {
            std::size_t remove{};
            while (remove < 4 && remove < line.size() && line[remove] == ' ') ++remove;
            line.erase(0, remove);
        }
        result += line;
        if (next == std::string_view::npos) break;
        result.push_back('\n');
        cursor = next + 1;
    }
    return result;
}

LRESULT CALLBACK BlockButtonSubclass(HWND window, UINT message, WPARAM w_param,
                                     LPARAM l_param, UINT_PTR, DWORD_PTR reference) {
    auto* self = reinterpret_cast<RichEditHost*>(reference);
    if (message == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
        static_cast<void>(TrackMouseEvent(&tracking));
    } else if (message == WM_MOUSELEAVE && self) {
        POINT cursor{};
        GetCursorPos(&cursor);
        const auto hovered = WindowFromPoint(cursor);
        if (hovered != self->block_type_window() && hovered != self->block_handle_window())
            self->clear_block_hover();
    } else if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, BlockButtonSubclass, 1);
    }
    return DefSubclassProc(window, message, w_param, l_param);
}

LRESULT CALLBACK RichEditSubclass(HWND window, UINT message, WPARAM w_param,
                                  LPARAM l_param, UINT_PTR, DWORD_PTR reference) {
    auto* self = reinterpret_cast<RichEditHost*>(reference);
    LRESULT menu_result{};
    if (HandleDocumentContextMenuMessage(message, w_param, l_param, menu_result))
        return menu_result;
    if (message == WM_RBUTTONDOWN && self)
        self->remember_context_selection_at(
            {GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)});
    if (message == WM_CONTEXTMENU && self && self->handle() == window) {
        self->restore_context_selection();
        POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        if (point.x == -1 && point.y == -1) {
            CHARRANGE selection{};
            SendMessageW(window, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
            SendMessageW(window, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&point), selection.cpMin);
            point.y += MulDiv(24, GetDpiForWindow(window), 96);
            ClientToScreen(window, &point);
        }
        if (self->show_document_context_menu(point)) return 0;
    }
    if (self && (message == WM_SIZE || message == WM_LBUTTONDOWN ||
            message == WM_LBUTTONUP || message == WM_CAPTURECHANGED)) {
        self->trace_table_event("richedit message=" + std::to_string(message) +
            " w=" + std::to_string(static_cast<unsigned long long>(w_param)) +
            " l=" + std::to_string(static_cast<long long>(l_param)) +
            " capture=" + std::to_string(GetCapture() == window));
    }
    if (message == kReprojectNativeTableMessage && self) {
        static_cast<void>(self->run_deferred_reproject());
        return 0;
    }
    if (message == kProjectionNotificationsSettledMessage && self) {
        self->complete_projection_notification_window();
        return 0;
    }
    if (message == WM_KEYDOWN && w_param == VK_OEM_4 &&
        (GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
        (GetKeyState(VK_SHIFT) & 0x8000) != 0 && self &&
        self->toggle_heading_fold_at_caret()) return 0;
    if (message == WM_KEYDOWN && self &&
        (w_param == VK_APPS || (w_param == VK_F10 &&
            (GetKeyState(VK_SHIFT) & 0x8000) != 0)) &&
        self->show_block_context_menu_at_caret()) return 0;
    if (message == WM_LBUTTONDOWN && self &&
        self->handle_heading_fold_click({GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)}))
        return 0;
    if (message == WM_LBUTTONDOWN && self &&
        self->handle_list_marker_click({GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)}))
        return 0;
    if (message == WM_LBUTTONDOWN && self &&
        self->handle_block_handle_click({GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)}))
        return 0;
    if ((message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK) && self &&
        self->is_native_table_column_boundary(
            {GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)})) {
        // RichEdit's native table ruler mutates RTF-only cell widths outside the
        // DocumentSession transaction model. Markdown has no portable column-width
        // syntax, so suppress that private mutation until an application-owned
        // view-state resize interaction is introduced.
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return 0;
    }
    if ((message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK) && self)
        static_cast<void>(self->begin_native_table_pointer_gesture(
            {GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)}));
    if (message == WM_LBUTTONUP && self) {
        const auto result = DefSubclassProc(window, message, w_param, l_param);
        self->end_native_table_pointer_gesture();
        return result;
    }
    if (message == WM_CAPTURECHANGED && self)
        self->end_native_table_pointer_gesture();
    if (message == WM_SETCURSOR && self && LOWORD(l_param) == HTCLIENT) {
        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(window, &point);
        if (self->is_native_table_column_boundary(point)) {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
    }
    if (message == WM_MOUSEMOVE && self)
        static_cast<void>(self->update_block_hover(
            {GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)}));
    if (message == WM_MOUSELEAVE && self) {
        POINT cursor{};
        GetCursorPos(&cursor);
        const auto hovered = WindowFromPoint(cursor);
        if (hovered != self->block_type_window() && hovered != self->block_handle_window())
            self->clear_block_hover();
    }
    if (message == WM_COMMAND && self && HIWORD(w_param) == BN_CLICKED &&
        reinterpret_cast<HWND>(l_param) == self->block_handle_window()) {
        const auto context = self->hovered_block();
        if (context) {
            RECT rect{};
            GetWindowRect(self->block_handle_window(), &rect);
            static_cast<void>(self->show_block_context_menu(*context,
                {rect.left, rect.bottom}));
        }
        return 0;
    }
    if (message == WM_COMMAND && self && HIWORD(w_param) == BN_CLICKED &&
        reinterpret_cast<HWND>(l_param) == self->block_type_window()) {
        SetFocus(window);
        return 0;
    }
    if (message == WM_DRAWITEM && self) {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(l_param);
        if (item && (item->hwndItem == self->block_type_window() ||
            item->hwndItem == self->block_handle_window())) {
            self->draw_block_accessible_button(*item);
            return TRUE;
        }
    }
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
        if (self) self->invalidate_block_layout();
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
            self->draw_quote_guides(dc);
            self->draw_inline_code_frames(dc);
            self->draw_code_block_frames(dc);
            self->draw_heading_folds(dc);
            self->draw_block_interaction(dc);
            ReleaseDC(window, dc);
        }
        // RichEdit may scroll the caret into view while laying out hidden text.
        // Restore only after that paint/layout pass has completed.
        self->restore_heading_fold_scroll();
    } else if (self && message == WM_PRINTCLIENT) {
        self->draw_table_grid(reinterpret_cast<HDC>(w_param));
        self->draw_quote_guides(reinterpret_cast<HDC>(w_param));
        self->draw_inline_code_frames(reinterpret_cast<HDC>(w_param));
        self->draw_code_block_frames(reinterpret_cast<HDC>(w_param));
        self->draw_heading_folds(reinterpret_cast<HDC>(w_param));
        self->draw_block_interaction(reinterpret_cast<HDC>(w_param));
    }
    if (self && (message == WM_SIZE || message == WM_VSCROLL || message == WM_HSCROLL))
        self->invalidate_block_layout();
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
        if (self) self->refresh_layout_after_resize();
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
    TEXTRANGEW range{{0, length}, value.data()};
    const auto copied = static_cast<LONG>(SendMessageW(
        handle, EM_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&range)));
    value.resize(static_cast<std::size_t>((std::max)(copied, 0L)));
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

std::vector<LONG> BuildPhysicalPositions(const RichProjection& projection) {
    const auto logical = BuildUtf16Positions(projection.text);
    auto physical = logical;
    LONG delta{};
    std::size_t cursor{};
    for (const auto& table : projection.tables) {
        const auto table_begin = static_cast<std::size_t>(table.begin);
        const auto table_end = static_cast<std::size_t>(table.end);
        for (; cursor <= table_begin && cursor < physical.size(); ++cursor)
            physical[cursor] = logical[cursor] + delta;
        const auto actual_begin = table.physical_begin >= 0
            ? table.physical_begin : logical[table_begin] + delta;
        for (; cursor <= table_end && cursor < physical.size(); ++cursor)
            physical[cursor] = actual_begin + (logical[cursor] - logical[table_begin]);
        for (const auto& row : table.rows) for (const auto& cell : row.cells) {
            if (cell.physical_begin < 0) continue;
            for (auto offset = static_cast<std::size_t>(cell.begin);
                    offset <= static_cast<std::size_t>(cell.end) && offset < physical.size();
                    ++offset)
                physical[offset] = cell.physical_begin +
                    (logical[offset] - logical[static_cast<std::size_t>(cell.begin)]);
        }
        if (table.physical_end >= 0)
            delta = table.physical_end - logical[table_end];
    }
    for (; cursor < physical.size(); ++cursor) physical[cursor] = logical[cursor] + delta;
    return physical;
}

Microsoft::WRL::ComPtr<ITextDocument2> TextDocumentFor(HWND handle);

bool SetNativeTableParameters(HWND handle, RichProjection& projection,
        COLORREF background, UINT dpi, bool insert) {
    if (!handle) return false;
    const auto positions = BuildPhysicalPositions(projection);
    RECT formatting{};
    SendMessageW(handle, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&formatting));
    const auto effective_dpi = dpi ? dpi : 96U;
    const auto margin = static_cast<LONG>(MulDiv(
        TableHorizontalPadding(effective_dpi), 1440, static_cast<int>(effective_dpi)));
    const bool dark = GetRValue(background) + GetGValue(background) +
        GetBValue(background) < 384;
    const auto border = dark ? RGB(112, 112, 116) : RGB(176, 176, 176);

    LONG physical_delta{};
    for (auto& table : projection.tables) {
        if (table.rows.empty() || table.rows.front().cells.empty() ||
            table.begin >= positions.size() || table.end >= positions.size()) return false;
        const auto rows = table.rows.size();
        const auto columns = table.rows.front().cells.size();
        if (rows > 255U || columns > 100U) return false;
        const auto begin = positions[static_cast<std::size_t>(table.begin)] +
            (insert ? physical_delta : 0L);
        const auto end = positions[static_cast<std::size_t>(table.end)] +
            (insert ? physical_delta : 0L);
        const auto available_pixels = (std::max)(1L, formatting.right - formatting.left);
        // RichEdit's native table row/cell positions have a practical width
        // ceiling.  Feeding a maximized 2K/4K formatting rectangle directly into
        // cumulative cellx makes both EM_SETTABLEPARMS and a later EM_INSERTTABLE
        // fail. Markdown carries no viewport-filling column width, so cap the
        // visual table at ten logical inches and let narrower windows contract it.
        const auto available_twips = (std::clamp)(static_cast<LONG>(MulDiv(
            available_pixels, 1440, static_cast<int>(effective_dpi))),
            1440L, kMaxNativeTableWidthTwips);
        const auto width = (std::max)(240L, available_twips / static_cast<LONG>(columns));

        std::vector<TABLECELLPARMS> cells(columns);
        for (std::size_t column{}; column < cells.size(); ++column) {
            auto& cell = cells[column];
            // RichEdit stores the RTF \cellx right boundary, not a per-cell delta.
            cell.dxWidth = static_cast<LONG>(column + 1U) * width;
            cell.dxBrdrLeft = cell.dyBrdrTop = cell.dxBrdrRight = cell.dyBrdrBottom = 10;
            cell.crBrdrLeft = cell.crBrdrTop = cell.crBrdrRight = cell.crBrdrBottom = border;
            cell.crBackPat = background;
            cell.crForePat = background;
        }
        TABLEROWPARMS row{};
        row.cbRow = sizeof(row);
        row.cbCell = sizeof(TABLECELLPARMS);
        row.cCell = static_cast<BYTE>(columns);
        row.cRow = static_cast<BYTE>(rows);
        row.dxCellMargin = margin;
        row.dxIndent = kBlockGutterTwips;
        row.nAlignment = PFA_LEFT;
        row.fIdentCells = FALSE;
        row.cpStartRow = insert ? -1 : begin;

        if (insert) {
            CHARRANGE replace{begin, end};
            SendMessageW(handle, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&replace));
            SendMessageW(handle, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
            const auto result = static_cast<HRESULT>(SendMessageW(handle, EM_INSERTTABLE,
                reinterpret_cast<WPARAM>(&row), reinterpret_cast<LPARAM>(cells.data())));
            if (FAILED(result)) return false;
            CHARRANGE selected{};
            SendMessageW(handle, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selected));
            static_cast<void>(selected);
            LONG cursor = begin + 2;
            table.physical_begin = begin;
            for (std::size_t row_index{}; row_index < rows; ++row_index) {
                auto& projected_row = table.rows[row_index];
                if (projected_row.cells.size() != columns) return false;
                for (std::size_t column{}; column < columns; ++column) {
                    projected_row.cells[column].physical_begin = cursor;
                    CHARRANGE target{cursor, cursor};
                    SendMessageW(handle, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&target));
                    const auto value = ToWide(projected_row.cells[column].text);
                    SendMessageW(handle, EM_REPLACESEL, FALSE,
                        reinterpret_cast<LPARAM>(value.c_str()));
                    cursor += static_cast<LONG>(value.size()) + 1;
                    projected_row.cells[column].physical_end = cursor - 1;
                }
                cursor += 4;
            }
            Microsoft::WRL::ComPtr<ITextDocument2> document = TextDocumentFor(handle);
            Microsoft::WRL::ComPtr<ITextRange2> table_range;
            long expanded{};
            long table_start{}, table_end{};
            if (!document || FAILED(document->Range2(table.physical_begin,
                    table.physical_begin, &table_range)) || !table_range ||
                FAILED(table_range->Expand(tomTable, &expanded)) ||
                FAILED(table_range->GetStart(&table_start)) ||
                FAILED(table_range->GetEnd(&table_end))) return false;
            table.physical_begin = table_start;
            table.physical_end = table_end;
            physical_delta += (table_end - table_start) - (end - begin);
        } else {
            row.cRow = 1;
            for (std::size_t row_index{}; row_index < rows; ++row_index) {
                const auto row_background = row_index == 0
                    ? (dark ? RGB(54, 61, 68) : RGB(232, 232, 232))
                    : row_index % 2 == 0
                    ? (dark ? RGB(44, 44, 47) : RGB(242, 242, 242)) : background;
                for (auto& cell : cells) {
                    cell.crBackPat = row_background;
                    cell.crForePat = row_background;
                }
                row.cpStartRow = table.rows[row_index].cells.front().physical_begin - 2;
                const auto result = static_cast<HRESULT>(SendMessageW(handle,
                    EM_SETTABLEPARMS, reinterpret_cast<WPARAM>(&row),
                    reinterpret_cast<LPARAM>(cells.data())));
                if (FAILED(result)) return false;
            }
        }
    }
    return true;
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

struct NativeCellEdit final {
    const TableCellProjection* cell{};
    std::string text;
    std::size_t selection_begin{};
    std::size_t selection_end{};
};

struct LinearProjection final {
    std::string text;
    std::vector<std::uint64_t> source_offsets;
};

LinearProjection BuildLinearProjection(const RichProjection& projection) {
    LinearProjection result;
    const auto append_range = [&](std::uint64_t begin, std::uint64_t end) {
        if (result.source_offsets.empty())
            result.source_offsets.push_back(projection.source_offsets[begin]);
        for (auto offset = begin; offset < end; ++offset) {
            result.text.push_back(projection.text[static_cast<std::size_t>(offset)]);
            result.source_offsets.push_back(projection.source_offsets[static_cast<std::size_t>(offset + 1U)]);
        }
    };
    const auto append_synthetic = [&](char value, std::uint64_t source) {
        if (result.source_offsets.empty()) result.source_offsets.push_back(source);
        result.text.push_back(value);
        result.source_offsets.push_back(source);
    };
    std::uint64_t cursor{};
    for (const auto& table : projection.tables) {
        append_range(cursor, table.begin);
        for (const auto& row : table.rows) {
            for (const auto& cell : row.cells) {
                append_range(cell.begin, cell.end);
                append_synthetic('\t', cell.source_range.end);
            }
            append_synthetic('\n', table.source_range.end);
        }
        cursor = table.end;
    }
    append_range(cursor, projection.text.size());
    if (result.source_offsets.empty()) result.source_offsets = projection.source_offsets;
    return result;
}

std::optional<NativeCellEdit> ReadNativeCellEdit(HWND handle,
        const RichProjection& projection, CHARRANGE selection) {
    const auto document = TextDocumentFor(handle);
    if (!document || selection.cpMin < 0 || selection.cpMax < selection.cpMin) return std::nullopt;
    Microsoft::WRL::ComPtr<ITextRange2> range;
    long expanded{};
    if (FAILED(document->Range2(selection.cpMin, selection.cpMin, &range)) || !range ||
        FAILED(range->Expand(tomCell, &expanded))) return std::nullopt;
    long start{}, end{};
    if (FAILED(range->GetStart(&start)) || FAILED(range->GetEnd(&end)) || end <= start)
        return std::nullopt;
    const TableCellProjection* projected{};
    LONG nearest_distance = LONG_MAX;
    for (const auto& table : projection.tables) {
        for (const auto& row : table.rows) {
            for (const auto& cell : row.cells) {
                // TOM's cell range includes RichEdit's leading structural marker.
                // Formatting a native row can move the reported range start by
                // one marker without moving the visible cell content.
                if (cell.physical_begin == start ||
                    (cell.physical_begin > start && cell.physical_begin < end)) {
                    projected = &cell;
                    nearest_distance = 0;
                    break;
                }
                if (cell.physical_begin >= 0) {
                    const auto distance = std::labs(cell.physical_begin - start);
                    if (distance < nearest_distance) {
                        nearest_distance = distance;
                        projected = &cell;
                    }
                }
            }
            if (nearest_distance == 0) break;
        }
        if (nearest_distance == 0) break;
    }
    BSTR value{};
    if (FAILED(range->GetText(&value)) || !value) return std::nullopt;
    std::wstring wide(value, SysStringLen(value));
    SysFreeString(value);
    while (!wide.empty() && (wide.back() == L'\a' || wide.back() == L'\t' ||
            wide.back() == L'\r' || wide.back() == L'\n')) wide.pop_back();
    const auto visible_text = ToUtf8(wide);
    if (!projected || nearest_distance > 8 || projected->text != visible_text) {
        projected = nullptr;
        nearest_distance = LONG_MAX;
        for (const auto& table : projection.tables)
            for (const auto& row : table.rows) for (const auto& cell : row.cells) {
                if (cell.text != visible_text || cell.physical_begin < 0) continue;
                const auto distance = std::labs(cell.physical_begin - start);
                if (distance < nearest_distance) {
                    nearest_distance = distance;
                    projected = &cell;
                }
            }
    }
    if (!projected) return std::nullopt;
    const auto bounded_begin = (std::clamp)(selection.cpMin - start, 0L,
        static_cast<LONG>(wide.size()));
    const auto bounded_end = (std::clamp)(selection.cpMax - start, bounded_begin,
        static_cast<LONG>(wide.size()));
    NativeCellEdit result;
    result.cell = projected;
    result.text = visible_text;
    result.selection_begin = ToUtf8(std::wstring_view(wide).substr(
        0, static_cast<std::size_t>(bounded_begin))).size();
    result.selection_end = ToUtf8(std::wstring_view(wide).substr(
        0, static_cast<std::size_t>(bounded_end))).size();
    return result;
}

bool NativeTableTextUnchanged(HWND handle, const RichProjection& projection) {
    const auto document = TextDocumentFor(handle);
    if (!document || projection.tables.empty()) return false;

    // RichEdit's native column tracker may move the selection outside the table
    // before it sends EN_CHANGE (this is reproducible after maximizing).  The
    // selection therefore cannot be used as proof that this is a table-format
    // notification.  Compare every projected cell with its live native cell;
    // only a document-wide text match is safe to classify as format-only.
    for (const auto& table : projection.tables)
        for (const auto& row : table.rows) for (const auto& cell : row.cells) {
        if (cell.physical_begin < 0) return false;
        Microsoft::WRL::ComPtr<ITextRange2> range;
        long cell_expanded{};
        if (FAILED(document->Range2(cell.physical_begin, cell.physical_begin, &range)) ||
            !range || FAILED(range->Expand(tomCell, &cell_expanded))) return false;
        BSTR value{};
        if (FAILED(range->GetText(&value)) || !value) return false;
        std::wstring wide(value, SysStringLen(value));
        SysFreeString(value);
        while (!wide.empty() && (wide.back() == L'\a' || wide.back() == L'\t' ||
                wide.back() == L'\r' || wide.back() == L'\n')) wide.pop_back();
        if (ToUtf8(wide) != cell.text) return false;
    }
    return true;
}

ErrorCode MapControlSelection(HWND handle, const RichProjection& projection,
                              ParagraphEditor& editor) {
    CHARRANGE selected{};
    SendMessageW(handle, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selected));
    if (selected.cpMin < 0 || selected.cpMax < selected.cpMin ||
        selected.cpMax > GetWindowTextLengthW(handle))
        return ErrorCode::editor_selection_mapping_failed;
    const auto positions = BuildPhysicalPositions(projection);
    const auto projected_offset = [&positions](LONG cp) {
        const auto found = std::lower_bound(positions.begin(), positions.end(), cp);
        return static_cast<std::size_t>(found == positions.end()
            ? positions.size() - 1U : std::distance(positions.begin(), found));
    };
    const auto begin = projected_offset(selected.cpMin);
    const auto end = projected_offset(selected.cpMax);
    if (begin >= projection.source_offsets.size() || end >= projection.source_offsets.size())
        return ErrorCode::editor_selection_mapping_failed;
    return editor.set_selection(
        {projection.source_offsets[begin], projection.source_offsets[end]});
}

void SelectSourceRange(HWND handle, const RichProjection& projection, TextSelection source) {
    const auto nearest = [&projection](std::uint64_t target) {
        std::size_t best{};
        auto distance = (std::numeric_limits<std::uint64_t>::max)();
        for (std::size_t index = 0; index < projection.source_offsets.size(); ++index) {
            const auto mapped = projection.source_offsets[index];
            const auto candidate = mapped >= target ? mapped - target : target - mapped;
            if (candidate < distance || (candidate == distance &&
                    projection.source_offsets[best] < target && mapped >= target)) {
                best = index;
                distance = candidate;
            }
        }
        return best;
    };
    const auto begin = nearest(source.anchor);
    const auto end = nearest(source.caret);
    const auto positions = BuildPhysicalPositions(projection);
    CHARRANGE selected{positions[begin], positions[end]};
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
            a.table_column != b.table_column || a.table_columns != b.table_columns ||
            a.node_id != b.node_id || a.table_id != b.table_id) return false;
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
                          std::uint8_t level, UINT dpi, const RenderStyleProfile& profile) {
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
    const auto index = (std::clamp)(level, std::uint8_t{1}, std::uint8_t{6}) - 1;
    return point.y + MulDiv(profile.heading_sizes[index], static_cast<int>(dpi), 2880);
}

void ApplySpan(HWND handle, const ProjectionSpan& span,
        std::span<const LONG> utf16_positions, COLORREF text_color,
        COLORREF background_color, bool insert_image,
        const RenderStyleProfile& profile, UINT layout_dpi) {
    static_cast<void>(layout_dpi);
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
        format.dwMask = CFM_FACE | CFM_BACKCOLOR | CFM_SIZE;
        format.crBackColor = dark ? RGB(77, 48, 58) : RGB(255, 232, 238);
        format.yHeight = (std::max)(120L, profile.body_size - 20L);
        wcscpy_s(format.szFaceName, L"Consolas");
    } else if (span.kind == document::NodeKind::link) {
        format.dwMask = CFM_UNDERLINE | CFM_COLOR;
        format.dwEffects = CFE_UNDERLINE;
        format.crTextColor = dark ? RGB(105, 175, 245) : RGB(0, 102, 204);
    } else if (span.kind == document::NodeKind::heading) {
        format.dwMask = CFM_BOLD | CFM_SIZE | CFM_FACE;
        format.dwEffects = profile.heading_bold ? CFE_BOLD : 0;
        const auto index = (std::clamp)(span.heading_level,
            std::uint8_t{1}, std::uint8_t{6}) - 1;
        format.yHeight = profile.heading_sizes[index];
        wcscpy_s(format.szFaceName, profile.heading_font);
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
    if (span.kind == document::NodeKind::list_item && span.marker_end > span.begin) {
        const auto marker_end = utf16_positions[(std::min)(
            static_cast<std::size_t>(span.marker_end), utf16_positions.size() - 1U)];
        SendMessageW(handle, EM_SETSEL, static_cast<WPARAM>(begin),
            static_cast<LPARAM>(marker_end));
        CHARFORMAT2W marker{};
        marker.cbSize = sizeof(marker);
        marker.dwMask = CFM_PROTECTED;
        marker.dwEffects = CFE_PROTECTED;
        SendMessageW(handle, EM_SETCHARFORMAT, SCF_SELECTION,
            reinterpret_cast<LPARAM>(&marker));
    }
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
    if (span.kind == document::NodeKind::heading ||
        span.kind == document::NodeKind::quote ||
        span.kind == document::NodeKind::code_block ||
        span.kind == document::NodeKind::thematic_break ||
        span.kind == document::NodeKind::list_item) {
        PARAFORMAT2 paragraph{};
        paragraph.cbSize = sizeof(paragraph);
        paragraph.dwMask = span.kind == document::NodeKind::thematic_break
            ? PFM_ALIGNMENT :
            span.kind == document::NodeKind::code_block
            ? PFM_STARTINDENT | PFM_RIGHTINDENT | PFM_SPACEBEFORE | PFM_SPACEAFTER :
            span.kind == document::NodeKind::list_item
            ? PFM_STARTINDENT | PFM_OFFSET :
            span.kind == document::NodeKind::heading
            ? PFM_SPACEBEFORE | PFM_SPACEAFTER : PFM_STARTINDENT;
        if (span.kind == document::NodeKind::heading) {
            const auto index = (std::clamp)(span.heading_level,
                std::uint8_t{1}, std::uint8_t{6}) - 1;
            paragraph.dySpaceBefore = profile.heading_space_before[index];
            paragraph.dySpaceAfter = profile.heading_space_after[index];
        } else if (span.kind == document::NodeKind::quote)
            paragraph.dxStartIndent = kBlockGutterTwips + 360;
        else if (span.kind == document::NodeKind::code_block) {
            paragraph.dxStartIndent = kBlockGutterTwips + 300;
            paragraph.dxRightIndent = 300;
            paragraph.dySpaceBefore = 300;
            paragraph.dySpaceAfter = 180;
        }
        else if (span.kind == document::NodeKind::list_item) {
            constexpr LONG hanging = 540;
            constexpr LONG list_indent = 240;
            paragraph.dxStartIndent = kBlockGutterTwips +
                list_indent + static_cast<LONG>(span.list_depth) * 360;
            // RichEdit interprets dxOffset as the indentation of continuation
            // lines relative to the first line.  A positive offset therefore
            // keeps the marker to the left and aligns wrapped text with the
            // list item's body; a negative value produces a first-line indent.
            paragraph.dxOffset = hanging;
        }
        else paragraph.wAlignment = PFA_CENTER;
        SendMessageW(handle, EM_SETPARAFORMAT, 0,
                     reinterpret_cast<LPARAM>(&paragraph));
    }
}

void ApplyBaseParagraph(HWND handle, const RenderStyleProfile& profile) {
    PARAFORMAT2 paragraph{};
    paragraph.cbSize = sizeof(paragraph);
    paragraph.dwMask = PFM_STARTINDENT | PFM_LINESPACING |
        PFM_SPACEBEFORE | PFM_SPACEAFTER;
    paragraph.dxStartIndent = kBlockGutterTwips;
    paragraph.bLineSpacingRule = 4;
    paragraph.dyLineSpacing = profile.minimum_line_spacing;
    paragraph.dySpaceBefore = profile.paragraph_space_before;
    paragraph.dySpaceAfter = profile.paragraph_space_after;
    SendMessageW(handle, EM_SETSEL, 0, -1);
    SendMessageW(handle, EM_SETPARAFORMAT, 0,
        reinterpret_cast<LPARAM>(&paragraph));
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
    block_type_window_ = CreateWindowExW(WS_EX_TRANSPARENT, L"BUTTON", L"",
        WS_CHILD | BS_OWNERDRAW,
        0, 0, 0, 0, handle_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(kBlockTypeControlId)),
        GetModuleHandleW(nullptr), nullptr);
    block_handle_window_ = CreateWindowExW(WS_EX_TRANSPARENT, L"BUTTON", L"块操作",
        WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, handle_, reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(kBlockHandleControlId)),
        GetModuleHandleW(nullptr), nullptr);
    if (!block_type_window_ || !block_handle_window_ ||
        !SetWindowSubclass(block_type_window_, BlockButtonSubclass, 1,
            reinterpret_cast<DWORD_PTR>(this)) ||
        !SetWindowSubclass(block_handle_window_, BlockButtonSubclass, 1,
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
    TraceTable("project begin revision=" + std::to_string(session_.snapshot().source_revision));
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
    const auto event_mask = static_cast<DWORD_PTR>(
        SendMessageW(handle_, EM_GETEVENTMASK, 0, 0));
    SendMessageW(handle_, EM_SETEVENTMASK, 0,
        static_cast<LPARAM>(event_mask & ~static_cast<DWORD_PTR>(
            ENM_CHANGE | ENM_SELCHANGE)));
    SendMessageW(handle_, WM_SETREDRAW, FALSE, 0);
    RichEditFreeze freeze(handle_);
    projection_ = BuildInlineProjection(*snapshot.semantic, snapshot.source, document_path_);
    static_cast<void>(block_interactions_.refresh(
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&session_)),
        snapshot, projection_));
    block_layout_valid_ = false;
    const auto rich_text = ToWide(fileio::NormalizeLineEndings(
        projection_.text, fileio::LineEnding::crlf));
    const auto reset_native_structure = reset_native_table_structure_;
    reset_native_table_structure_ = false;
    const auto success = (!reset_native_structure || ResetRichEditDocument(handle_)) &&
        (SetWindowTextW(handle_, rich_text.c_str()) != 0 || rich_text.empty());
    if (!success) {
        SendMessageW(handle_, EM_SETEVENTMASK, 0, static_cast<LPARAM>(event_mask));
        SendMessageW(handle_, WM_SETREDRAW, TRUE, 0);
        projecting_ = false;
        return ErrorCode::editor_render_projection_failed;
    }
    if (!SetNativeTableParameters(handle_, projection_, background_color_, dpi_, true)) {
        TraceTable("project insert_tables failed tables=" +
            std::to_string(projection_.tables.size()));
        SendMessageW(handle_, EM_SETEVENTMASK, 0, static_cast<LPARAM>(event_mask));
        SendMessageW(handle_, WM_SETREDRAW, TRUE, 0);
        projecting_ = false;
        return ErrorCode::editor_render_projection_failed;
    }
    const auto utf16_positions = BuildPhysicalPositions(projection_);
    for (const auto& span : projection_.spans) {
        if (span.kind == document::NodeKind::image)
            ApplySpan(handle_, span, utf16_positions,
                text_color_, background_color_, true, ProfileFor(render_style_), dpi_);
    }
    apply_appearance(text_color_, background_color_, dpi_);
    apply_heading_folds();
    const auto length = static_cast<LONG>(GetWindowTextLengthW(handle_));
    selection.cpMin = (std::min)(selection.cpMin, length);
    selection.cpMax = (std::min)(selection.cpMax, length);
    SendMessageW(handle_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    SendMessageW(handle_, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
    SendMessageW(handle_, EM_SETEVENTMASK, 0, static_cast<LPARAM>(event_mask));
    projecting_ = false;
    SendMessageW(handle_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(handle_, nullptr, TRUE);
    if (projection_notifications_pending_)
        PostMessageW(handle_, kProjectionNotificationsSettledMessage, 0, 0);
    TraceTable("project end tables=" + std::to_string(projection_.tables.size()) +
        " length=" + std::to_string(GetWindowTextLengthW(handle_)));
    return ErrorCode::ok;
}

ErrorCode RichEditHost::project_editor_selection() {
    const auto selection = editor_.selection();
    const auto result = project();
    if (result != ErrorCode::ok) return result;
    return select_source_range(selection);
}

ErrorCode RichEditHost::run_deferred_reproject() {
    deferred_reproject_pending_ = false;
    return project();
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
    const auto& profile = ProfileFor(render_style_);
    format.yHeight = profile.body_size;
    wcscpy_s(format.szFaceName, profile.body_font);
    SendMessageW(handle_, EM_SETCHARFORMAT, SCF_DEFAULT,
        reinterpret_cast<LPARAM>(&format));
    CHARFORMAT2W base{};
    base.cbSize = sizeof(base);
    base.dwMask = CFM_COLOR | CFM_BACKCOLOR | CFM_FACE | CFM_SIZE;
    base.crTextColor = text_color_;
    base.crBackColor = background_color_;
    base.yHeight = profile.body_size;
    wcscpy_s(base.szFaceName, profile.body_font);
    SendMessageW(handle_, EM_SETSEL, 0, -1);
    SendMessageW(handle_, EM_SETCHARFORMAT, SCF_SELECTION,
        reinterpret_cast<LPARAM>(&base));
    ApplyBaseParagraph(handle_, profile);
    const auto utf16_positions = BuildPhysicalPositions(projection_);
    for (const auto& span : projection_.spans)
        ApplySpan(handle_, span, utf16_positions,
            text_color_, background_color_, false, profile, dpi_);
    static_cast<void>(SetNativeTableParameters(
        handle_, projection_, background_color_, dpi_, false));
    apply_heading_folds();
    invalidate_block_layout();
    SendMessageW(handle_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    projecting_ = was_projecting;
    InvalidateRect(handle_, nullptr, TRUE);
}

void RichEditHost::set_render_style(RenderStyle style) {
    if (render_style_ == style) return;
    render_style_ = style;
    apply_appearance(text_color_, background_color_, dpi_);
}

RenderStyle RichEditHost::render_style() const noexcept { return render_style_; }

void RichEditHost::refresh_layout_after_resize() {
    if (!handle_ || projecting_ || projection_.spans.empty()) return;
    RECT client{};
    GetClientRect(handle_, &client);
    TraceTable("resize refresh begin width=" + std::to_string(client.right) +
        " height=" + std::to_string(client.bottom) + " tables=" +
        std::to_string(projection_.tables.size()));
    // The real maximized-window trace proved EM_SETTABLEPARMS fails here because
    // WM_SIZE is still inside RichEdit's layout stack and cached cpStartRow values
    // no longer identify valid rows.  Coalesce a clean rebuild after WM_SIZE
    // returns; the empty-RTF reset discards the stale native table tree first.
    if (!projection_.tables.empty() && !deferred_reproject_pending_) {
        reset_native_table_structure_ = true;
        deferred_reproject_pending_ = true;
        PostMessageW(handle_, kReprojectNativeTableMessage, 0, 0);
        TraceTable("resize refresh queued full reproject");
    } else {
        TraceTable("resize refresh skipped pending=" +
            std::to_string(deferred_reproject_pending_));
    }
}

bool RichEditHost::begin_native_table_pointer_gesture(POINT point) {
    if (!handle_ || projection_.tables.empty()) return false;
    POINTL native_point{point.x, point.y};
    const auto cp = static_cast<LONG>(SendMessageW(handle_, EM_CHARFROMPOS, 0,
        reinterpret_cast<LPARAM>(&native_point)));
    const auto document = TextDocumentFor(handle_);
    Microsoft::WRL::ComPtr<ITextRange2> cell;
    long expanded{};
    if (cp < 0 || !document || FAILED(document->Range2(cp, cp, &cell)) || !cell ||
        FAILED(cell->Expand(tomCell, &expanded)) || expanded <= 0) return false;

    LONG left{}, top{}, right{}, bottom{}, hit{};
    if (FAILED(cell->GetRect(tomClientCoord | tomAllowOffClient | tomCell,
            &left, &top, &right, &bottom, &hit)) ||
        point.x < left || point.x > right || point.y < top || point.y >= bottom)
        return false;

    if (!native_table_pointer_read_only_) {
        native_table_pointer_was_read_only_ =
            (GetWindowLongPtrW(handle_, GWL_STYLE) & ES_READONLY) != 0;
        SendMessageW(handle_, EM_SETREADONLY, TRUE, 0);
        native_table_pointer_read_only_ = true;
    }
    return true;
}

void RichEditHost::end_native_table_pointer_gesture() {
    if (!handle_ || !native_table_pointer_read_only_) return;
    if (!native_table_pointer_was_read_only_)
        SendMessageW(handle_, EM_SETREADONLY, FALSE, 0);
    native_table_pointer_read_only_ = false;
    native_table_pointer_was_read_only_ = false;
}

bool RichEditHost::is_native_table_column_boundary(POINT point) const {
    if (!handle_ || projection_.tables.empty()) return false;
    // The resize cursor exposed by msftedit.dll is wider than the one-pixel
    // border that BuildTableLayouts reports, particularly with fractional DPI.
    // Cover the complete native hot zone so the click cannot start RichEdit's
    // private RTF-only column tracking before our source-owned interaction does.
    const auto tolerance = (std::max)(4L, static_cast<LONG>(MulDiv(
        8, static_cast<int>(dpi_ ? dpi_ : 96), 96)));

    // Primary hit-test: resolve the mouse point against RichEdit's *current*
    // native document instead of projection cp values cached before WM_SIZE.
    // Maximizing can relayout row/cell markers without changing Markdown, which
    // made the old TableLayout-only test miss the actual native resize hot zone.
    POINTL native_point{point.x, point.y};
    const auto cp = static_cast<LONG>(SendMessageW(handle_, EM_CHARFROMPOS, 0,
        reinterpret_cast<LPARAM>(&native_point)));
    const auto document = TextDocumentFor(handle_);
    Microsoft::WRL::ComPtr<ITextRange2> native_cell;
    long expanded{};
    if (cp >= 0 && document &&
        SUCCEEDED(document->Range2(cp, cp, &native_cell)) && native_cell &&
        SUCCEEDED(native_cell->Expand(tomCell, &expanded)) && expanded > 0) {
        LONG left{}, top{}, right{}, bottom{}, hit{};
        if (SUCCEEDED(native_cell->GetRect(
                tomClientCoord | tomAllowOffClient | tomCell,
                &left, &top, &right, &bottom, &hit)) &&
            point.y >= top && point.y < bottom &&
            (std::labs(point.x - left) <= tolerance ||
             std::labs(point.x - right) <= tolerance))
            return true;
    }

    // Secondary hit-test supplies whole-table outer edges and remains useful
    // when EM_CHARFROMPOS chooses the paragraph beside an exact border pixel.
    const auto revision = session_.snapshot().source_revision;
    const auto layouts = BuildTableLayouts(handle_, projection_, revision, dpi_);
    for (const auto& layout : layouts) {
        if (point.y < layout.table_rect.top || point.y >= layout.table_rect.bottom ||
            layout.column_boundaries.size() < 2U) continue;
        // Trust RichEdit's own resize hit-test as a second line of defence.  Its
        // maximized formatting rectangle can expose a hot zone that is wider or
        // offset from the TOM cell rectangle by more than our DPI tolerance.
        // Restrict this cursor check to a known native table rectangle so normal
        // horizontal-resize cursors elsewhere are unaffected.
        if (GetCursor() == LoadCursorW(nullptr, IDC_SIZEWE) &&
            point.x >= layout.table_rect.left && point.x <= layout.table_rect.right)
            return true;
        // RichEdit exposes resize tracking not only on internal separators but also
        // on the table's left/right outer edges. The right edge is normally hidden
        // against a narrow formatting rectangle and becomes draggable after maximize.
        for (const auto boundary : layout.column_boundaries)
            if (std::labs(point.x - boundary) <= tolerance)
                return true;
    }
    return false;
}

void RichEditHost::set_heading_folds(HeadingFoldController* folds) {
    folds_ = folds;
    apply_heading_folds();
}

void RichEditHost::apply_heading_folds() {
    if (!handle_) return;
    invalidate_block_layout();
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
            const auto utf16 = BuildPhysicalPositions(projection_);
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
    const auto utf16 = BuildPhysicalPositions(projection_);
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
            item.level, dpi_, ProfileFor(render_style_));
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
    const auto utf16 = BuildPhysicalPositions(projection_);
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
            item.level, dpi_, ProfileFor(render_style_));
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

bool RichEditHost::block_hidden_by_fold(const VisibleBlockItem& item) const noexcept {
    return folds_ && std::any_of(folds_->items().begin(), folds_->items().end(),
        [&item](const auto& fold) {
            return fold.collapsed && item.node_id != fold.node_id &&
                item.source_range.begin >= fold.body_range.begin &&
                item.source_range.begin < fold.body_range.end;
        });
}

bool RichEditHost::refresh_block_layout() {
    if (!handle_ || block_interactions_.revision() != session_.snapshot().source_revision)
        return false;
    if (block_layout_valid_) return true;
    const auto utf16 = BuildPhysicalPositions(projection_);
    RECT client{};
    GetClientRect(handle_, &client);
    const auto line_height = (std::max)(18, MulDiv(22, static_cast<int>(dpi_), 96));
    const auto first_line = static_cast<LONG>(SendMessageW(
        handle_, EM_GETFIRSTVISIBLELINE, 0, 0));
    const auto visible_lines = (std::max)(4L,
        static_cast<LONG>((client.bottom - client.top) / line_height + 4));
    auto first_character = static_cast<LONG>(SendMessageW(
        handle_, EM_LINEINDEX, static_cast<WPARAM>((std::max)(0L, first_line - 2)), 0));
    auto last_character = static_cast<LONG>(SendMessageW(
        handle_, EM_LINEINDEX, static_cast<WPARAM>(first_line + visible_lines), 0));
    if (first_character < 0) first_character = 0;
    if (last_character < 0) last_character = GetWindowTextLengthW(handle_);
    const auto first_projection = static_cast<std::uint64_t>(
        std::lower_bound(utf16.begin(), utf16.end(), first_character) - utf16.begin());
    const auto last_projection = static_cast<std::uint64_t>(
        std::upper_bound(utf16.begin(), utf16.end(), last_character) - utf16.begin());
    struct Candidate final {
        document::NodeId node_id{};
        int top{};
        int bottom{};
    };
    std::vector<Candidate> candidates;
    candidates.reserve(block_interactions_.items().size());
    const auto first_item = std::lower_bound(block_interactions_.items().begin(),
        block_interactions_.items().end(), first_projection,
        [](const VisibleBlockItem& item, const std::uint64_t projection_position) {
            return item.projection_end < projection_position;
        });
    for (auto cursor = first_item; cursor != block_interactions_.items().end(); ++cursor) {
        const auto& item = *cursor;
        if (item.projection_begin > last_projection) break;
        if (block_hidden_by_fold(item) || item.projection_begin >= utf16.size() ||
            item.projection_end >= utf16.size()) continue;
        POINT begin{};
        POINT end{};
        SendMessageW(handle_, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&begin),
            utf16[static_cast<std::size_t>(item.projection_begin)]);
        SendMessageW(handle_, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&end),
            utf16[static_cast<std::size_t>(item.projection_end)]);
        auto bottom = (std::max)(begin.y + line_height, end.y + line_height);
        if (bottom <= client.top || begin.y >= client.bottom) continue;
        if (item.kind == document::NodeKind::heading) {
            const auto document = TextDocumentFor(handle_);
            const auto center = HeadingVerticalCenter(handle_, document.Get(),
                utf16[static_cast<std::size_t>(item.projection_begin)],
                item.heading_level, dpi_,
                ProfileFor(render_style_));
            // Preserve the glyph top for hover hit testing while making the
            // rectangle's midpoint exactly the TOM character-row center.
            candidates.push_back({item.node_id, begin.y,
                (std::max)(begin.y + 1, center * 2 - begin.y)});
        } else {
            candidates.push_back({item.node_id, begin.y, bottom});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        if (left.top != right.top) return left.top < right.top;
        return left.bottom < right.bottom;
    });
    std::vector<BlockLayoutItem> layout;
    layout.reserve(candidates.size());
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        auto top = (std::max)(static_cast<int>(client.top), candidates[index].top);
        auto bottom = (std::min)(static_cast<int>(client.bottom), candidates[index].bottom);
        if (index + 1 < candidates.size())
            bottom = (std::min)(bottom, candidates[index + 1].top);
        if (bottom <= top) continue;
        layout.push_back({candidates[index].node_id,
            {client.left, top, client.right, bottom}});
    }
    block_layout_valid_ = block_interactions_.set_visible_layout(
        block_interactions_.revision(), std::move(layout));
    return block_layout_valid_;
}

bool RichEditHost::update_block_hover(const POINT point) {
    if (!handle_ || !refresh_block_layout()) {
        clear_block_hover();
        return false;
    }
    if (!tracking_mouse_leave_) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, handle_, 0};
        tracking_mouse_leave_ = TrackMouseEvent(&tracking) != FALSE;
    }
    const auto next = block_interactions_.hit_test(point.x, point.y);
    const auto unchanged = next.has_value() == hovered_block_.has_value() &&
        (!next || next->node_id == hovered_block_->node_id);
    if (unchanged) {
        if (next && (!IsWindowVisible(block_handle_window_) ||
            (next->kind == document::NodeKind::heading &&
             !IsWindowVisible(block_type_window_))))
            update_block_accessible_windows();
        return next.has_value();
    }
    hovered_block_ = next;
    RECT dirty{};
    GetClientRect(handle_, &dirty);
    dirty.right = MulDiv(kBlockHandleRightDips + 2, static_cast<int>(dpi_), 96);
    InvalidateRect(handle_, &dirty, TRUE);
    update_block_accessible_windows();
    return hovered_block_.has_value();
}

void RichEditHost::clear_block_hover() {
    tracking_mouse_leave_ = false;
    if (!hovered_block_) return;
    hovered_block_.reset();
    if (block_type_window_) ShowWindow(block_type_window_, SW_HIDE);
    if (block_handle_window_) ShowWindow(block_handle_window_, SW_HIDE);
    if (handle_) {
        RECT dirty{};
        GetClientRect(handle_, &dirty);
        dirty.right = MulDiv(kBlockHandleRightDips + 2, static_cast<int>(dpi_), 96);
        InvalidateRect(handle_, &dirty, TRUE);
    }
}

void RichEditHost::invalidate_block_layout() {
    block_layout_valid_ = false;
    clear_block_hover();
}

RECT RichEditHost::block_type_hit_rect() const noexcept {
    RECT result{};
    if (!hovered_block_ || hovered_block_->kind != document::NodeKind::heading) return result;
    const auto layout = block_interactions_.layout_rect(hovered_block_->node_id);
    if (!layout) return result;
    result.left = MulDiv(kBlockTypeLeftDips, static_cast<int>(dpi_), 96);
    result.right = MulDiv(kBlockTypeRightDips, static_cast<int>(dpi_), 96);
    result.top = layout->top;
    result.bottom = layout->bottom;
    return result;
}

RECT RichEditHost::block_handle_hit_rect() const noexcept {
    RECT result{};
    if (!hovered_block_) return result;
    const auto layout = block_interactions_.layout_rect(hovered_block_->node_id);
    if (!layout) return result;
    result.left = MulDiv(kBlockHandleLeftDips, static_cast<int>(dpi_), 96);
    result.right = MulDiv(kBlockHandleRightDips, static_cast<int>(dpi_), 96);
    result.top = layout->top;
    result.bottom = layout->bottom;
    return result;
}

std::optional<BlockCommandContext> RichEditHost::hovered_block() const noexcept {
    return hovered_block_;
}

void RichEditHost::draw_block_interaction(HDC dc) const {
    if (!handle_ || !dc || !hovered_block_) return;
    const auto handle_rect = block_handle_hit_rect();
    if (IsRectEmpty(&handle_rect)) return;
    const auto saved_dc = SaveDC(dc);
    const auto dark = GetRValue(background_color_) < 128;
    const auto foreground = dark ? RGB(224, 224, 224) : RGB(70, 70, 70);
    const auto hover_background = dark ? RGB(62, 62, 66) : RGB(238, 238, 238);
    const auto brush = CreateSolidBrush(hover_background);
    const auto pen = CreatePen(PS_SOLID, 1, hover_background);
    const auto old_brush = SelectObject(dc, brush);
    const auto old_pen = SelectObject(dc, pen);
    const auto radius = (std::max)(3, MulDiv(4, static_cast<int>(dpi_), 96));
    RoundRect(dc, handle_rect.left, handle_rect.top + 1, handle_rect.right,
        handle_rect.bottom - 1, radius, radius);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);

    const auto dot_brush = CreateSolidBrush(foreground);
    const auto old_dot_brush = SelectObject(dc, dot_brush);
    const auto old_dot_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    const auto center_x = (handle_rect.left + handle_rect.right) / 2;
    const auto center_y = (handle_rect.top + handle_rect.bottom) / 2;
    const auto dot = (std::max)(1, MulDiv(2, static_cast<int>(dpi_), 96));
    const auto gap = (std::max)(3, MulDiv(4, static_cast<int>(dpi_), 96));
    for (int row = -1; row <= 1; ++row)
        for (int column = -1; column <= 0; ++column) {
            const auto x = center_x + column * gap + gap / 2;
            const auto y = center_y + row * gap;
            Ellipse(dc, x - dot / 2, y - dot / 2, x + (dot + 1) / 2,
                y + (dot + 1) / 2);
        }
    SelectObject(dc, old_dot_pen);
    SelectObject(dc, old_dot_brush);
    DeleteObject(dot_brush);

    if (hovered_block_->kind == document::NodeKind::heading) {
        const auto type_rect = block_type_hit_rect();
        const auto item = std::find_if(block_interactions_.items().begin(),
            block_interactions_.items().end(), [this](const auto& candidate) {
                return candidate.node_id == hovered_block_->node_id;
            });
        if (!IsRectEmpty(&type_rect) && item != block_interactions_.items().end()) {
            const auto font = CreateFontW(-MulDiv(9, static_cast<int>(dpi_), 72), 0, 0, 0,
                FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            const auto old_font = SelectObject(dc, font);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, foreground);
            const wchar_t label[]{L'H', static_cast<wchar_t>(L'0' + item->heading_level), L'\0'};
            RECT text_rect = type_rect;
            DrawTextW(dc, label, 2, &text_rect,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(dc, old_font);
            DeleteObject(font);
        }
    }
    if (saved_dc != 0) RestoreDC(dc, saved_dc);
}

void RichEditHost::update_block_accessible_windows() {
    if (!handle_ || !hovered_block_ || !block_handle_window_) return;
    const auto handle_rect = block_handle_hit_rect();
    if (IsRectEmpty(&handle_rect)) return;
    const auto snapshot = session_.snapshot();
    std::wstring summary;
    if (hovered_block_->source_range.end <= snapshot.source.size()) {
        auto source = std::string_view(snapshot.source).substr(
            static_cast<std::size_t>(hovered_block_->source_range.begin),
            static_cast<std::size_t>(hovered_block_->source_range.end -
                hovered_block_->source_range.begin));
        if (source.size() > 40) source = source.substr(0, 40);
        summary = ToWide(source);
        std::replace(summary.begin(), summary.end(), L'\r', L' ');
        std::replace(summary.begin(), summary.end(), L'\n', L' ');
    }
    const auto handle_name = std::wstring(L"块操作：") + summary;
    SetWindowTextW(block_handle_window_, handle_name.c_str());
    MoveWindow(block_handle_window_, handle_rect.left, handle_rect.top,
        handle_rect.right - handle_rect.left, handle_rect.bottom - handle_rect.top, TRUE);
    ShowWindow(block_handle_window_, SW_SHOWNA);
    if (hovered_block_->kind == document::NodeKind::heading && block_type_window_) {
        const auto type_rect = block_type_hit_rect();
        const auto item = std::find_if(block_interactions_.items().begin(),
            block_interactions_.items().end(), [this](const auto& candidate) {
                return candidate.node_id == hovered_block_->node_id;
            });
        const auto level = item == block_interactions_.items().end()
            ? 1 : static_cast<int>(item->heading_level);
        const auto type_name = std::wstring(L"标题 ") + std::to_wstring(level) +
            L"：" + summary;
        SetWindowTextW(block_type_window_, type_name.c_str());
        MoveWindow(block_type_window_, type_rect.left, type_rect.top,
            type_rect.right - type_rect.left, type_rect.bottom - type_rect.top, TRUE);
        ShowWindow(block_type_window_, SW_SHOWNA);
    } else if (block_type_window_) ShowWindow(block_type_window_, SW_HIDE);
    NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, block_handle_window_, OBJID_CLIENT, CHILDID_SELF);
}

void RichEditHost::draw_block_accessible_button(const DRAWITEMSTRUCT& item) const {
    if (!item.hDC) return;
    const auto dark = GetRValue(background_color_) < 128;
    const auto foreground = dark ? RGB(224, 224, 224) : RGB(70, 70, 70);
    const auto hover_background = dark ? RGB(62, 62, 66) : RGB(238, 238, 238);
    const auto background = CreateSolidBrush(hover_background);
    FillRect(item.hDC, &item.rcItem, background);
    DeleteObject(background);
    if (item.hwndItem == block_type_window_) {
        const auto found = hovered_block_ ? std::find_if(block_interactions_.items().begin(),
            block_interactions_.items().end(), [this](const auto& candidate) {
                return candidate.node_id == hovered_block_->node_id;
            }) : block_interactions_.items().end();
        if (found != block_interactions_.items().end()) {
            const wchar_t label[]{L'H', static_cast<wchar_t>(L'0' + found->heading_level), L'\0'};
            RECT rect = item.rcItem;
            SetBkMode(item.hDC, TRANSPARENT);
            SetTextColor(item.hDC, foreground);
            DrawTextW(item.hDC, label, 2, &rect,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        return;
    }
    const auto brush = CreateSolidBrush(foreground);
    const auto old_brush = SelectObject(item.hDC, brush);
    const auto old_pen = SelectObject(item.hDC, GetStockObject(NULL_PEN));
    const auto center_x = (item.rcItem.left + item.rcItem.right) / 2;
    const auto center_y = (item.rcItem.top + item.rcItem.bottom) / 2;
    const auto gap = (std::max)(3, MulDiv(4, static_cast<int>(dpi_), 96));
    for (int row = -1; row <= 1; ++row)
        for (int column = -1; column <= 0; ++column) {
            const auto x = center_x + column * gap + gap / 2;
            const auto y = center_y + row * gap;
            Ellipse(item.hDC, x - 1, y - 1, x + 1, y + 1);
        }
    SelectObject(item.hDC, old_pen);
    SelectObject(item.hDC, old_brush);
    DeleteObject(brush);
}

BlockMenuCapabilities RichEditHost::query_block_menu(
    const BlockCommandContext& context) const noexcept {
    const auto snapshot = session_.snapshot();
    if (!block_interactions_.validate(context,
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&session_)), snapshot))
        return {};
    const auto simple = context.kind == document::NodeKind::paragraph ||
        context.kind == document::NodeKind::heading;
    const auto list = context.kind == document::NodeKind::list_item;
    const auto safe_range = simple || list;
    bool can_indent{};
    bool can_outdent{};
    if (list && context.source_range.begin < snapshot.source.size()) {
        auto current = static_cast<std::size_t>(context.source_range.begin);
        std::size_t indent{};
        while (current + indent < snapshot.source.size() &&
            snapshot.source[current + indent] == ' ') ++indent;
        can_outdent = indent > 0;
        if (current > 0) {
            auto previous_end = current - 1;
            if (snapshot.source[previous_end] == '\n' && previous_end > 0) --previous_end;
            if (snapshot.source[previous_end] == '\r' && previous_end > 0) --previous_end;
            const auto previous_break = snapshot.source.rfind('\n', previous_end);
            const auto previous_begin = previous_break == std::string::npos
                ? 0 : previous_break + 1;
            auto marker = previous_begin;
            while (marker < snapshot.source.size() && snapshot.source[marker] == ' ') ++marker;
            const auto unordered = marker + 1 < snapshot.source.size() &&
                (snapshot.source[marker] == '-' || snapshot.source[marker] == '*' ||
                 snapshot.source[marker] == '+') && snapshot.source[marker + 1] == ' ';
            auto ordered = marker;
            while (ordered < snapshot.source.size() &&
                snapshot.source[ordered] >= '0' && snapshot.source[ordered] <= '9') ++ordered;
            const auto ordered_marker = ordered > marker && ordered + 1 < snapshot.source.size() &&
                snapshot.source[ordered] == '.' && snapshot.source[ordered + 1] == ' ';
            can_indent = unordered || ordered_marker;
        }
    }
    return {simple, safe_range, safe_range, safe_range,
        can_indent, can_outdent, simple};
}

ErrorCode RichEditHost::execute_block_menu(
    const BlockMenuCommand command, const BlockCommandContext& context) {
    const auto snapshot = session_.snapshot();
    if (!block_interactions_.validate(context,
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&session_)), snapshot))
        return ErrorCode::editor_transaction_conflict;
    const auto capabilities = query_block_menu(context);
    if (context.source_range.begin > context.source_range.end ||
        context.source_range.end > snapshot.source.size())
        return ErrorCode::editor_selection_mapping_failed;
    const auto source = snapshot.source.substr(
        static_cast<std::size_t>(context.source_range.begin),
        static_cast<std::size_t>(context.source_range.end - context.source_range.begin));
    const auto write_clipboard = [this](const std::string_view utf8) {
        const auto wide = ToWide(utf8);
        const auto bytes = (wide.size() + 1) * sizeof(wchar_t);
        const auto memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!memory) return false;
        auto* destination = static_cast<wchar_t*>(GlobalLock(memory));
        if (!destination) { GlobalFree(memory); return false; }
        std::copy(wide.begin(), wide.end(), destination);
        destination[wide.size()] = L'\0';
        GlobalUnlock(memory);
        if (!OpenClipboard(handle_)) { GlobalFree(memory); return false; }
        const auto close = [] { CloseClipboard(); };
        if (!EmptyClipboard()) { GlobalFree(memory); close(); return false; }
        if (!SetClipboardData(CF_UNICODETEXT, memory)) {
            GlobalFree(memory); close(); return false;
        }
        close();
        return true;
    };

    std::string replacement;
    switch (command) {
        case BlockMenuCommand::convert_paragraph:
        case BlockMenuCommand::convert_h1:
        case BlockMenuCommand::convert_h2:
        case BlockMenuCommand::convert_h3:
        case BlockMenuCommand::convert_h4:
        case BlockMenuCommand::convert_h5:
        case BlockMenuCommand::convert_h6: {
            if (!capabilities.convert) return ErrorCode::editor_unmapped_rich_edit_change;
            const auto level = command == BlockMenuCommand::convert_paragraph ? 0U :
                static_cast<unsigned>(command) -
                    static_cast<unsigned>(BlockMenuCommand::convert_h1) + 1U;
            replacement = ConvertBlockSource(source, static_cast<std::uint8_t>(level));
            break;
        }
        case BlockMenuCommand::remove:
            if (!capabilities.remove) return ErrorCode::editor_unmapped_rich_edit_change;
            break;
        case BlockMenuCommand::copy:
            return capabilities.copy && write_clipboard(source)
                ? ErrorCode::ok : ErrorCode::editor_unmapped_rich_edit_change;
        case BlockMenuCommand::cut:
            if (!capabilities.cut || !write_clipboard(source))
                return ErrorCode::editor_unmapped_rich_edit_change;
            break;
        case BlockMenuCommand::indent:
            if (!capabilities.indent) return ErrorCode::editor_unmapped_rich_edit_change;
            replacement = ShiftBlockIndent(source, true);
            break;
        case BlockMenuCommand::outdent:
            if (!capabilities.outdent) return ErrorCode::editor_unmapped_rich_edit_change;
            replacement = ShiftBlockIndent(source, false);
            break;
        case BlockMenuCommand::add_below: {
            if (!capabilities.add_below) return ErrorCode::editor_unmapped_rich_edit_change;
            const auto ending = fileio::DetectLineEnding(snapshot.source) ==
                fileio::LineEnding::crlf ? std::string("\r\n") : std::string("\n");
            const auto has_following_ending = snapshot.source.compare(
                static_cast<std::size_t>(context.source_range.end), ending.size(), ending) == 0;
            const auto insertion = has_following_ending ? ending : ending + ending;
            const auto caret = context.source_range.end + ending.size();
            const auto result = editor_.replace_source_range(context.source_range.end,
                context.source_range.end, insertion, {caret, caret});
            if (result != ErrorCode::ok) return result;
            const auto projected = project();
            if (projected != ErrorCode::ok) return projected;
            return select_source_range({caret, caret});
        }
    }
    const auto next = context.source_range.begin + replacement.size();
    const auto result = editor_.replace_source_range(context.source_range.begin,
        context.source_range.end, std::move(replacement), {next, next});
    return result == ErrorCode::ok ? project() : result;
}

void RichEditHost::set_block_menu_callback(std::function<void(
    BlockMenuCommand, const BlockCommandContext&)> callback) {
    block_menu_callback_ = std::move(callback);
}

HMENU RichEditHost::create_block_context_menu(
    const BlockCommandContext& context) const {
    const auto capabilities = query_block_menu(context);
    if (!capabilities.copy) return nullptr;
    const auto menu = CreatePopupMenu();
    const auto convert = CreatePopupMenu();
    if (!menu || !convert) {
        if (menu) DestroyMenu(menu);
        if (convert) DestroyMenu(convert);
        return nullptr;
    }
    const auto append = [](HMENU target, UINT id, const wchar_t* label, bool enabled) {
        return AppendMenuW(target, MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED), id, label) != FALSE;
    };
    append(convert, kBlockMenuFirst + 0, L"普通段落", capabilities.convert);
    for (UINT level = 1; level <= 6; ++level) {
        const auto label = L"标题 " + std::to_wstring(level);
        append(convert, kBlockMenuFirst + level, label.c_str(), capabilities.convert);
    }
    AppendMenuW(menu, MF_POPUP | (capabilities.convert ? MF_ENABLED : MF_GRAYED),
        reinterpret_cast<UINT_PTR>(convert), L"转换为");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    append(menu, kBlockMenuFirst + 7, L"删除", capabilities.remove);
    append(menu, kBlockMenuFirst + 8, L"复制", capabilities.copy);
    append(menu, kBlockMenuFirst + 9, L"剪切", capabilities.cut);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    append(menu, kBlockMenuFirst + 10, L"增加缩进", capabilities.indent);
    append(menu, kBlockMenuFirst + 11, L"减少缩进", capabilities.outdent);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    append(menu, kBlockMenuFirst + 12, L"在下方添加", capabilities.add_below);
    return menu;
}

bool RichEditHost::show_block_context_menu(
    const BlockCommandContext& context, const POINT screen_point) {
    if (!handle_) return false;
    const auto menu = create_block_context_menu(context);
    if (!menu) return false;
    const auto selected = static_cast<UINT>(TrackPopupMenuEx(menu,
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
        screen_point.x, screen_point.y, handle_, nullptr));
    DestroyMenu(menu);
    SetFocus(handle_);
    if (selected < kBlockMenuFirst || selected > kBlockMenuFirst + 12) return true;
    if (query_block_menu(context).copy) {
        const auto command = static_cast<BlockMenuCommand>(selected - kBlockMenuFirst);
        if (block_menu_callback_) block_menu_callback_(command, context);
        else static_cast<void>(execute_block_menu(command, context));
    }
    return true;
}

bool RichEditHost::handle_block_handle_click(const POINT point) {
    if (!hovered_block_) return false;
    const auto rect = block_handle_hit_rect();
    if (point.x < rect.left || point.x >= rect.right ||
        point.y < rect.top || point.y >= rect.bottom) return false;
    POINT screen{rect.left, rect.bottom};
    ClientToScreen(handle_, &screen);
    return show_block_context_menu(*hovered_block_, screen);
}

bool RichEditHost::handle_list_marker_click(const POINT point) {
    if (!handle_) return false;
    const auto physical = BuildPhysicalPositions(projection_);
    const auto hit = static_cast<LONG>(SendMessageW(handle_, EM_CHARFROMPOS, 0,
        reinterpret_cast<LPARAM>(&point)));
    const auto span = std::find_if(projection_.spans.begin(), projection_.spans.end(),
        [&](const ProjectionSpan& value) {
            return value.kind == document::NodeKind::list_item &&
                value.marker_end > value.begin && value.marker_end < physical.size() &&
                hit >= physical[static_cast<std::size_t>(value.begin)] &&
                hit < physical[static_cast<std::size_t>(value.marker_end)];
        });
    if (span == projection_.spans.end()) return false;
    const auto marker_end = physical[static_cast<std::size_t>(span->marker_end)];
    SendMessageW(handle_, EM_SETSEL, marker_end, marker_end);
    if (!span->ordered) return true;

    const auto menu = CreatePopupMenu();
    if (!menu) return true;
    constexpr std::array<std::uint32_t, 5> starts{1, 2, 3, 5, 10};
    for (std::size_t index = 0; index < starts.size(); ++index) {
        const auto label = std::wstring(L"起始值 ") + std::to_wstring(starts[index]);
        AppendMenuW(menu, MF_STRING, 6301 + static_cast<UINT>(index), label.c_str());
    }
    POINT screen = point;
    ClientToScreen(handle_, &screen);
    const auto command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN |
        TPM_TOPALIGN | TPM_NONOTIFY, screen.x, screen.y, 0, handle_, nullptr);
    DestroyMenu(menu);
    if (command >= 6301 && command < 6301 + starts.size()) {
        const auto source = projection_.source_offsets[static_cast<std::size_t>(span->begin)];
        if (editor_.set_selection({source, source}) == ErrorCode::ok &&
            list_editor_.set_ordered_start(starts[command - 6301]) == ErrorCode::ok)
            static_cast<void>(project());
    }
    return true;
}

bool RichEditHost::show_block_context_menu_at_caret() {
    const auto selection = source_selection();
    if (!selection.is_ok() || !refresh_block_layout()) return false;
    const auto context = block_interactions_.context_at_source(selection.value().caret);
    if (!context) return false;
    hovered_block_ = context;
    update_block_accessible_windows();
    auto rect = block_handle_hit_rect();
    POINT screen{rect.left, rect.bottom};
    if (IsRectEmpty(&rect)) {
        CHARRANGE native{};
        SendMessageW(handle_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&native));
        SendMessageW(handle_, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&screen), native.cpMin);
    }
    ClientToScreen(handle_, &screen);
    return show_block_context_menu(*context, screen);
}

HWND RichEditHost::block_type_window() const noexcept { return block_type_window_; }
HWND RichEditHost::block_handle_window() const noexcept { return block_handle_window_; }
std::optional<BlockCommandContext> RichEditHost::block_context_at_source(
    const std::uint64_t source_offset) const noexcept {
    return block_interactions_.context_at_source(source_offset);
}

void RichEditHost::draw_table_grid(HDC dc) const {
    static_cast<void>(dc);
    // Native RichEdit table cells own their borders and backgrounds.  GDI is
    // intentionally reserved for interaction overlays, not table structure.
}

void RichEditHost::draw_quote_guides(HDC dc) const {
    if (!handle_ || !dc || projection_.spans.empty()) return;
    const auto utf16 = BuildPhysicalPositions(projection_);
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    const auto color = GetRValue(background_color_) < 128
        ? RGB(118, 118, 122) : RGB(172, 172, 172);
    const auto pen = CreatePen(PS_SOLID, (std::max)(2, MulDiv(3,
        static_cast<int>(dpi_), 96)), color);
    const auto old_pen = SelectObject(dc, pen);
    RECT client{};
    GetClientRect(handle_, &client);
    for (const auto& quote : projection_.spans) {
        if (quote.kind != document::NodeKind::quote || quote.begin >= utf16.size() ||
            quote.end >= utf16.size()) continue;
        POINT begin{}, end{};
        SendMessageW(handle_, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&begin),
            utf16[static_cast<std::size_t>(quote.begin)]);
        const auto last = quote.end > quote.begin ? quote.end - 1 : quote.end;
        SendMessageW(handle_, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&end),
            utf16[static_cast<std::size_t>(last)]);
        const auto vertical_adjust = MulDiv(4, static_cast<int>(dpi_), 96);
        const auto top = static_cast<int>(begin.y) + vertical_adjust;
        const auto bottom = static_cast<int>(end.y) + metrics.tmHeight + vertical_adjust;
        if (bottom < client.top || top > client.bottom) continue;
        // Keep the guide in the reserved gutter.  Deriving it from the first
        // child text position makes a quote containing a native table cross the
        // table's left border because the two children use different layouts.
        const auto x = MulDiv(66, static_cast<int>(dpi_), 96);
        MoveToEx(dc, x, top, nullptr);
        LineTo(dc, x, bottom);
    }
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

void RichEditHost::draw_code_block_frames(HDC dc) const {
    if (!handle_ || !dc || projection_.spans.empty()) return;
    const auto utf16 = BuildPhysicalPositions(projection_);
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    RECT client{};
    GetClientRect(handle_, &client);
    const bool dark = GetRValue(background_color_) < 128;
    const auto color = dark ? RGB(88, 88, 94) : RGB(214, 214, 218);
    const auto label_color = dark ? RGB(176, 176, 182) : RGB(104, 104, 110);
    const auto pen = CreatePen(PS_SOLID, 1, color);
    const auto old_pen = SelectObject(dc, pen);
    const auto old_brush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    const auto old_mode = SetBkMode(dc, TRANSPARENT);
    const auto old_color = SetTextColor(dc, label_color);
    for (const auto& span : projection_.spans) {
        if (span.kind != document::NodeKind::code_block || span.begin >= utf16.size() ||
            span.end >= utf16.size()) continue;
        POINT begin{}, end{};
        SendMessageW(handle_, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&begin),
            utf16[static_cast<std::size_t>(span.begin)]);
        const auto last = span.end > span.begin ? span.end - 1 : span.end;
        SendMessageW(handle_, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&end),
            utf16[static_cast<std::size_t>(last)]);
        const auto pad = MulDiv(8, static_cast<int>(dpi_), 96);
        const auto radius = MulDiv(8, static_cast<int>(dpi_), 96);
        RECT frame{begin.x - pad, begin.y - pad, client.right - pad,
            end.y + metrics.tmHeight + pad};
        if (frame.bottom < client.top || frame.top > client.bottom) continue;
        RoundRect(dc, frame.left, frame.top, frame.right, frame.bottom, radius, radius);
        const auto label = ToWide(span.language.empty() ? "Plain Text" : span.language);
        TextOutW(dc, frame.left + pad, frame.top - metrics.tmHeight,
            label.c_str(), static_cast<int>(label.size()));
    }
    SetTextColor(dc, old_color);
    SetBkMode(dc, old_mode);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

void RichEditHost::draw_inline_code_frames(HDC dc) const {
    if (!handle_ || !dc || projection_.spans.empty()) return;
    const auto positions = BuildPhysicalPositions(projection_);
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    const bool dark = GetRValue(background_color_) < 128;
    const auto pen = CreatePen(PS_SOLID, 1,
        dark ? RGB(132, 78, 96) : RGB(232, 168, 184));
    const auto old_pen = SelectObject(dc, pen);
    const auto old_brush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    const auto pad_x = (std::max)(2, MulDiv(3, static_cast<int>(dpi_), 96));
    const auto pad_y = (std::max)(1, MulDiv(2, static_cast<int>(dpi_), 96));
    const auto radius = (std::max)(4, MulDiv(6, static_cast<int>(dpi_), 96));
    for (const auto& span : projection_.spans) {
        if (span.kind != document::NodeKind::inline_code || span.begin >= positions.size() ||
            span.end >= positions.size()) continue;
        POINT begin{}, end{};
        SendMessageW(handle_, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&begin),
            positions[static_cast<std::size_t>(span.begin)]);
        SendMessageW(handle_, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&end),
            positions[static_cast<std::size_t>(span.end)]);
        if (end.y != begin.y) continue;
        RoundRect(dc, begin.x - pad_x, begin.y - pad_y, end.x + pad_x,
            begin.y + metrics.tmHeight + pad_y, radius, radius);
    }
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

ErrorCode RichEditHost::show_status_message(std::wstring_view message) {
    if (!handle_) return ErrorCode::editor_render_projection_failed;
    projecting_ = true;
    clear_block_hover();
    block_interactions_.clear();
    block_layout_valid_ = false;
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

void RichEditHost::align_selection_to_top() {
    if (!handle_) return;
    CHARRANGE selected{};
    SendMessageW(handle_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selected));
    const auto target = static_cast<LONG>(SendMessageW(handle_, EM_LINEFROMCHAR,
        static_cast<WPARAM>(selected.cpMin), 0));
    const auto current = static_cast<LONG>(SendMessageW(handle_, EM_GETFIRSTVISIBLELINE, 0, 0));
    SendMessageW(handle_, EM_LINESCROLL, 0, target - current);
}

ErrorCode RichEditHost::synchronize_change() {
    if (projecting_) return ErrorCode::ok;
    TraceTable("sync begin pending_format=" +
        std::to_string(native_table_format_change_pending_) + " capture=" +
        std::to_string(GetCapture() == handle_) + " tables=" +
        std::to_string(projection_.tables.size()) + " revision=" +
        std::to_string(session_.snapshot().source_revision));
    CHARRANGE control_selection{};
    SendMessageW(handle_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&control_selection));
    if (native_table_format_change_pending_) {
        TraceTable("sync branch=pending_native_format reproject");
        native_table_format_change_pending_ = false;
        reset_native_table_structure_ = true;
        PostMessageW(handle_, kReprojectNativeTableMessage, 0, 0);
        return ErrorCode::ok;
    }
    // RichEdit owns mouse capture for its private table-column tracker.  Any
    // EN_CHANGE raised while that capture is active is an RTF/table-structure
    // mutation, not a Markdown text transaction.  This is the invariant-level
    // guard: it is independent of maximized geometry, caret relocation, cursor
    // shape, and the tracker rewriting cell/row markers.  Ordinary mouse text
    // selection raises no EN_CHANGE, while keyboard/IME/paste edits do not hold
    // this capture.
    if (!projection_.tables.empty() && GetCapture() == handle_) {
        TraceTable("sync branch=current_capture reproject");
        reset_native_table_structure_ = true;
        PostMessageW(handle_, kReprojectNativeTableMessage, 0, 0);
        return ErrorCode::ok;
    }
    // A native column drag reports EN_CHANGE even though Markdown was not edited.
    // The tracker can move the caret outside the table and can rewrite structural
    // marker text, so neither selection identity nor flat-text equality is stable.
    // The system horizontal-resize cursor is the reliable discriminator while the
    // pointer remains on the dragged table boundary.
    if (!projection_.tables.empty() &&
        GetCursor() == LoadCursorW(nullptr, IDC_SIZEWE)) {
        TraceTable("sync branch=size_cursor reproject");
        reset_native_table_structure_ = true;
        PostMessageW(handle_, kReprojectNativeTableMessage, 0, 0);
        return ErrorCode::ok;
    }
    const auto linear_projection = BuildLinearProjection(projection_);
    const auto line_ending = fileio::DetectLineEnding(session_.snapshot().source);
    const auto live_linear_text = ReadUtf8(handle_, line_ending == fileio::LineEnding::mixed
        ? fileio::LineEnding::crlf : line_ending);
    if (NativeTableTextUnchanged(handle_, projection_) &&
        linear_projection.text == live_linear_text) {
        TraceTable("sync branch=native_text_unchanged reproject");
        // Native table formatting notifications (most notably RichEdit's built-in
        // column resize tracker) contain no Markdown text edit.  Restore the
        // projection-owned geometry before any structural markers reach the flat
        // source mapper.
        reset_native_table_structure_ = true;
        PostMessageW(handle_, kReprojectNativeTableMessage, 0, 0);
        return ErrorCode::ok;
    }
    if (const auto native = ReadNativeCellEdit(handle_, projection_, control_selection)) {
        TraceTable("sync branch=native_cell before=" + std::to_string(native->cell->text.size()) +
            " after=" + std::to_string(native->text.size()));
        const auto& before_cell = native->cell->text;
        const auto& after_cell = native->text;
        if (before_cell == after_cell) {
            reset_native_table_structure_ = true;
            PostMessageW(handle_, kReprojectNativeTableMessage, 0, 0);
            return ErrorCode::ok;
        }
        if (before_cell != after_cell) {
            std::size_t prefix{};
            while (prefix < before_cell.size() && prefix < after_cell.size() &&
                before_cell[prefix] == after_cell[prefix]) ++prefix;
            while (prefix > 0 && prefix < before_cell.size() &&
                (static_cast<unsigned char>(before_cell[prefix]) & 0xc0U) == 0x80U) --prefix;
            std::size_t old_suffix = before_cell.size();
            std::size_t new_suffix = after_cell.size();
            while (old_suffix > prefix && new_suffix > prefix &&
                before_cell[old_suffix - 1] == after_cell[new_suffix - 1]) {
                --old_suffix;
                --new_suffix;
            }
            if (old_suffix >= native->cell->source_offsets.size()) {
                static_cast<void>(project());
                return ErrorCode::editor_selection_mapping_failed;
            }
            const auto source_begin = native->cell->source_offsets[prefix];
            const auto source_end = native->cell->source_offsets[old_suffix];
            auto visible_replacement = after_cell.substr(prefix, new_suffix - prefix);
            std::string markdown_replacement;
            for (const auto value : visible_replacement) {
                if (value == '\r' || value == '\n' || value == '\t') markdown_replacement.push_back(' ');
                else {
                    if (value == '|') markdown_replacement.push_back('\\');
                    markdown_replacement.push_back(value);
                }
            }
            const auto next = source_begin + markdown_replacement.size();
            const auto result = editor_.replace_source_range(source_begin, source_end,
                std::move(markdown_replacement), {next, next});
            if (result != ErrorCode::ok) {
                static_cast<void>(project());
                return result;
            }
            const auto projected = project();
            if (projected == ErrorCode::ok)
                SelectSourceRange(handle_, projection_, editor_.selection());
            return projected;
        }
    }
    const auto source = session_.snapshot().source;
    TraceTable("sync branch=linear source_bytes=" + std::to_string(source.size()));
    const auto& before = linear_projection.text;
    const auto after = live_linear_text;
    if (before == after) {
        TraceTable("sync linear unchanged bytes=" + std::to_string(before.size()));
        return ErrorCode::ok;
    }

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
    TraceTable("sync linear diff before=" + std::to_string(before.size()) +
        " after=" + std::to_string(after.size()) + " prefix=" + std::to_string(prefix) +
        " old_suffix=" + std::to_string(old_suffix) + " new_suffix=" +
        std::to_string(new_suffix));

    if (prefix >= linear_projection.source_offsets.size() ||
        old_suffix >= linear_projection.source_offsets.size()) {
        static_cast<void>(project());
        return ErrorCode::editor_selection_mapping_failed;
    }
    auto source_begin = linear_projection.source_offsets[prefix];
    auto source_end = linear_projection.source_offsets[old_suffix];
    auto replacement = after.substr(prefix, new_suffix - prefix);
    const auto expected_eol = line_ending == fileio::LineEnding::lf ? "\n" : "\r\n";
    if (replacement == expected_eol && control_selection.cpMin == control_selection.cpMax) {
        const auto target_ending = line_ending == fileio::LineEnding::mixed
            ? fileio::LineEnding::crlf : line_ending;
        const auto caret_after = PrefixUtf8Size(handle_, control_selection.cpMin, target_ending);
        if (caret_after >= replacement.size()) {
            const auto visual_insertion = caret_after - replacement.size();
            if (visual_insertion < linear_projection.source_offsets.size()) {
                source_begin = linear_projection.source_offsets[visual_insertion];
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
        if (BuildLinearProjection(next_projection).text == after &&
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

void RichEditHost::note_change_notification() {
    if (projection_.tables.empty()) return;
    const auto pointer_read_only = native_table_pointer_read_only_;
    const auto captured = GetCapture() == handle_;
    const auto size_cursor = GetCursor() == LoadCursorW(nullptr, IDC_SIZEWE);
    TraceTable("change notification pointer_read_only=" + std::to_string(pointer_read_only) +
        " capture=" + std::to_string(captured) + " size_cursor=" +
        std::to_string(size_cursor));
    if (pointer_read_only || captured || size_cursor)
        native_table_format_change_pending_ = true;
}

void RichEditHost::trace_table_event(std::string_view event) const {
    TraceTable(event);
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
    if (selected.cpMin == selected.cpMax) return ErrorCode::ok;
    auto result = MapControlSelection(handle_, projection_, editor_);
    if (result == ErrorCode::ok) result = formatter_.toggle(format);
    return result == ErrorCode::ok ? project_editor_selection() : result;
}

ErrorCode RichEditHost::set_link(std::string_view target, std::string_view title) {
    if (!handle_) return ErrorCode::editor_render_projection_failed;
    CHARRANGE selected{};
    SendMessageW(handle_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selected));
    auto result = MapControlSelection(handle_, projection_, editor_);
    if (result == ErrorCode::ok) result = formatter_.set_link(target, title);
    return result == ErrorCode::ok ? project_editor_selection() : result;
}

ErrorCode RichEditHost::set_heading(std::uint8_t level) {
    if (!handle_) return ErrorCode::editor_render_projection_failed;
    CHARRANGE selected{};
    SendMessageW(handle_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selected));
    auto result = MapControlSelection(handle_, projection_, editor_);
    if (result == ErrorCode::ok) result = block_formatter_.set_heading(level);
    return result == ErrorCode::ok ? project_editor_selection() : result;
}

ErrorCode RichEditHost::toggle_quote() {
    if (!handle_) return ErrorCode::editor_render_projection_failed;
    auto result = MapControlSelection(handle_, projection_, editor_);
    if (result == ErrorCode::ok) result = block_formatter_.toggle_quote();
    return result == ErrorCode::ok ? project_editor_selection() : result;
}

ErrorCode RichEditHost::toggle_code_block(std::string_view language) {
    if (!handle_) return ErrorCode::editor_render_projection_failed;
    // A real toolbar click can make RichEdit emit a focus/format EN_CHANGE
    // synchronously before the Markdown transaction reaches project().  Keep
    // the whole command inside the programmatic-projection notification window,
    // not just SetWindowText/formatting inside project().
    projection_notifications_pending_ = true;
    auto result = MapControlSelection(handle_, projection_, editor_);
    if (result == ErrorCode::ok) result = block_formatter_.toggle_code_block(language);
    if (result != ErrorCode::ok) {
        complete_projection_notification_window();
        return result;
    }
    result = project_editor_selection();
    if (result != ErrorCode::ok) complete_projection_notification_window();
    return result;
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
    return result == ErrorCode::ok ? project_editor_selection() : result;
}
ErrorCode RichEditHost::select_all() {
    SendMessageW(handle_, EM_SETSEL, 0, -1); return ErrorCode::ok;
}
ErrorCode RichEditHost::erase_selection() {
    if (!handle_) return ErrorCode::editor_render_projection_failed;
    SendMessageW(handle_, WM_CLEAR, 0, 0);
    return ErrorCode::ok;
}
void RichEditHost::set_document_context_menu(DocumentContextStateQuery query,
        DocumentContextCommandHandler handler) {
    document_context_query_ = std::move(query);
    document_context_handler_ = std::move(handler);
}
bool RichEditHost::show_document_context_menu(POINT screen_point) {
    if (!document_context_query_ || !document_context_handler_) return false;
    return ShowDocumentContextMenu(handle_, screen_point, dpi_, text_color_,
        background_color_, document_context_query_(), document_context_handler_);
}
void RichEditHost::remember_context_selection_at(const POINT client_point) {
    context_selection_pending_ = false;
    if (!handle_) return;
    CHARRANGE selection{};
    SendMessageW(handle_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    if (selection.cpMin == selection.cpMax) return;
    const auto hit = static_cast<LONG>(SendMessageW(handle_, EM_CHARFROMPOS, 0,
        reinterpret_cast<LPARAM>(&client_point)));
    if (hit < selection.cpMin || hit > selection.cpMax) return;
    context_selection_begin_ = selection.cpMin;
    context_selection_end_ = selection.cpMax;
    context_selection_pending_ = true;
}
void RichEditHost::restore_context_selection() {
    if (!handle_ || !context_selection_pending_) return;
    const CHARRANGE selection{context_selection_begin_, context_selection_end_};
    SendMessageW(handle_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    context_selection_pending_ = false;
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

bool RichEditHost::block_active(BlockFormat format) const noexcept {
    if (!handle_ || projection_.spans.empty()) return false;
    CHARRANGE selected{};
    SendMessageW(handle_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selected));
    if (selected.cpMin < 0) return false;
    const auto visible = ReadWide(handle_);
    if (static_cast<std::size_t>(selected.cpMin) > visible.size()) return false;
    const auto caret = PrefixUtf8Size(handle_, selected.cpMin,
        fileio::DetectLineEnding(projection_.text));
    const auto matches = [format](const ProjectionSpan& span) {
        switch (format) {
        case BlockFormat::quote: return span.kind == document::NodeKind::quote;
        case BlockFormat::code_block: return span.kind == document::NodeKind::code_block;
        case BlockFormat::task_list:
            return span.kind == document::NodeKind::list_item && span.task;
        case BlockFormat::ordered_list:
            return span.kind == document::NodeKind::list_item && span.ordered && !span.task;
        case BlockFormat::unordered_list:
            return span.kind == document::NodeKind::list_item && !span.ordered && !span.task;
        }
        return false;
    };
    const auto at = [&](std::uint64_t position) {
        return std::any_of(projection_.spans.begin(), projection_.spans.end(),
            [position, &matches](const ProjectionSpan& span) {
                return matches(span) && position >= span.begin && position < span.end;
            });
    };
    return at(caret) || (caret > 0 && at(caret - 1));
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
