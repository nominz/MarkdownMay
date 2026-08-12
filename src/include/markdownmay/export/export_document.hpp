#pragma once

#include "markdownmay/core/result.hpp"
#include "markdownmay/document/document_session.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace markdownmay::exporting {

enum class ExportScope : std::uint8_t { outline, full };
enum class ExportFormat : std::uint8_t { pdf, docx, txt, html };

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
};

[[nodiscard]] bool IsSupportedCombination(
    ExportScope scope, ExportFormat format) noexcept;

[[nodiscard]] Result<ExportDocument> BuildExportDocument(
    const document::SessionSnapshot& snapshot,
    std::uint64_t expected_revision,
    ExportScope scope,
    ExportFormat format);

}  // namespace markdownmay::exporting
