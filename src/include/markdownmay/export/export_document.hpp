#pragma once

#include "markdownmay/core/result.hpp"
#include "markdownmay/document/document_session.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace markdownmay::exporting {

enum class ExportScope : std::uint8_t { outline, full };
enum class ExportFormat : std::uint8_t { pdf, docx, txt, html };

enum class ExportResourceState : std::uint8_t {
    embedded, remote_blocked, missing, too_large, unsafe
};

struct ExportResource final {
    document::NodeId source_id{};
    std::string markdown_target;
    std::filesystem::path source_path;
    ExportResourceState state{ExportResourceState::missing};
    std::vector<std::uint8_t> bytes;
};

struct ExportContext final {
    std::filesystem::path document_path;
    std::uint64_t maximum_resource_bytes{32ULL * 1024ULL * 1024ULL};
    std::uint64_t maximum_total_resource_bytes{64ULL * 1024ULL * 1024ULL};
};

struct ExportNode final {
    document::NodeId source_id{};
    document::NodeKind kind{document::NodeKind::paragraph};
    document::SourceRange source_range;
    document::NodeAttributes attributes;
    std::string text;
    std::vector<ExportNode> children;
};

struct ExportDocument final {
    std::uint64_t revision{};
    ExportScope scope{ExportScope::full};
    std::vector<ExportNode> blocks;
    std::vector<ExportResource> resources;
};

[[nodiscard]] bool IsSupportedCombination(
    ExportScope scope, ExportFormat format) noexcept;

[[nodiscard]] Result<ExportDocument> BuildExportDocument(
    const document::SessionSnapshot& snapshot,
    std::uint64_t expected_revision,
    ExportScope scope,
    ExportFormat format,
    const ExportContext& context = {});

}  // namespace markdownmay::exporting
