#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace markdownmay {

enum class ErrorCode : std::uint32_t;

using Revision = std::uint64_t;
using NodeId = std::uint64_t;
using DocumentId = std::uint64_t;
using TransactionId = std::uint64_t;
using Utf8Text = std::string;
using Utf8View = std::string_view;
using Path = std::filesystem::path;

struct SourceRange final {
    std::uint64_t begin_byte{};
    std::uint64_t end_byte{};
};

enum class ViewMode : std::uint8_t {
    render,
    source,
    split,
};

enum class TextEncoding : std::uint8_t {
    utf8,
    utf8_bom,
    utf16_le,
    utf16_be,
};

enum class LineEnding : std::uint8_t {
    crlf,
    lf,
    mixed,
};

enum class ParseState : std::uint8_t {
    empty,
    parsing,
    valid,
    invalid,
};

struct CancellationToken final {
    [[nodiscard]] bool is_cancelled() const noexcept;
};

}  // namespace markdownmay
