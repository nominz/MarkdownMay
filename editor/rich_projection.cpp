#include "markdownmay/editor/rich_projection.hpp"

#include <algorithm>

namespace markdownmay::editor {
namespace {

void AppendMapped(RichProjection& output, std::string_view text, std::uint64_t source_begin) {
    if (output.source_offsets.empty()) output.source_offsets.push_back(source_begin);
    for (std::size_t index = 0; index < text.size(); ++index) {
        output.text.push_back(text[index]);
        output.source_offsets.push_back(source_begin + index + 1U);
    }
}

void Inline(const document::Node& node, RichProjection& output) {
    const auto begin = static_cast<std::uint64_t>(output.text.size());
    if (node.kind == document::NodeKind::text ||
        node.kind == document::NodeKind::inline_code) {
        AppendMapped(output, node.text, node.source.begin);
    } else {
        for (const auto& child : node.children) Inline(*child, output);
    }
    const auto end = static_cast<std::uint64_t>(output.text.size());
    if (node.kind == document::NodeKind::emphasis ||
        node.kind == document::NodeKind::strong ||
        node.kind == document::NodeKind::strike ||
        node.kind == document::NodeKind::inline_code ||
        node.kind == document::NodeKind::link) {
        output.spans.push_back({node.kind, begin, end, 0, 0, false, false});
    }
}

void AppendNewlines(RichProjection& output, std::string_view source,
                    std::uint64_t begin, std::uint64_t end) {
    end = (std::min)(end, static_cast<std::uint64_t>(source.size()));
    for (auto offset = begin; offset < end; ++offset) {
        const auto value = source[static_cast<std::size_t>(offset)];
        if (value == '\r' || value == '\n') AppendMapped(output, {&value, 1}, offset);
    }
}

void AppendSynthetic(RichProjection& output, std::string_view text,
                     std::uint64_t source_begin, std::uint64_t source_end) {
    if (output.source_offsets.empty()) output.source_offsets.push_back(source_begin);
    for (const auto value : text) {
        output.text.push_back(value);
        output.source_offsets.push_back(source_begin);
    }
    output.source_offsets.back() = source_end;
}

void Block(const document::Node& node, RichProjection& output,
           std::string_view source, std::uint8_t depth = 0) {
    const auto begin = static_cast<std::uint64_t>(output.text.size());
    if (node.kind == document::NodeKind::text ||
        node.kind == document::NodeKind::emphasis ||
        node.kind == document::NodeKind::strong ||
        node.kind == document::NodeKind::strike ||
        node.kind == document::NodeKind::inline_code ||
        node.kind == document::NodeKind::link) {
        Inline(node, output);
    } else if (node.kind == document::NodeKind::paragraph ||
        node.kind == document::NodeKind::heading) {
        for (const auto& child : node.children) Inline(*child, output);
    } else if (node.kind == document::NodeKind::quote) {
        std::uint64_t cursor{};
        bool first = true;
        for (const auto& child : node.children) {
            if (!first && child->source.begin > cursor)
                AppendNewlines(output, source, cursor, child->source.begin);
            Block(*child, output, source, depth);
            cursor = child->source.end;
            first = false;
        }
    } else if (node.kind == document::NodeKind::code_block) {
        AppendMapped(output, node.text, node.source.begin);
    } else if (node.kind == document::NodeKind::thematic_break) {
        AppendSynthetic(output, "────────", node.source.begin, node.source.end);
    } else if (node.kind == document::NodeKind::list) {
        const auto* list = std::get_if<document::ListAttributes>(&node.attributes);
        std::uint32_t number = list ? list->start : 1;
        bool first = true;
        std::uint64_t cursor{};
        for (const auto& item : node.children) {
            if (!first) {
                const auto newline_at = cursor < source.size() ? cursor : item->source.begin;
                AppendSynthetic(output, "\n", newline_at, newline_at);
            }
            const auto item_begin = static_cast<std::uint64_t>(output.text.size());
            const auto* detail = std::get_if<document::ListItemAttributes>(&item->attributes);
            std::string marker;
            if (detail && detail->task) marker = detail->checked ? "☑ " : "☐ ";
            else if (list && list->ordered) marker = std::to_string(number++) + ". ";
            else marker = "• ";
            AppendSynthetic(output, marker, item->source.begin, item->source.begin);
            for (const auto& child : item->children) {
                if (child->kind == document::NodeKind::list &&
                    !output.text.empty() && output.text.back() != '\n')
                    AppendSynthetic(output, "\n", child->source.begin, child->source.begin);
                Block(*child, output, source, static_cast<std::uint8_t>(depth + 1));
            }
            const auto item_end = static_cast<std::uint64_t>(output.text.size());
            output.spans.push_back({document::NodeKind::list_item, item_begin, item_end,
                0, depth, detail && detail->task, detail && detail->checked});
            cursor = item->source.end;
            first = false;
        }
    }
    const auto end = static_cast<std::uint64_t>(output.text.size());
    if (node.kind == document::NodeKind::heading ||
        node.kind == document::NodeKind::quote ||
        node.kind == document::NodeKind::code_block ||
        node.kind == document::NodeKind::thematic_break) {
        std::uint8_t level{};
        if (const auto* heading = std::get_if<document::HeadingAttributes>(&node.attributes))
            level = heading->level;
        output.spans.push_back({node.kind, begin, end, level, depth, false, false});
    }
}

}  // namespace

RichProjection BuildInlineProjection(
    const document::Document& document,
    std::string_view source) {
    RichProjection output;
    std::uint64_t source_cursor{};
    for (const auto& block : document.root()->children) {
        if (!output.text.empty() && block->source.begin > source_cursor)
            AppendNewlines(output, source, source_cursor, block->source.begin);
        Block(*block, output, source);
        source_cursor = block->source.end;
    }
    if (source_cursor < source.size())
        AppendNewlines(output, source, source_cursor, static_cast<std::uint64_t>(source.size()));
    if (output.source_offsets.empty()) output.source_offsets.push_back(0);
    return output;
}

}  // namespace markdownmay::editor
