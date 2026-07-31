#pragma once

#include "markdownmay/document/document.hpp"

#include <memory>
#include <string_view>

namespace markdownmay::markdown {

[[nodiscard]] std::shared_ptr<const document::Document> ParseMarkdown(
    std::string_view source,
    std::uint64_t revision);

}  // namespace markdownmay::markdown
