#include "markdownmay/fileio/text_encoding.hpp"

#include <array>

namespace markdownmay::fileio {
namespace {

void AppendUtf8(std::string& output, std::uint32_t value) {
    if (value <= 0x7fU) {
        output.push_back(static_cast<char>(value));
    } else if (value <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (value >> 6U)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3fU)));
    } else if (value <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (value >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3fU)));
    } else {
        output.push_back(static_cast<char>(0xf0U | (value >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3fU)));
    }
}

bool DecodeUtf8CodePoint(
    std::string_view text,
    std::size_t& index,
    std::uint32_t& value) noexcept {
    const auto lead = static_cast<std::uint8_t>(text[index]);
    std::size_t count = 0;
    std::uint32_t minimum = 0;
    if (lead <= 0x7fU) {
        value = lead;
        ++index;
        return true;
    }
    if ((lead & 0xe0U) == 0xc0U) {
        count = 2; value = lead & 0x1fU; minimum = 0x80U;
    } else if ((lead & 0xf0U) == 0xe0U) {
        count = 3; value = lead & 0x0fU; minimum = 0x800U;
    } else if ((lead & 0xf8U) == 0xf0U) {
        count = 4; value = lead & 0x07U; minimum = 0x10000U;
    } else {
        return false;
    }
    if (index + count > text.size()) {
        return false;
    }
    for (std::size_t offset = 1; offset < count; ++offset) {
        const auto next = static_cast<std::uint8_t>(text[index + offset]);
        if ((next & 0xc0U) != 0x80U) {
            return false;
        }
        value = (value << 6U) | (next & 0x3fU);
    }
    index += count;
    return value >= minimum && value <= 0x10ffffU &&
           !(value >= 0xd800U && value <= 0xdfffU);
}

}  // namespace

bool IsValidUtf8(std::string_view text) noexcept {
    std::size_t index = 0;
    while (index < text.size()) {
        std::uint32_t ignored = 0;
        if (!DecodeUtf8CodePoint(text, index, ignored)) {
            return false;
        }
    }
    return true;
}

Result<DecodedText> DecodeText(std::span<const std::byte> bytes) {
    const auto byte = [&](std::size_t index) {
        return std::to_integer<std::uint8_t>(bytes[index]);
    };
    if (bytes.size() >= 3 && byte(0) == 0xefU &&
        byte(1) == 0xbbU && byte(2) == 0xbfU) {
        const std::string text(
            reinterpret_cast<const char*>(bytes.data() + 3), bytes.size() - 3);
        return IsValidUtf8(text)
            ? Result<DecodedText>::success({text, TextEncoding::utf8_bom})
            : Result<DecodedText>::failure(ErrorCode::file_encoding_invalid);
    }
    if (bytes.size() >= 2 &&
        ((byte(0) == 0xffU && byte(1) == 0xfeU) ||
         (byte(0) == 0xfeU && byte(1) == 0xffU))) {
        if ((bytes.size() - 2) % 2 != 0) {
            return Result<DecodedText>::failure(ErrorCode::file_encoding_invalid);
        }
        const bool little = byte(0) == 0xffU;
        std::string output;
        for (std::size_t index = 2; index < bytes.size(); index += 2) {
            const auto unit = static_cast<std::uint16_t>(little
                ? byte(index) | (byte(index + 1) << 8U)
                : (byte(index) << 8U) | byte(index + 1));
            std::uint32_t value = unit;
            if (unit >= 0xd800U && unit <= 0xdbffU) {
                if (index + 3 >= bytes.size()) {
                    return Result<DecodedText>::failure(ErrorCode::file_encoding_invalid);
                }
                const auto low = static_cast<std::uint16_t>(little
                    ? byte(index + 2) | (byte(index + 3) << 8U)
                    : (byte(index + 2) << 8U) | byte(index + 3));
                if (low < 0xdc00U || low > 0xdfffU) {
                    return Result<DecodedText>::failure(ErrorCode::file_encoding_invalid);
                }
                value = 0x10000U + ((unit - 0xd800U) << 10U) +
                    (low - 0xdc00U);
                index += 2;
            } else if (unit >= 0xdc00U && unit <= 0xdfffU) {
                return Result<DecodedText>::failure(ErrorCode::file_encoding_invalid);
            }
            AppendUtf8(output, value);
        }
        return Result<DecodedText>::success({
            std::move(output), little ? TextEncoding::utf16_le
                                      : TextEncoding::utf16_be});
    }
    const std::string text(
        reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return IsValidUtf8(text)
        ? Result<DecodedText>::success({text, TextEncoding::utf8})
        : Result<DecodedText>::failure(ErrorCode::file_encoding_invalid);
}

Result<std::vector<std::byte>> EncodeText(
    std::string_view utf8,
    TextEncoding encoding) {
    if (!IsValidUtf8(utf8)) {
        return Result<std::vector<std::byte>>::failure(
            ErrorCode::file_encoding_invalid);
    }
    std::vector<std::byte> output;
    if (encoding == TextEncoding::utf8 || encoding == TextEncoding::utf8_bom) {
        if (encoding == TextEncoding::utf8_bom) {
            output.insert(output.end(), {std::byte{0xef}, std::byte{0xbb}, std::byte{0xbf}});
        }
        for (const unsigned char value : utf8) {
            output.push_back(static_cast<std::byte>(value));
        }
        return Result<std::vector<std::byte>>::success(std::move(output));
    }
    output.insert(output.end(), encoding == TextEncoding::utf16_le
        ? std::initializer_list<std::byte>{std::byte{0xff}, std::byte{0xfe}}
        : std::initializer_list<std::byte>{std::byte{0xfe}, std::byte{0xff}});
    std::size_t index = 0;
    while (index < utf8.size()) {
        std::uint32_t value = 0;
        DecodeUtf8CodePoint(utf8, index, value);
        const auto append_unit = [&](std::uint16_t unit) {
            const auto lo = static_cast<std::byte>(unit & 0xffU);
            const auto hi = static_cast<std::byte>(unit >> 8U);
            if (encoding == TextEncoding::utf16_le) {
                output.push_back(lo); output.push_back(hi);
            } else {
                output.push_back(hi); output.push_back(lo);
            }
        };
        if (value <= 0xffffU) {
            append_unit(static_cast<std::uint16_t>(value));
        } else {
            value -= 0x10000U;
            append_unit(static_cast<std::uint16_t>(0xd800U + (value >> 10U)));
            append_unit(static_cast<std::uint16_t>(0xdc00U + (value & 0x3ffU)));
        }
    }
    return Result<std::vector<std::byte>>::success(std::move(output));
}

}  // namespace markdownmay::fileio
