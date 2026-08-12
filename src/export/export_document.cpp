#include "markdownmay/export/export_document.hpp"

namespace markdownmay::exporting {
namespace {

ExportNode CopyNode(const document::Node& node) {
    ExportNode copy{node.id, node.kind, node.source, node.attributes, node.text, {}};
    copy.children.reserve(node.children.size());
    for (const auto& child : node.children) copy.children.push_back(CopyNode(*child));
    return copy;
}

void CopyHeadings(const document::Node& node, std::vector<ExportNode>& output) {
    if (node.kind == document::NodeKind::heading) output.push_back(CopyNode(node));
    for (const auto& child : node.children) CopyHeadings(*child, output);
}

}  // namespace

bool IsSupportedCombination(ExportScope scope, ExportFormat format) noexcept {
    return format != ExportFormat::html || scope == ExportScope::full;
}

Result<ExportDocument> BuildExportDocument(
    const document::SessionSnapshot& snapshot,
    std::uint64_t expected_revision,
    ExportScope scope,
    ExportFormat format) {
    if (!IsSupportedCombination(scope, format))
        return Result<ExportDocument>::failure(ErrorCode::export_invalid_options);
    if (!snapshot.semantic || snapshot.source_revision != expected_revision ||
        snapshot.parsed_revision != snapshot.source_revision ||
        snapshot.semantic->revision() != snapshot.source_revision ||
        !snapshot.semantic->validate(snapshot.source.size()))
        return Result<ExportDocument>::failure(ErrorCode::export_revision_not_current);

    ExportDocument result{snapshot.source_revision, scope, {}};
    const auto& root = *snapshot.semantic->root();
    if (scope == ExportScope::outline) {
        CopyHeadings(root, result.blocks);
    } else {
        result.blocks.reserve(root.children.size());
        for (const auto& child : root.children) result.blocks.push_back(CopyNode(*child));
    }
    return Result<ExportDocument>::success(std::move(result));
}

}  // namespace markdownmay::exporting
