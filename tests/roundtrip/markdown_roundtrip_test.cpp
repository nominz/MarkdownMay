#include "markdownmay/markdown/markdown_parser.hpp"
#include "markdownmay/markdown/markdown_writer.hpp"

#include <string>

namespace {
using namespace markdownmay::document;

std::string Signature(const Node& node) {
    std::string result = std::to_string(static_cast<int>(node.kind)) + ":" + node.text;
    if (const auto* heading = std::get_if<HeadingAttributes>(&node.attributes)) {
        result += ":h" + std::to_string(heading->level);
    } else if (const auto* list = std::get_if<ListAttributes>(&node.attributes)) {
        result += list->ordered ? ":ol" : ":ul";
        result += ":" + std::to_string(list->start);
    } else if (const auto* item = std::get_if<ListItemAttributes>(&node.attributes)) {
        result += item->task ? (item->checked ? ":checked" : ":unchecked") : ":item";
    } else if (const auto* link = std::get_if<LinkAttributes>(&node.attributes)) {
        result += ":" + link->target + ":" + link->title;
    } else if (const auto* code = std::get_if<CodeAttributes>(&node.attributes)) {
        result += ":" + code->language;
    }
    result += "[";
    for (const auto& child : node.children) result += Signature(*child);
    return result + "]";
}

}  // namespace

int main() {
    constexpr std::string_view source =
        "## 马冬梅\n\n"
        "正文有 *斜体*、**粗体**、~~删除~~、`代码`、"
        "[链接](local.md \"标题\") 和 ![图片](a.png)。\n\n"
        "> 引用内容\n\n"
        "3. 第三项\n4. 第四项\n\n"
        "- [x] 完成\n- [ ] 未完成\n\n"
        "```cpp\nint main() {}\n```\n\n"
        "| A | B |\n| --- | --- |\n| 1 | 2 |\n\n"
        "<div data-x=\"1\">必须原样保留</div>\n";
    const auto first = markdownmay::markdown::ParseMarkdown(source, 10);
    if (!first) return 1;
    const auto output = markdownmay::markdown::WriteMarkdown(
        *first, {markdownmay::fileio::LineEnding::crlf, true});
    if (output.empty() || output.find("\n") == std::string::npos ||
        output.find("<div data-x=\"1\">必须原样保留</div>") == std::string::npos) return 2;
    for (std::size_t index = 0; index < output.size(); ++index) {
        if (output[index] == '\n' && (index == 0 || output[index - 1] != '\r')) return 3;
    }
    const auto second = markdownmay::markdown::ParseMarkdown(output, 11);
    if (!second) return 4;
    if (Signature(*first->root()) != Signature(*second->root())) return 5;
    const auto without_unknown = markdownmay::markdown::WriteMarkdown(
        *first, {markdownmay::fileio::LineEnding::lf, false});
    if (without_unknown.find("data-x") != std::string::npos ||
        without_unknown.find('\r') != std::string::npos) return 6;
    return 0;
}
