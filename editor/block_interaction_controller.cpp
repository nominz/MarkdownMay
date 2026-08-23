#include "markdownmay/editor/block_interaction_controller.hpp"

#include <algorithm>
#include <unordered_map>

namespace markdownmay::editor {
namespace {

bool ValidRange(const document::SourceRange range, const std::uint64_t source_size) noexcept {
    return range.begin <= range.end && range.end <= source_size;
}

bool IsActionable(const document::NodeKind kind) noexcept {
    using document::NodeKind;
    switch (kind) {
    case NodeKind::paragraph:
    case NodeKind::heading:
    case NodeKind::list_item:
    case NodeKind::code_block:
    case NodeKind::table:
    case NodeKind::thematic_break:
    case NodeKind::unknown_block:
    case NodeKind::formula_block:
        return true;
    default:
        return false;
    }
}

void CollectBlocks(const document::Node& node, std::vector<const document::Node*>& output) {
    if (node.kind == document::NodeKind::table) {
        output.push_back(&node);
        return;
    }
    if (node.kind == document::NodeKind::list_item) {
        output.push_back(&node);
        for (const auto& child : node.children)
            if (child->kind == document::NodeKind::list) CollectBlocks(*child, output);
        return;
    }
    if (IsActionable(node.kind)) output.push_back(&node);
    else for (const auto& child : node.children) CollectBlocks(*child, output);
}

std::uint64_t ProjectionPosition(
    const std::vector<std::uint64_t>& offsets,
    const std::uint64_t source_offset) noexcept {
    if (offsets.empty()) return 0;
    const auto found = std::lower_bound(offsets.begin(), offsets.end(), source_offset);
    return static_cast<std::uint64_t>(found - offsets.begin());
}

bool SameRange(const document::SourceRange left, const document::SourceRange right) noexcept {
    return left.begin == right.begin && left.end == right.end;
}

}  // namespace

bool BlockInteractionController::refresh(
    const std::uint64_t document_id,
    const document::SessionSnapshot& snapshot,
    const RichProjection& projection) {
    clear();
    if (document_id == 0 || snapshot.kind != document::DocumentKind::markdown || !snapshot.semantic ||
        snapshot.parsed_revision != snapshot.source_revision ||
        snapshot.semantic->revision() != snapshot.source_revision ||
        projection.source_offsets.size() != projection.text.size() + 1U)
        return false;

    std::vector<const document::Node*> blocks;
    CollectBlocks(*snapshot.semantic->root(), blocks);
    items_.reserve(blocks.size());
    const auto source_size = static_cast<std::uint64_t>(snapshot.source.size());
    for (const auto* node : blocks) {
        if (!node || node->id == 0 || !ValidRange(node->source, source_size)) {
            clear();
            return false;
        }
        const auto begin = ProjectionPosition(projection.source_offsets, node->source.begin);
        auto end = ProjectionPosition(projection.source_offsets, node->source.end);
        if (end < begin) {
            clear();
            return false;
        }
        if (end == begin && end < projection.text.size()) ++end;
        std::uint8_t level{};
        if (const auto* heading = std::get_if<document::HeadingAttributes>(&node->attributes))
            level = (std::clamp)(heading->level, std::uint8_t{1}, std::uint8_t{6});
        items_.push_back({node->id, node->kind, node->source, begin, end, level});
    }
    std::sort(items_.begin(), items_.end(), [](const auto& left, const auto& right) {
        if (left.source_range.begin != right.source_range.begin)
            return left.source_range.begin < right.source_range.begin;
        return left.source_range.end < right.source_range.end;
    });
    document_id_ = document_id;
    revision_ = snapshot.source_revision;
    return true;
}

bool BlockInteractionController::set_visible_layout(
    const std::uint64_t source_revision,
    std::vector<BlockLayoutItem> layout) {
    positioned_.clear();
    if (source_revision == 0 || source_revision != revision_) return false;
    std::unordered_map<document::NodeId, std::size_t> indices;
    indices.reserve(items_.size());
    for (std::size_t index = 0; index < items_.size(); ++index)
        indices.emplace(items_[index].node_id, index);
    positioned_.reserve(layout.size());
    for (const auto& entry : layout) {
        const auto found = indices.find(entry.node_id);
        if (found == indices.end() || entry.rect.right <= entry.rect.left ||
            entry.rect.bottom <= entry.rect.top) {
            positioned_.clear();
            return false;
        }
        positioned_.push_back({found->second, entry.rect});
    }
    std::sort(positioned_.begin(), positioned_.end(), [](const auto& left, const auto& right) {
        if (left.rect.top != right.rect.top) return left.rect.top < right.rect.top;
        return left.rect.bottom < right.rect.bottom;
    });
    for (std::size_t index = 1; index < positioned_.size(); ++index) {
        if (positioned_[index].rect.top < positioned_[index - 1].rect.bottom) {
            positioned_.clear();
            return false;
        }
    }
    return true;
}

void BlockInteractionController::clear() noexcept {
    items_.clear();
    positioned_.clear();
    document_id_ = 0;
    revision_ = 0;
}

std::optional<BlockCommandContext> BlockInteractionController::hit_test(
    const std::int32_t x, const std::int32_t y) const noexcept {
    const auto first = std::lower_bound(positioned_.begin(), positioned_.end(), y,
        [](const PositionedItem& item, const std::int32_t value) {
            return item.rect.bottom <= value;
        });
    for (auto cursor = first; cursor != positioned_.end() && cursor->rect.top <= y; ++cursor) {
        if (x < cursor->rect.left || x >= cursor->rect.right || y < cursor->rect.top ||
            y >= cursor->rect.bottom) continue;
        const auto& item = items_[cursor->item_index];
        return BlockCommandContext{document_id_, revision_, item.node_id, item.kind,
            item.source_range};
    }
    return std::nullopt;
}

std::optional<BlockScreenRect> BlockInteractionController::layout_rect(
    const document::NodeId node_id) const noexcept {
    for (const auto& positioned : positioned_)
        if (items_[positioned.item_index].node_id == node_id) return positioned.rect;
    return std::nullopt;
}

std::optional<BlockCommandContext> BlockInteractionController::context_at_source(
    const std::uint64_t source_offset) const noexcept {
    const auto found = std::find_if(items_.begin(), items_.end(),
        [source_offset](const auto& item) {
            return source_offset >= item.source_range.begin &&
                source_offset <= item.source_range.end;
        });
    if (found == items_.end()) return std::nullopt;
    return BlockCommandContext{document_id_, revision_, found->node_id, found->kind,
        found->source_range};
}

bool BlockInteractionController::validate(
    const BlockCommandContext& context,
    const std::uint64_t document_id,
    const document::SessionSnapshot& snapshot) const noexcept {
    if (document_id == 0 || document_id != document_id_ || context.document_id != document_id ||
        context.source_revision == 0 || context.source_revision != revision_ ||
        snapshot.kind != document::DocumentKind::markdown || !snapshot.semantic ||
        snapshot.source_revision != context.source_revision ||
        snapshot.parsed_revision != snapshot.source_revision ||
        snapshot.semantic->revision() != snapshot.source_revision) return false;
    const auto* node = snapshot.semantic->find(context.node_id);
    return node && node->kind == context.kind && SameRange(node->source, context.source_range) &&
        ValidRange(node->source, static_cast<std::uint64_t>(snapshot.source.size()));
}

const std::vector<VisibleBlockItem>& BlockInteractionController::items() const noexcept {
    return items_;
}
std::uint64_t BlockInteractionController::revision() const noexcept { return revision_; }

}  // namespace markdownmay::editor
