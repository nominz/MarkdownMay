#include "markdownmay/fileio/image_store.hpp"

#include "markdownmay/fileio/path_utils.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cwchar>
#include <fstream>

namespace markdownmay::fileio {
namespace {

std::string PathUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::filesystem::path Utf8Path(std::string_view text) {
    std::u8string value; value.reserve(text.size());
    for (const unsigned char byte : text) value.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(value);
}

bool StartsWithInsensitive(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() &&
        std::equal(prefix.begin(), prefix.end(), text.begin(), [](char left, char right) {
            return static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(left))) ==
                   static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(right)));
        });
}

bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right) {
    return _wcsicmp(left.lexically_normal().c_str(), right.lexically_normal().c_str()) == 0;
}

bool SameFileContent(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::error_code error;
    if (std::filesystem::file_size(left, error) != std::filesystem::file_size(right, error) || error)
        return false;
    std::ifstream a(left, std::ios::binary), b(right, std::ios::binary);
    constexpr std::size_t size = 64 * 1024;
    std::vector<char> first(size), second(size);
    while (a && b) {
        a.read(first.data(), first.size()); b.read(second.data(), second.size());
        if (a.gcount() != b.gcount() || !std::equal(
                first.begin(), first.begin() + static_cast<std::size_t>(a.gcount()), second.begin()))
            return false;
    }
    return true;
}

std::string RelativeTarget(
    const std::filesystem::path& image,
    const std::filesystem::path& document) {
    std::error_code error;
    auto relative = std::filesystem::relative(image, document.parent_path(), error);
    return PathUtf8(error ? image : relative);
}

}  // namespace

ImageReference ResolveImageReference(
    const std::filesystem::path& document_path,
    std::string markdown_target) {
    if (StartsWithInsensitive(markdown_target, "http://") ||
        StartsWithInsensitive(markdown_target, "https://")) {
        return {std::move(markdown_target), {}, ImageLocationKind::remote};
    }
    const auto raw = Utf8Path(markdown_target);
    const auto resolved = (raw.is_absolute() ? raw : document_path.parent_path() / raw)
        .lexically_normal();
    const auto assets = AssetsDirectoryFor(document_path).lexically_normal();
    const bool exists = std::filesystem::is_regular_file(resolved);
    return {std::move(markdown_target), resolved,
        !exists ? ImageLocationKind::missing :
        IsPathInside(resolved, assets) ? ImageLocationKind::managed :
                                        ImageLocationKind::external_local};
}

Result<ImageReference> ImportImageFile(
    const std::filesystem::path& document_path,
    const std::filesystem::path& source_image,
    bool copy_to_assets) {
    if (document_path.empty() || !std::filesystem::is_regular_file(source_image))
        return Result<ImageReference>::failure(ErrorCode::image_import_failed);
    if (!copy_to_assets) {
        return Result<ImageReference>::success(ResolveImageReference(
            document_path, RelativeTarget(source_image, document_path)));
    }
    const auto assets = AssetsDirectoryFor(document_path);
    std::error_code error; std::filesystem::create_directories(assets, error);
    if (error) return Result<ImageReference>::failure(ErrorCode::image_import_failed);
    const auto stem = source_image.stem().wstring(); const auto extension = source_image.extension();
    auto target = assets / source_image.filename();
    for (std::uint32_t suffix = 2; std::filesystem::exists(target); ++suffix) {
        if (SameFileContent(source_image, target)) {
            return Result<ImageReference>::success(ResolveImageReference(
                document_path, RelativeTarget(target, document_path)));
        }
        target = assets / (stem + L"_" + std::to_wstring(suffix) + extension.wstring());
    }
    const auto temporary = target.wstring() + L".tmp";
    if (!CopyFileW(source_image.c_str(), temporary.c_str(), TRUE) ||
        !MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return Result<ImageReference>::failure(ErrorCode::image_import_failed);
    }
    return Result<ImageReference>::success(ResolveImageReference(
        document_path, RelativeTarget(target, document_path)));
}

Result<ManagedImageRename> MarkManagedImageDeleted(
    const std::filesystem::path& document_path,
    const ImageReference& image,
    std::span<const ImageReference> remaining) {
    const auto assets = AssetsDirectoryFor(document_path).lexically_normal();
    if (image.kind != ImageLocationKind::managed ||
        !IsPathInside(image.resolved_path, assets) ||
        !std::filesystem::is_regular_file(image.resolved_path))
        return Result<ManagedImageRename>::failure(ErrorCode::image_assets_path_unsafe);
    for (const auto& other : remaining) {
        if (SamePath(other.resolved_path, image.resolved_path))
            return Result<ManagedImageRename>::success({image.resolved_path, {}, false});
    }
    auto marked = image.resolved_path.parent_path() /
        (L"del_" + image.resolved_path.filename().wstring());
    for (std::uint32_t suffix = 2; std::filesystem::exists(marked); ++suffix) {
        marked = image.resolved_path.parent_path() /
            (L"del_" + std::to_wstring(suffix) + L"_" + image.resolved_path.filename().wstring());
    }
    if (!MoveFileExW(image.resolved_path.c_str(), marked.c_str(), MOVEFILE_WRITE_THROUGH))
        return Result<ManagedImageRename>::failure(ErrorCode::image_mark_deleted_failed);
    return Result<ManagedImageRename>::success({image.resolved_path, marked, true});
}

ErrorCode UndoManagedImageDeleted(const ManagedImageRename& rename) {
    if (!rename.completed) return ErrorCode::ok;
    if (std::filesystem::exists(rename.original_path)) return ErrorCode::image_restore_name_conflict;
    return MoveFileExW(rename.marked_path.c_str(), rename.original_path.c_str(), MOVEFILE_WRITE_THROUGH)
        ? ErrorCode::ok : ErrorCode::image_restore_name_conflict;
}

Result<std::filesystem::path> RestoreManagedImageDeletedSafely(
    const ManagedImageRename& rename) {
    if (!rename.completed)
        return Result<std::filesystem::path>::success(rename.original_path);
    auto target = rename.original_path;
    if (std::filesystem::exists(target)) {
        const auto stem = target.stem().wstring();
        const auto extension = target.extension().wstring();
        for (std::uint32_t suffix = 2; std::filesystem::exists(target); ++suffix) {
            target = rename.original_path.parent_path() /
                (stem + L"_restored_" + std::to_wstring(suffix) + extension);
        }
    }
    if (!MoveFileExW(rename.marked_path.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
        return Result<std::filesystem::path>::failure(ErrorCode::image_restore_name_conflict);
    return Result<std::filesystem::path>::success(std::move(target));
}

std::string ImageMarkdownTarget(const std::filesystem::path& document_path,
                                const std::filesystem::path& image_path) {
    return RelativeTarget(image_path, document_path);
}

}  // namespace markdownmay::fileio
