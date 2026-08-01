#pragma once

#include "markdownmay/document/document.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace markdownmay::editor {

struct ProjectionSpan final {
    document::NodeKind kind{};
    std::uint64_t begin{};
    std::uint64_t end{};
};

struct RichProjection final {
    std::string text;
    std::vector<std::uint64_t> source_offsets;
    std::vector<ProjectionSpan> spans;
};

[[nodiscard]] RichProjection BuildInlineProjection(
    const document::Document& document,
    std::string_view source);

}  // namespace markdownmay::editor
