#include "markdownmay/export/export_document.hpp"

#include "markdownmay/fileio/image_store.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

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

void FreezeResources(const document::Node& node, const ExportContext& context,
                     std::uint64_t& total, std::vector<ExportResource>& output) {
    if (node.kind == document::NodeKind::image) {
        const auto* link = std::get_if<document::LinkAttributes>(&node.attributes);
        if (link) {
            ExportResource resource{node.id, link->target};
            if (context.document_path.empty()) {
                resource.state = ExportResourceState::unsafe;
                output.push_back(std::move(resource));
                for (const auto& child : node.children)
                    FreezeResources(*child, context, total, output);
                return;
            }
            const auto resolved = fileio::ResolveImageReference(context.document_path, link->target);
            resource.source_path = resolved.resolved_path;
            if (resolved.kind == fileio::ImageLocationKind::remote) {
                resource.state = ExportResourceState::remote_blocked;
            } else if (resolved.kind == fileio::ImageLocationKind::missing) {
                resource.state = ExportResourceState::missing;
            } else {
                std::error_code error;
                const auto size = std::filesystem::file_size(resolved.resolved_path, error);
                if (error) resource.state = ExportResourceState::missing;
                else if (size > context.maximum_resource_bytes ||
                         total > context.maximum_total_resource_bytes -
                            (std::min)(size, context.maximum_total_resource_bytes)) {
                    resource.state = ExportResourceState::too_large;
                } else {
                    std::ifstream input(resolved.resolved_path, std::ios::binary);
                    resource.bytes.assign(std::istreambuf_iterator<char>(input), {});
                    if (input.bad() || resource.bytes.size() != size) {
                        resource.bytes.clear(); resource.state = ExportResourceState::missing;
                    } else {
                        resource.state = ExportResourceState::embedded;
                        total += size;
                    }
                }
            }
            output.push_back(std::move(resource));
        }
    }
    for (const auto& child : node.children) FreezeResources(*child, context, total, output);
}

}  // namespace

bool IsSupportedCombination(ExportScope scope, ExportFormat format) noexcept {
    return format != ExportFormat::html || scope == ExportScope::full;
}

Result<ExportDocument> BuildExportDocument(
    const document::SessionSnapshot& snapshot,
    std::uint64_t expected_revision,
    ExportScope scope,
    ExportFormat format,
    const ExportContext& context) {
    if (!IsSupportedCombination(scope, format))
        return Result<ExportDocument>::failure(ErrorCode::export_invalid_options);
    if (!snapshot.semantic || snapshot.source_revision != expected_revision ||
        snapshot.parsed_revision != snapshot.source_revision ||
        snapshot.semantic->revision() != snapshot.source_revision ||
        !snapshot.semantic->validate(snapshot.source.size()))
        return Result<ExportDocument>::failure(ErrorCode::export_revision_not_current);

    ExportDocument result{snapshot.source_revision, scope, {}, {}};
    const auto& root = *snapshot.semantic->root();
    if (scope == ExportScope::outline) {
        CopyHeadings(root, result.blocks);
    } else {
        result.blocks.reserve(root.children.size());
        for (const auto& child : root.children) result.blocks.push_back(CopyNode(*child));
        std::uint64_t total{};
        FreezeResources(root, context, total, result.resources);
    }
    return Result<ExportDocument>::success(std::move(result));
}

}  // namespace markdownmay::exporting
