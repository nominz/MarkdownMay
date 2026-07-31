#pragma once

#include "markdownmay/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace markdownmay::fileio {

enum class TextEncoding : std::uint8_t {
    utf8,
    utf8_bom,
    utf16_le,
    utf16_be,
};

struct DecodedText final {
    std::string utf8;
    TextEncoding encoding{TextEncoding::utf8};
};

[[nodiscard]] bool IsValidUtf8(std::string_view text) noexcept;
[[nodiscard]] Result<DecodedText> DecodeText(
    std::span<const std::byte> bytes);
[[nodiscard]] Result<std::vector<std::byte>> EncodeText(
    std::string_view utf8,
    TextEncoding encoding);

}  // namespace markdownmay::fileio
