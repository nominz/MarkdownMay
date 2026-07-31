#pragma once

#include "markdownmay/document_session.hpp"

#include <cstddef>
#include <span>

namespace markdownmay {

struct LoadedFile final {
    FileMetadata metadata;
    Utf8Text source;
};

struct SaveRequest final {
    Path target;
    Utf8View source;
    TextEncoding encoding{TextEncoding::utf8};
    LineEnding line_ending{LineEnding::crlf};
    Revision revision{};
};

enum class ImageLocationKind : std::uint8_t {
    managed,
    external_local,
    remote,
    missing,
};

struct ImageReference final {
    Utf8Text markdown_target;
    Path resolved_path;
    ImageLocationKind kind{ImageLocationKind::missing};
};

struct ManagedImageRename final {
    Path original_path;
    Path marked_path;
    bool completed{};
};

class IFileService {
public:
    virtual ~IFileService() = default;
    [[nodiscard]] virtual Result<LoadedFile> load(const Path& path) const = 0;
    [[nodiscard]] virtual Status save_atomic(const SaveRequest& request) const = 0;
};

class IImageStore {
public:
    virtual ~IImageStore() = default;
    [[nodiscard]] virtual Result<ImageReference> import_file(
        const Path& document_path,
        const Path& source_image,
        bool copy_to_assets) = 0;
    [[nodiscard]] virtual Result<ImageReference> import_bitmap(
        const Path& document_path,
        std::span<const std::byte> encoded_png) = 0;
    [[nodiscard]] virtual Result<ManagedImageRename> mark_deleted_if_unreferenced(
        const Path& document_path,
        const ImageReference& image,
        std::span<const ImageReference> remaining) = 0;
    [[nodiscard]] virtual Status undo_mark_deleted(
        const ManagedImageRename& rename) = 0;
};

}  // namespace markdownmay
