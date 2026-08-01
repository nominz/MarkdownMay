#include "markdownmay/markdown/markdown_parser.hpp"

#include <functional>
#include <string_view>

int RunDocumentSessionTests();

int main() {
    using namespace markdownmay::document;
    constexpr std::string_view source =
        "# 马冬梅\n\n"
        "正文有 **粗体**、[链接](local.md) 和 ![图片](a.png)。\n\n"
        "- [x] 完成\n"
        "- [ ] 未完成\n\n"
        "| A | B |\n| - | - |\n| 1 | 2 |\n\n"
        "<div data-x=\"1\">保留</div>\n";
    const auto document = markdownmay::markdown::ParseMarkdown(source, 7);
    if (!document || document->revision() != 7 ||
        !document->validate(source.size())) return 1;

    std::size_t headings = 0, tables = 0, tasks = 0, unknown = 0;
    NodeId heading_id = 0;
    std::function<void(const Node&)> visit = [&](const Node& node) {
        if (document->find(node.id) != &node) headings = 1000;
        if (node.kind == NodeKind::heading) {
            ++headings; heading_id = node.id;
            const auto* value = std::get_if<HeadingAttributes>(&node.attributes);
            if (!value || value->level != 1) headings = 1000;
        } else if (node.kind == NodeKind::table) {
            ++tables;
        } else if (node.kind == NodeKind::list_item) {
            const auto* value = std::get_if<ListItemAttributes>(&node.attributes);
            if (value && value->task) ++tasks;
        } else if (node.kind == NodeKind::unknown_block) {
            ++unknown;
            if (node.text.find("data-x") == std::string::npos) unknown = 1000;
        }
        for (const auto& child : node.children) visit(*child);
    };
    visit(*document->root());
    if (headings != 1 || tables != 1 || tasks != 2 || unknown != 1) return 2;
    if (heading_id == 0 || document->find(heading_id) == nullptr) return 3;
    if (document->root()->source.begin != 0 ||
        document->root()->source.end != source.size()) return 4;
    constexpr std::string_view soft_break = "> 第一行\n> 第二行\n";
    const auto with_soft_break = markdownmay::markdown::ParseMarkdown(soft_break, 8);
    if (!with_soft_break || !with_soft_break->validate(soft_break.size())) return 5;
    return RunDocumentSessionTests();
}
