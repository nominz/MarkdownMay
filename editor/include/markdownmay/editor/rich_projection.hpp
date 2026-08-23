#pragma once

#include "markdownmay/document/document.hpp"
#include "markdownmay/editor/image_object.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace markdownmay::editor {

struct ProjectionSpan final {
    document::NodeKind kind{};
    std::uint64_t begin{};
    std::uint64_t end{};
    std::uint8_t heading_level{};
    std::uint8_t list_depth{};
    bool task{};
    bool checked{};
    ImageDisplayState image_state{ImageDisplayState::missing};
    std::uint32_t image_width{};
    std::uint32_t image_height{};
    std::uint16_t image_display_percent{100};
    std::filesystem::path image_path;
    std::uint32_t table_row{};
    std::uint32_t table_column{};
    std::uint32_t table_columns{};
    bool ordered{};
    std::string language;
};

struct RichProjection final {
    std::string text;
    std::vector<std::uint64_t> source_offsets;
    std::vector<ProjectionSpan> spans;
};

[[nodiscard]] RichProjection BuildInlineProjection(
    const document::Document& document,
    std::string_view source,
    const std::filesystem::path& document_path = {});

}  // namespace markdownmay::editor
