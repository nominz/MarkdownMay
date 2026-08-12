#pragma once

#include <filesystem>
#include <string_view>

namespace markdownmay::prototype {

[[nodiscard]] bool WriteSafeHtmlProbe(
    const std::filesystem::path& target,
    std::string_view untrusted_text,
    std::string_view mermaid_source,
    std::string_view formula_source) noexcept;

}  // namespace markdownmay::prototype
