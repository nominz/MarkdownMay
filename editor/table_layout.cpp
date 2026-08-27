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

bool NativeCellBounds(ITextDocument2* document, LONG position, RECT& bounds) {
    if (!document || position < 0) return false;
    Microsoft::WRL::ComPtr<ITextRange2> range;
    long delta{};
    if (FAILED(document->Range2(position, position, &range)) || !range ||
        FAILED(range->Expand(tomCell, &delta))) return false;
    LONG left{}, top{}, right{}, bottom{};
    LONG hit{};
    if (SUCCEEDED(range->GetRect(tomClientCoord | tomAllowOffClient | tomCell,
            &left, &top, &right, &bottom, &hit)) && right > left && bottom > top) {
        bounds = {left, top, right, bottom};
        return true;
    }
    if (FAILED(range->GetPoint(tomStart | tomClientCoord | tomAllowOffClient |
            TA_LEFT | TA_TOP, &left, &top)) ||
        FAILED(range->GetPoint(tomEnd | tomClientCoord | tomAllowOffClient |
            TA_RIGHT | TA_BOTTOM, &right, &bottom)) || right <= left || bottom <= top)
        return false;
    bounds = {left, top, right, bottom};
    return true;
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
        layout.row_rects.assign(rows, RECT{INT_MAX, INT_MAX, INT_MIN, INT_MIN});
        layout.column_boundaries.assign(columns + 1U, LONG_MIN);

        for (const auto& cell : projection.spans) {
            if (cell.kind != document::NodeKind::table_cell ||
                cell.table_id != table.node_id) continue;
            const auto projected_table = std::find_if(projection.tables.begin(),
                projection.tables.end(), [&cell](const auto& candidate) {
                    return candidate.table_id == cell.table_id;
                });
            if (projected_table == projection.tables.end()) continue;
            const TableCellProjection* projected_cell{};
            for (const auto& projected_row : projected_table->rows)
                for (const auto& candidate : projected_row.cells)
                    if (candidate.cell_id == cell.node_id) projected_cell = &candidate;
            if (!projected_cell) continue;
            const auto begin = projected_cell->physical_begin >= 0
                ? projected_cell->physical_begin
                : utf16[static_cast<std::size_t>(projected_cell->begin)];
            RECT native{};
            if (!NativeCellBounds(document.Get(), begin, native)) continue;
            auto& row = layout.row_rects[cell.table_row];
            row.left = (std::min)(row.left, native.left);
            row.right = (std::max)(row.right, native.right);
            row.top = (std::min)(row.top, native.top);
            row.bottom = (std::max)(row.bottom, native.bottom);
            auto& boundary = layout.column_boundaries[cell.table_column];
            const auto observed = native.left - padding;
            boundary = boundary == LONG_MIN ? observed : (std::min)(boundary, observed);
            layout.column_boundaries.back() = (std::max)(
                layout.column_boundaries.back(), native.right + padding);
        }

        if (layout.column_boundaries.front() == LONG_MIN) continue;
        static_cast<void>(formatting);
        bool complete = std::all_of(layout.row_rects.begin(), layout.row_rects.end(),
            [](const RECT& row) { return row.top != INT_MAX && row.bottom != INT_MIN; });
        complete = complete && std::none_of(layout.column_boundaries.begin(),
            layout.column_boundaries.end(), [](LONG value) { return value == LONG_MIN; });
        if (!complete) continue;

        static_cast<void>(vertical_space);
        layout.table_rect = {layout.column_boundaries.front(), layout.row_rects.front().top,
            layout.column_boundaries.back(), layout.row_rects.back().bottom};
        for (const auto& cell : projection.spans) {
            if (cell.kind != document::NodeKind::table_cell ||
                cell.table_id != table.node_id || cell.table_row >= rows ||
                cell.table_column >= columns) continue;
            const RECT rect{layout.column_boundaries[cell.table_column],
                layout.row_rects[cell.table_row].top,
                layout.column_boundaries[cell.table_column + 1U],
                layout.row_rects[cell.table_row].bottom};
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
