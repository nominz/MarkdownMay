#pragma once

#include "markdownmay/document/document_session.hpp"
#include "markdownmay/editor/rich_projection.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace markdownmay::editor {

struct BlockScreenRect final {
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};
    std::int32_t bottom{};
};

struct VisibleBlockItem final {
    document::NodeId node_id{};
    document::NodeKind kind{document::NodeKind::paragraph};
    document::SourceRange source_range;
    std::uint64_t projection_begin{};
    std::uint64_t projection_end{};
    std::uint8_t heading_level{};
};

struct BlockLayoutItem final {
    document::NodeId node_id{};
    BlockScreenRect rect;
};

struct BlockCommandContext final {
    std::uint64_t document_id{};
    std::uint64_t source_revision{};
    document::NodeId node_id{};
    document::NodeKind kind{document::NodeKind::paragraph};
    document::SourceRange source_range;
};

class BlockInteractionController final {
public:
    [[nodiscard]] bool refresh(
        std::uint64_t document_id,
        const document::SessionSnapshot& snapshot,
        const RichProjection& projection);
    [[nodiscard]] bool set_visible_layout(
        std::uint64_t source_revision,
        std::vector<BlockLayoutItem> layout);
    void clear() noexcept;

    [[nodiscard]] std::optional<BlockCommandContext> hit_test(
        std::int32_t x, std::int32_t y) const noexcept;
    [[nodiscard]] std::optional<BlockCommandContext> context_at_source(
        std::uint64_t source_offset) const noexcept;
    [[nodiscard]] std::optional<BlockScreenRect> layout_rect(
        document::NodeId node_id) const noexcept;
    [[nodiscard]] bool validate(
        const BlockCommandContext& context,
        std::uint64_t document_id,
        const document::SessionSnapshot& snapshot) const noexcept;
    [[nodiscard]] const std::vector<VisibleBlockItem>& items() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;

private:
    struct PositionedItem final {
        std::size_t item_index{};
        BlockScreenRect rect;
    };
    std::vector<VisibleBlockItem> items_;
    std::vector<PositionedItem> positioned_;
    std::uint64_t document_id_{};
    std::uint64_t revision_{};
};

}  // namespace markdownmay::editor
