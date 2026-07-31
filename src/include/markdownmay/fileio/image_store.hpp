#pragma once

#include "markdownmay/core/result.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace markdownmay::fileio {

enum class ImageLocationKind : std::uint8_t {
    managed, external_local, remote, missing
};

struct ImageReference final {
    std::string markdown_target;
    std::filesystem::path resolved_path;
    ImageLocationKind kind{ImageLocationKind::missing};
};

struct ManagedImageRename final {
    std::filesystem::path original_path;
    std::filesystem::path marked_path;
    bool completed{};
};

[[nodiscard]] ImageReference ResolveImageReference(
    const std::filesystem::path& document_path,
    std::string markdown_target);
[[nodiscard]] Result<ImageReference> ImportImageFile(
    const std::filesystem::path& document_path,
    const std::filesystem::path& source_image,
    bool copy_to_assets);
[[nodiscard]] Result<ManagedImageRename> MarkManagedImageDeleted(
    const std::filesystem::path& document_path,
    const ImageReference& image,
    std::span<const ImageReference> remaining);
[[nodiscard]] ErrorCode UndoManagedImageDeleted(
    const ManagedImageRename& rename);

}  // namespace markdownmay::fileio
