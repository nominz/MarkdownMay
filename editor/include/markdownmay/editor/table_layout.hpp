#pragma once

#include "markdownmay/editor/rich_projection.hpp"

#include <windows.h>

#include <cstdint>
#include <vector>

namespace markdownmay::editor {

struct TableCellLayout final {
    document::NodeId cell_id{};
    std::uint32_t row{};
    std::uint32_t column{};
    RECT rect{};
    RECT content_rect{};
};

struct TableLayout final {
    document::NodeId table_id{};
    std::uint64_t revision{};
    RECT table_rect{};
    std::vector<RECT> row_rects;
    std::vector<LONG> column_boundaries;
    std::vector<TableCellLayout> cells;
};

// Builds a disposable geometry snapshot from the current RichEdit layout.
// RichEdit remains the authority for text layout; no result is cached here.
[[nodiscard]] std::vector<TableLayout> BuildTableLayouts(
    HWND rich_edit, const RichProjection& projection,
    std::uint64_t revision, UINT dpi);

[[nodiscard]] LONG TableHorizontalPadding(UINT dpi) noexcept;

}  // namespace markdownmay::editor
