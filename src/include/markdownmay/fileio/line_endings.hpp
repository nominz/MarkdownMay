#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace markdownmay::fileio {

enum class LineEnding : std::uint8_t { crlf, lf, mixed };

[[nodiscard]] LineEnding DetectLineEnding(std::string_view text) noexcept;
[[nodiscard]] std::string NormalizeLineEndings(
    std::string_view text,
    LineEnding target);

}  // namespace markdownmay::fileio
