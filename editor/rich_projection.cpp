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

void AppendSynthetic(RichProjection& output, std::string_view text,
                     std::uint64_t source_begin, std::uint64_t source_end);

std::string ChildrenText(const document::Node& node) {
    std::string result;
    for (const auto& child : node.children) {
        if (child->kind == document::NodeKind::text) result += child->text;
        else result += ChildrenText(*child);
    }
    return result;
}

void Inline(const document::Node& node, RichProjection& output,
            std::string_view source, const std::filesystem::path& document_path) {
    const auto begin = static_cast<std::uint64_t>(output.text.size());
    if (node.kind == document::NodeKind::text ||
        node.kind == document::NodeKind::inline_code) {
        AppendMapped(output, node.text, node.source.begin);
    } else if (node.kind == document::NodeKind::image) {
        const auto* attributes = std::get_if<document::LinkAttributes>(&node.attributes);
        const auto alternative = ChildrenText(node);
        const auto target = attributes ? attributes->target : std::string{};
        std::uint16_t display_percent = 100;
        if (attributes) {
            constexpr std::string_view prefix = "markdownmay-width=";
            if (attributes->title.starts_with(prefix) && attributes->title.ends_with('%')) {
                std::uint32_t parsed{};
                for (const auto digit : std::string_view(attributes->title).substr(
                         prefix.size(), attributes->title.size() - prefix.size() - 1)) {
                    if (digit < '0' || digit > '9') { parsed = 0; break; }
                    parsed = parsed * 10 + static_cast<std::uint32_t>(digit - '0');
                }
                if (parsed > 0) display_percent = static_cast<std::uint16_t>(
                    (std::min)(parsed, std::uint32_t{300}));
            }
        }
        auto image = LoadImageObject(document_path, target, alternative, display_percent);
        auto full_begin = node.source.begin >= 2 ? node.source.begin - 2 : node.source.begin;
        const auto open = source.rfind("![", static_cast<std::size_t>(node.source.begin));
        if (open != std::string_view::npos) full_begin = open;
        auto full_end = source.find(')', static_cast<std::size_t>(node.source.end));
        full_end = full_end == std::string_view::npos ? node.source.end : full_end + 1;
        std::string label;
        if (image.state == ImageDisplayState::remote_blocked) label = "[远程图片未加载：" + alternative + "]";
        else if (image.state == ImageDisplayState::missing) label = "[图片缺失：" + alternative + "]";
        else if (image.state == ImageDisplayState::decode_failed) label = "[图片无法显示：" + alternative + "]";
        else label = "\xEF\xBF\xBC";
        AppendSynthetic(output, label, full_begin, full_end);
        const auto image_end = static_cast<std::uint64_t>(output.text.size());
        output.spans.push_back({node.kind, begin, image_end, 0, 0, false, false,
            image.state, image.pixel_width, image.pixel_height, image.display_percent,
            image.reference.resolved_path});
    } else {
        for (const auto& child : node.children) Inline(*child, output, source, document_path);
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
           std::string_view source, const std::filesystem::path& document_path,
           std::uint8_t depth = 0) {
    const auto begin = static_cast<std::uint64_t>(output.text.size());
    if (node.kind == document::NodeKind::text ||
        node.kind == document::NodeKind::emphasis ||
        node.kind == document::NodeKind::strong ||
        node.kind == document::NodeKind::strike ||
        node.kind == document::NodeKind::inline_code ||
        node.kind == document::NodeKind::link ||
        node.kind == document::NodeKind::image) {
        Inline(node, output, source, document_path);
    } else if (node.kind == document::NodeKind::paragraph ||
        node.kind == document::NodeKind::heading) {
        for (const auto& child : node.children) Inline(*child, output, source, document_path);
    } else if (node.kind == document::NodeKind::quote) {
        std::uint64_t cursor{};
        bool first = true;
        for (const auto& child : node.children) {
            if (!first && child->source.begin > cursor)
                AppendNewlines(output, source, cursor, child->source.begin);
            Block(*child, output, source, document_path, depth);
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
                Block(*child, output, source, document_path, static_cast<std::uint8_t>(depth + 1));
            }
            const auto item_end = static_cast<std::uint64_t>(output.text.size());
            output.spans.push_back({document::NodeKind::list_item, item_begin, item_end,
                0, depth, detail && detail->task, detail && detail->checked});
            cursor = item->source.end;
            first = false;
        }
    } else if (node.kind == document::NodeKind::table) {
        std::uint32_t row_index{};
        bool first_row = true;
        for (const auto& section : node.children) {
            for (const auto& row : section->children) {
                if (row->kind != document::NodeKind::table_row) continue;
                if (!first_row) AppendSynthetic(output, "\n", row->source.begin, row->source.begin);
                std::uint32_t column_index{};
                for (const auto& cell : row->children) {
                    if (cell->kind != document::NodeKind::table_cell) continue;
                    if (column_index > 0)
                        AppendSynthetic(output, "\t", cell->source.begin, cell->source.begin);
                    const auto cell_begin = static_cast<std::uint64_t>(output.text.size());
                    for (const auto& child : cell->children)
                        Inline(*child, output, source, document_path);
                    const auto cell_end = static_cast<std::uint64_t>(output.text.size());
                    output.spans.push_back({document::NodeKind::table_cell, cell_begin, cell_end,
                        0, 0, false, false, ImageDisplayState::missing, 0, 0, 100, {},
                        row_index, column_index});
                    ++column_index;
                }
                ++row_index;
                first_row = false;
            }
        }
    }
    const auto end = static_cast<std::uint64_t>(output.text.size());
    if (node.kind == document::NodeKind::heading ||
        node.kind == document::NodeKind::quote ||
        node.kind == document::NodeKind::code_block ||
        node.kind == document::NodeKind::thematic_break ||
        node.kind == document::NodeKind::table) {
        std::uint8_t level{};
        if (const auto* heading = std::get_if<document::HeadingAttributes>(&node.attributes))
            level = heading->level;
        output.spans.push_back({node.kind, begin, end, level, depth, false, false});
    }
}

}  // namespace

RichProjection BuildInlineProjection(
    const document::Document& document,
    std::string_view source,
    const std::filesystem::path& document_path) {
    RichProjection output;
    std::uint64_t source_cursor{};
    for (const auto& block : document.root()->children) {
        if (!output.text.empty() && block->source.begin > source_cursor)
            AppendNewlines(output, source, source_cursor, block->source.begin);
        Block(*block, output, source, document_path);
        source_cursor = block->source.end;
    }
    if (source_cursor < source.size())
        AppendNewlines(output, source, source_cursor, static_cast<std::uint64_t>(source.size()));
    if (output.source_offsets.empty()) output.source_offsets.push_back(0);
    return output;
}

}  // namespace markdownmay::editor
