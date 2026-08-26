#include "markdownmay/editor/table_layout.hpp"

#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <wrl/client.h>

#include <algorithm>
#include <climits>

namespace markdownmay::editor {
namespace {

std::vector<LONG> BuildUtf16Positions(std::string_view text) {
    std::vector<LONG> positions(text.size() + 1U);
    LONG utf16{};
    for (std::size_t index = 0; index < text.size();) {
        positions[index] = utf16;
        const auto first = static_cast<unsigned char>(text[index]);
        if (first == '\r' && index + 1U < text.size() && text[index + 1U] == '\n') {
            positions[index + 1U] = utf16 + 1;
            ++utf16;
            index += 2U;
            positions[index] = utf16;
            continue;
        }
        std::size_t bytes = 1U;
        LONG units = 1;
        if ((first & 0xf8U) == 0xf0U && index + 3U < text.size()) {
            bytes = 4U;
            units = 2;
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

Microsoft::WRL::ComPtr<ITextDocument2> TextDocumentFor(HWND handle) {
    Microsoft::WRL::ComPtr<IRichEditOle> rich_ole;
    Microsoft::WRL::ComPtr<ITextDocument2> document;
    if (SendMessageW(handle, EM_GETOLEINTERFACE, 0,
            reinterpret_cast<LPARAM>(rich_ole.GetAddressOf())))
        static_cast<void>(rich_ole.As(&document));
    return document;
}

bool CharacterLineBounds(ITextDocument2* document, LONG begin, LONG end,
                         LONG& top, LONG& bottom) {
    if (!document || begin < 0 || end <= begin) return false;
    Microsoft::WRL::ComPtr<ITextRange2> first;
    Microsoft::WRL::ComPtr<ITextRange2> last;
    long unused{};
    if (FAILED(document->Range2(begin, begin + 1, &first)) || !first ||
        FAILED(document->Range2(end - 1, end, &last)) || !last ||
        FAILED(first->GetPoint(tomStart | tomClientCoord | tomAllowOffClient |
            TA_TOP, &unused, &top)) ||
        FAILED(last->GetPoint(tomEnd | tomClientCoord | tomAllowOffClient |
            TA_BOTTOM, &unused, &bottom))) return false;
    return bottom > top;
}

bool CharacterStart(HWND handle, LONG position, POINT& point) {
    return SendMessageW(handle, EM_POSFROMCHAR,
        reinterpret_cast<WPARAM>(&point), position) != -1;
}

}  // namespace

LONG TableHorizontalPadding(UINT dpi) noexcept {
    return (std::max)(1L, static_cast<LONG>(MulDiv(6,
        static_cast<int>(dpi ? dpi : 96), 96)));
}

std::vector<TableLayout> BuildTableLayouts(HWND rich_edit,
        const RichProjection& projection, std::uint64_t revision, UINT dpi) {
    std::vector<TableLayout> layouts;
    if (!rich_edit || projection.spans.empty()) return layouts;
    const auto document = TextDocumentFor(rich_edit);
    if (!document) return layouts;
    const auto utf16 = BuildUtf16Positions(projection.text);
    const auto padding = TableHorizontalPadding(dpi);
    const auto vertical_space = (std::max)(1L, static_cast<LONG>(MulDiv(
        100, static_cast<int>(dpi ? dpi : 96), 1440)));
    RECT formatting{};
    SendMessageW(rich_edit, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&formatting));

    for (const auto& table : projection.spans) {
        if (table.kind != document::NodeKind::table) continue;
        std::uint32_t rows{}, columns{};
        for (const auto& cell : projection.spans) {
            if (cell.kind != document::NodeKind::table_cell ||
                cell.table_id != table.node_id) continue;
            rows = (std::max)(rows, cell.table_row + 1);
            columns = (std::max)(columns, cell.table_column + 1);
        }
        if (!rows || !columns) continue;

        TableLayout layout;
        layout.table_id = table.node_id;
        layout.revision = revision;
        layout.row_rects.assign(rows, RECT{0, INT_MAX, 0, INT_MIN});
        layout.column_boundaries.assign(columns + 1U, LONG_MIN);

        for (const auto& cell : projection.spans) {
            if (cell.kind != document::NodeKind::table_cell ||
                cell.table_id != table.node_id || cell.begin >= utf16.size() ||
                cell.end >= utf16.size()) continue;
            const auto begin = utf16[static_cast<std::size_t>(cell.begin)];
            const auto end = utf16[static_cast<std::size_t>(cell.end)];
            LONG top{}, bottom{};
            POINT start{};
            if (!CharacterLineBounds(document.Get(), begin, end, top, bottom) ||
                !CharacterStart(rich_edit, begin, start)) continue;
            auto& row = layout.row_rects[cell.table_row];
            row.top = (std::min)(row.top, top);
            row.bottom = (std::max)(row.bottom, bottom);
            auto& boundary = layout.column_boundaries[cell.table_column];
            const auto observed = static_cast<LONG>(start.x) - padding;
            boundary = boundary == LONG_MIN ? observed : (std::min)(boundary, observed);
        }

        if (layout.column_boundaries.front() == LONG_MIN) continue;
        layout.column_boundaries.back() = formatting.right;
        for (std::size_t column = 1; column < columns; ++column) {
            if (layout.column_boundaries[column] == LONG_MIN) continue;
            layout.column_boundaries[column] = (std::max)(
                layout.column_boundaries[column],
                layout.column_boundaries[column - 1] + padding * 2);
        }
        bool complete = std::all_of(layout.row_rects.begin(), layout.row_rects.end(),
            [](const RECT& row) { return row.top != INT_MAX && row.bottom != INT_MIN; });
        complete = complete && std::none_of(layout.column_boundaries.begin(),
            layout.column_boundaries.end(), [](LONG value) { return value == LONG_MIN; });
        if (!complete) continue;

        std::vector<LONG> row_boundaries(rows + 1U);
        row_boundaries.front() = layout.row_rects.front().top - vertical_space;
        for (std::size_t row = 1; row < rows; ++row)
            row_boundaries[row] = layout.row_rects[row - 1].bottom +
                (layout.row_rects[row].top - layout.row_rects[row - 1].bottom) / 2;
        row_boundaries.back() = layout.row_rects.back().bottom + vertical_space;
        for (std::size_t row{}; row < rows; ++row) {
            layout.row_rects[row].left = layout.column_boundaries.front();
            layout.row_rects[row].right = layout.column_boundaries.back();
            layout.row_rects[row].top = row_boundaries[row];
            layout.row_rects[row].bottom = row_boundaries[row + 1U];
        }
        layout.table_rect = {layout.column_boundaries.front(), row_boundaries.front(),
            layout.column_boundaries.back(), row_boundaries.back()};
        for (const auto& cell : projection.spans) {
            if (cell.kind != document::NodeKind::table_cell ||
                cell.table_id != table.node_id || cell.table_row >= rows ||
                cell.table_column >= columns) continue;
            const RECT rect{layout.column_boundaries[cell.table_column],
                row_boundaries[cell.table_row],
                layout.column_boundaries[cell.table_column + 1U],
                row_boundaries[cell.table_row + 1U]};
            RECT content = rect;
            content.left += padding;
            content.right -= padding;
            layout.cells.push_back({cell.node_id, cell.table_row,
                cell.table_column, rect, content});
        }
        layouts.push_back(std::move(layout));
    }
    return layouts;
}

}  // namespace markdownmay::editor
