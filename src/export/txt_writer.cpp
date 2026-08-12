#include "markdownmay/export/txt_writer.hpp"

#include "markdownmay/fileio/text_encoding.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>

namespace markdownmay::exporting {
namespace {

void AppendInline(const ExportNode& node, std::string& output) {
    if (node.kind == document::NodeKind::text ||
        node.kind == document::NodeKind::inline_code) {
        output += node.text;
        return;
    }
    if (node.kind == document::NodeKind::formula_inline ||
        node.kind == document::NodeKind::formula_block) {
        output += node.kind == document::NodeKind::formula_inline ? "[公式：" : "[块公式：";
        output += node.text + "]";
        return;
    }
    std::string visible;
    for (const auto& child : node.children) AppendInline(child, visible);
    if (node.kind == document::NodeKind::link) {
        output += visible;
        if (const auto* link = std::get_if<document::LinkAttributes>(&node.attributes);
            link && !link->target.empty()) output += "（" + link->target + "）";
    } else if (node.kind == document::NodeKind::image) {
        output += "图片：" + visible;
        if (const auto* image = std::get_if<document::LinkAttributes>(&node.attributes);
            image && !image->target.empty()) output += "（" + image->target + "）";
    } else {
        output += visible;
    }
}

std::string InlineText(const ExportNode& node) {
    std::string output;
    if (!node.text.empty()) output += node.text;
    for (const auto& child : node.children) AppendInline(child, output);
    return output;
}

void AppendLinesWithPrefix(
    std::string_view text, std::string_view prefix, std::string& output) {
    std::size_t begin{};
    while (begin <= text.size()) {
        const auto end = text.find('\n', begin);
        auto line = text.substr(begin, end == std::string_view::npos ?
            text.size() - begin : end - begin);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        output += prefix;
        output += line;
        output += "\r\n";
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
}

void RenderList(const ExportNode& list, std::uint32_t depth, std::string& output) {
    const auto* attributes = std::get_if<document::ListAttributes>(&list.attributes);
    const bool ordered = attributes && attributes->ordered;
    std::uint32_t ordinal = attributes ? attributes->start : 1;
    for (const auto& item : list.children) {
        if (item.kind != document::NodeKind::list_item) continue;
        output.append(depth * 2U, ' ');
        output += ordered ? std::to_string(ordinal++) + ". " : "- ";
        if (const auto* value = std::get_if<document::ListItemAttributes>(&item.attributes);
            value && value->task) output += value->checked ? "[x] " : "[ ] ";
        bool wrote_content{};
        for (const auto& child : item.children) {
            if (child.kind == document::NodeKind::list) continue;
            const auto text = InlineText(child);
            if (text.empty()) continue;
            if (wrote_content) output += " ";
            output += text;
            wrote_content = true;
        }
        output += "\r\n";
        for (const auto& child : item.children)
            if (child.kind == document::NodeKind::list) RenderList(child, depth + 1, output);
    }
}

void RenderTable(const ExportNode& table, std::string& output) {
    const auto render_section = [&](const ExportNode& section) {
        for (const auto& row : section.children) {
            if (row.kind != document::NodeKind::table_row) continue;
            bool first{true};
            for (const auto& cell : row.children) {
                if (!first) output += '\t';
                first = false;
                auto text = InlineText(cell);
                for (auto& value : text)
                    if (value == '\r' || value == '\n') value = ' ';
                output += text;
            }
            output += "\r\n";
        }
    };
    for (const auto& section : table.children) render_section(section);
}

void RenderBlock(const ExportNode& block, std::string& output) {
    switch (block.kind) {
    case document::NodeKind::heading: {
        std::uint8_t level{1};
        if (const auto* value = std::get_if<document::HeadingAttributes>(&block.attributes))
            level = value->level;
        output.append((level > 0 ? level - 1 : 0) * 2U, ' ');
        output += InlineText(block);
        output += "\r\n";
        break;
    }
    case document::NodeKind::paragraph:
        output += InlineText(block) + "\r\n";
        break;
    case document::NodeKind::quote: {
        std::string quoted;
        for (const auto& child : block.children) RenderBlock(child, quoted);
        while (quoted.size() >= 2 && quoted.ends_with("\r\n")) quoted.resize(quoted.size() - 2);
        AppendLinesWithPrefix(quoted, "> ", output);
        break;
    }
    case document::NodeKind::list:
        RenderList(block, 0, output);
        break;
    case document::NodeKind::code_block: {
        const auto* code = std::get_if<document::CodeAttributes>(&block.attributes);
        if (code && code->language == "mermaid") output += "[Mermaid 源码]\r\n";
        AppendLinesWithPrefix(block.text, {}, output);
        break;
    }
    case document::NodeKind::table:
        RenderTable(block, output);
        break;
    case document::NodeKind::thematic_break:
        output += "--------------------\r\n";
        break;
    case document::NodeKind::unknown_block:
        output += "[原始 HTML]\r\n";
        AppendLinesWithPrefix(block.text, {}, output);
        break;
    default: {
        const auto text = InlineText(block);
        if (!text.empty()) output += text + "\r\n";
        break;
    }
    }
}

std::string RenderTxt(const ExportDocument& document, const CancellationToken& cancellation,
                      const ExportProgressSink& progress) {
    std::string output;
    for (std::size_t index = 0; index < document.blocks.size(); ++index) {
        if (cancellation.is_cancelled()) return {};
        RenderBlock(document.blocks[index], output);
        if (index + 1 < document.blocks.size() && !output.ends_with("\r\n\r\n"))
            output += "\r\n";
        if (progress) progress({ExportStage::writing,
            static_cast<std::uint32_t>(10 + (index + 1) * 60 /
                (std::max)(std::size_t{1}, document.blocks.size())), 100});
    }
    return output;
}

}  // namespace

ErrorCode WriteTxt(
    const ExportDocument& document,
    const std::filesystem::path& temporary,
    const CancellationToken& cancellation,
    const ExportProgressSink& progress) {
    if (cancellation.is_cancelled()) return ErrorCode::export_cancelled;
    const auto text = RenderTxt(document, cancellation, progress);
    if (cancellation.is_cancelled()) return ErrorCode::export_cancelled;
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return ErrorCode::export_target_failed;
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return output.good() ? ErrorCode::ok : ErrorCode::export_target_failed;
}

ErrorCode ValidateTxt(const std::filesystem::path& temporary) {
    std::ifstream input(temporary, std::ios::binary);
    if (!input) return ErrorCode::export_validation_failed;
    const std::string text{std::istreambuf_iterator<char>(input), {}};
    if (!fileio::IsValidUtf8(text) ||
        (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xef &&
         static_cast<unsigned char>(text[1]) == 0xbb &&
         static_cast<unsigned char>(text[2]) == 0xbf))
        return ErrorCode::export_validation_failed;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\n' && (index == 0 || text[index - 1] != '\r'))
            return ErrorCode::export_validation_failed;
        if (text[index] == '\r' && (index + 1 >= text.size() || text[index + 1] != '\n'))
            return ErrorCode::export_validation_failed;
    }
    return ErrorCode::ok;
}

ErrorCode ExportTxt(
    const ExportDocument& document,
    const std::filesystem::path& target,
    const CancellationToken& cancellation,
    const ExportProgressSink& progress) {
    return RunExportTask(document, target, WriteTxt, ValidateTxt, cancellation, progress);
}

}  // namespace markdownmay::exporting
