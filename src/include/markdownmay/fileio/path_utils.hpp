#pragma once

#include "markdownmay/core/result.hpp"

#include <filesystem>

namespace markdownmay::fileio {

[[nodiscard]] Result<std::filesystem::path> NormalizeAbsolutePath(
    const std::filesystem::path& path);
[[nodiscard]] bool IsPathInside(
    const std::filesystem::path& child,
    const std::filesystem::path& parent) noexcept;
[[nodiscard]] std::filesystem::path AssetsDirectoryFor(
    const std::filesystem::path& document_path);

}  // namespace markdownmay::fileio
