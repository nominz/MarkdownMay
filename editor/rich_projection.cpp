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
        output.spans.push_back({node.kind, begin, end});
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

}  // namespace

RichProjection BuildInlineProjection(
    const document::Document& document,
    std::string_view source) {
    RichProjection output;
    std::uint64_t source_cursor{};
    for (const auto& block : document.root()->children) {
        if (block->kind != document::NodeKind::paragraph) continue;
        if (!output.text.empty() && block->source.begin > source_cursor)
            AppendNewlines(output, source, source_cursor, block->source.begin);
        for (const auto& child : block->children) Inline(*child, output);
        source_cursor = block->source.end;
    }
    if (source_cursor < source.size())
        AppendNewlines(output, source, source_cursor, static_cast<std::uint64_t>(source.size()));
    if (output.source_offsets.empty()) output.source_offsets.push_back(0);
    return output;
}

}  // namespace markdownmay::editor
