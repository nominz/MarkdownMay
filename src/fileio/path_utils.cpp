#include "markdownmay/fileio/path_utils.hpp"

#include <system_error>
#include <cwchar>

namespace markdownmay::fileio {

Result<std::filesystem::path> NormalizeAbsolutePath(
    const std::filesystem::path& path) {
    if (path.empty()) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::file_write_failed);
    }
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error);
    if (error) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::file_write_failed);
    }
    return Result<std::filesystem::path>::success(
        absolute.lexically_normal());
}

bool IsPathInside(
    const std::filesystem::path& child,
    const std::filesystem::path& parent) noexcept {
    const auto normalized_child = child.lexically_normal();
    const auto normalized_parent = parent.lexically_normal();
    auto child_part = normalized_child.begin();
    for (auto parent_part = normalized_parent.begin();
         parent_part != normalized_parent.end();
         ++parent_part, ++child_part) {
        if (child_part == normalized_child.end() ||
            _wcsicmp(child_part->c_str(), parent_part->c_str()) != 0) {
            return false;
        }
    }
    return true;
}

std::filesystem::path AssetsDirectoryFor(
    const std::filesystem::path& document_path) {
    auto name = document_path.filename();
    name.replace_extension();
    name += L".assets";
    return document_path.parent_path() / name;
}

}  // namespace markdownmay::fileio
