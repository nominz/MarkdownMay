#pragma once

#include "markdownmay/core/result.hpp"
#include "markdownmay/fileio/line_endings.hpp"
#include "markdownmay/fileio/text_encoding.hpp"

#include <filesystem>
#include <string>

namespace markdownmay::fileio {

struct LoadedFile final {
    std::filesystem::path path;
    std::string source;
    TextEncoding encoding{TextEncoding::utf8};
    LineEnding line_ending{LineEnding::crlf};
};

struct SaveRequest final {
    std::filesystem::path target;
    std::string_view source;
    TextEncoding encoding{TextEncoding::utf8};
    LineEnding line_ending{LineEnding::crlf};
};

[[nodiscard]] Result<LoadedFile> LoadTextFile(
    const std::filesystem::path& path,
    std::uint64_t maximum_bytes = 100ULL * 1024ULL * 1024ULL);
[[nodiscard]] ErrorCode SaveTextFileAtomic(const SaveRequest& request);

}  // namespace markdownmay::fileio
