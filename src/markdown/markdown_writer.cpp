#include "markdownmay/markdown/markdown_writer.hpp"

#include <sstream>

namespace markdownmay::markdown {
namespace {
using namespace document;

std::string EscapeText(std::string_view text) {
    std::string output;
    for (const char value : text) {
        if (value == '\\' || value == '*' || value == '_' || value == '`' ||
            value == '[' || value == ']') output.push_back('\\');
        output.push_back(value);
    }
    return output;
}

std::string Inline(const Node& node);

std::string ChildrenInline(const Node& node) {
    std::string output;
    for (const auto& child : node.children) output += Inline(*child);
    return output;
}

std::string Inline(const Node& node) {
    switch (node.kind) {
        case NodeKind::text: return EscapeText(node.text);
        case NodeKind::emphasis: return "*" + ChildrenInline(node) + "*";
        case NodeKind::strong: return "**" + ChildrenInline(node) + "**";
        case NodeKind::strike: return "~~" + ChildrenInline(node) + "~~";
        case NodeKind::inline_code: {
            const std::string fence = node.text.find('`') == std::string::npos ? "`" : "``";
            return fence + node.text + fence;
        }
        case NodeKind::link:
        case NodeKind::image: {
            const auto* value = std::get_if<LinkAttributes>(&node.attributes);
            if (!value) return {};
            std::string result = node.kind == NodeKind::image ? "![" : "[";
            result += ChildrenInline(node) + "](" + value->target;
            if (!value->title.empty()) result += " \"" + value->title + "\"";
            return result + ")";
        }
        default: return ChildrenInline(node);
    }
}

void Block(const Node& node, std::string& output, const WriteOptions& options);

void WriteTableRows(const Node& node, std::string& output, bool header) {
    for (const auto& row : node.children) {
        if (row->kind != NodeKind::table_row) continue;
        output += "|";
        std::size_t cells = 0;
        for (const auto& cell : row->children) {
            output += " " + ChildrenInline(*cell) + " |";
            ++cells;
        }
        output += "\n";
        if (header) {
            output += "|";
            for (std::size_t index = 0; index < cells; ++index) output += " --- |";
            output += "\n";
            header = false;
        }
    }
}

void Block(const Node& node, std::string& output, const WriteOptions& options) {
    switch (node.kind) {
        case NodeKind::document:
            for (std::size_t index = 0; index < node.children.size(); ++index) {
                Block(*node.children[index], output, options);
                if (index + 1 < node.children.size() &&
                    (output.empty() || output.back() != '\n')) output += "\n";
                if (index + 1 < node.children.size()) output += "\n";
            }
            break;
        case NodeKind::heading: {
            const auto* value = std::get_if<HeadingAttributes>(&node.attributes);
            output.append(value ? value->level : 1, '#');
            output += " " + ChildrenInline(node) + "\n";
            break;
        }
        case NodeKind::paragraph:
            output += ChildrenInline(node) + "\n";
            break;
        case NodeKind::thematic_break:
            output += "---\n";
            break;
        case NodeKind::quote: {
            std::string content;
            for (const auto& child : node.children) Block(*child, content, options);
            std::istringstream lines(content); std::string line;
            while (std::getline(lines, line)) output += "> " + line + "\n";
            break;
        }
        case NodeKind::list: {
            const auto* list = std::get_if<ListAttributes>(&node.attributes);
            std::uint32_t number = list ? list->start : 1;
            for (const auto& item : node.children) {
                const auto* detail = std::get_if<ListItemAttributes>(&item->attributes);
                std::string marker = list && list->ordered
                    ? std::to_string(number++) + ". " : "- ";
                if (detail && detail->task) marker += detail->checked ? "[x] " : "[ ] ";
                std::string content;
                for (const auto& child : item->children) Block(*child, content, options);
                while (!content.empty() && content.back() == '\n') content.pop_back();
                output += marker + content + "\n";
            }
            break;
        }
        case NodeKind::code_block: {
            const auto* code = std::get_if<CodeAttributes>(&node.attributes);
            std::string fence = node.text.find("```") == std::string::npos ? "```" : "````";
            output += fence + (code ? code->language : "") + "\n" + node.text;
            if (output.empty() || output.back() != '\n') output += "\n";
            output += fence + "\n";
            break;
        }
        case NodeKind::table:
            for (const auto& section : node.children) {
                WriteTableRows(*section, output, section->kind == NodeKind::table_head);
            }
            break;
        case NodeKind::unknown_block:
            if (options.preserve_unknown_blocks) {
                output += node.text;
                if (output.empty() || output.back() != '\n') output += "\n";
            }
            break;
        default:
            output += Inline(node);
            break;
    }
}

}  // namespace

std::string WriteMarkdown(const Document& document, const WriteOptions& options) {
    if (!document.root() || options.line_ending == fileio::LineEnding::mixed) return {};
    std::string output;
    Block(*document.root(), output, options);
    return fileio::NormalizeLineEndings(output, options.line_ending);
}

}  // namespace markdownmay::markdown
