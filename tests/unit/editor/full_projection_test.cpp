#include "markdownmay/editor/rich_projection.hpp"

#include "markdownmay/fileio/file_service.hpp"
#include "markdownmay/markdown/markdown_parser.hpp"

#include <algorithm>
#include <filesystem>

namespace {
bool HasSpan(const markdownmay::editor::RichProjection& projection,
             markdownmay::document::NodeKind kind) {
    return std::ranges::any_of(projection.spans,
        [kind](const auto& span) { return span.kind == kind; });
}
}

int main() {
    using namespace markdownmay;
    const auto loaded = fileio::LoadTextFile(
        std::filesystem::path(MARKDOWNMAY_COMPLETE_FIXTURE));
    if (!loaded.is_ok()) return 1;
    const auto parsed = markdown::ParseMarkdown(loaded.value().source, 1);
    if (!parsed) return 2;
    const auto projection = editor::BuildInlineProjection(
        *parsed, loaded.value().source, loaded.value().path);

    if (projection.text.find("完整文档") == std::string::npos ||
        projection.text.find("&、©、中") == std::string::npos ||
        projection.text.find("*不是斜体*") == std::string::npos ||
        projection.text.find("原始 HTML 块必须可见且原样保留") == std::string::npos ||
        projection.text.find("<section data-keep=\"yes\">") == std::string::npos) return 3;
    if (projection.text.find("# 完整文档") != std::string::npos ||
        projection.text.find("```cpp") != std::string::npos ||
        projection.text.find("| --- |") != std::string::npos) return 4;
    if (projection.source_offsets.size() != projection.text.size() + 1) return 5;
    if (projection.source_offsets.back() > loaded.value().source.size()) return 5;
    for (std::size_t index = 1; index < projection.source_offsets.size(); ++index) {
        if (projection.source_offsets[index] < projection.source_offsets[index - 1]) {
            return 5;
        }
    }

    using document::NodeKind;
    for (const auto kind : {NodeKind::heading, NodeKind::emphasis, NodeKind::strong,
             NodeKind::strike, NodeKind::inline_code, NodeKind::link, NodeKind::quote,
             NodeKind::list_item, NodeKind::code_block, NodeKind::thematic_break,
             NodeKind::table, NodeKind::table_cell, NodeKind::image,
             NodeKind::unknown_block}) {
        if (!HasSpan(projection, kind)) return 6;
    }

    const std::string empty_item_source = "paragraph\n\n\n*\n\n## next\n";
    const auto empty_item_document = markdown::ParseMarkdown(empty_item_source, 2);
    if (!empty_item_document) return 7;
    const auto empty_item_projection = editor::BuildInlineProjection(
        *empty_item_document, empty_item_source, {});
    const auto paragraph_at = empty_item_projection.text.find("paragraph");
    const auto bullet_at = empty_item_projection.text.find("\xE2\x80\xA2 ");
    const auto next_at = empty_item_projection.text.find("next");
    if (paragraph_at == std::string::npos || bullet_at == std::string::npos ||
        next_at == std::string::npos || !(paragraph_at < bullet_at && bullet_at < next_at))
        return 8;
    if (empty_item_projection.text.find("\n\n\n\n\n", paragraph_at) != std::string::npos)
        return 9;
    if (empty_item_projection.source_offsets[bullet_at] < empty_item_source.find('*'))
        return 10;
    return 0;
}
