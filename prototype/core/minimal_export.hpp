#pragma once

#include <filesystem>
#include <string_view>

namespace markdownmay::prototype {

[[nodiscard]] bool WriteMinimalPdf(
    const std::filesystem::path& target,
    std::string_view ascii_title) noexcept;

[[nodiscard]] bool WriteMinimalDocx(
    const std::filesystem::path& target,
    std::string_view utf8_title) noexcept;

}  // namespace markdownmay::prototype
