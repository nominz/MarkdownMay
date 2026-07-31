#include "markdownmay/fileio/line_endings.hpp"
#include "markdownmay/fileio/text_encoding.hpp"

#include <array>
#include <cstddef>
#include <string>

namespace {

int Require(bool condition, int code) { return condition ? 0 : code; }

}  // namespace

int RunFileServiceTests();

int main() {
    using namespace markdownmay::fileio;
    constexpr std::string_view chinese = "马冬梅 Markdown May 😀";
    if (const int result = Require(IsValidUtf8(chinese), 1)) return result;
    const std::string invalid("\xc0\xaf", 2);
    if (const int result = Require(!IsValidUtf8(invalid), 2)) return result;

    for (const auto encoding : {TextEncoding::utf8, TextEncoding::utf8_bom,
                                TextEncoding::utf16_le, TextEncoding::utf16_be}) {
        auto encoded = EncodeText(chinese, encoding);
        if (const int result = Require(encoded.is_ok(), 3)) return result;
        auto decoded = DecodeText(encoded.value());
        if (const int result = Require(decoded.is_ok(), 4)) return result;
        if (const int result = Require(decoded.value().utf8 == chinese, 5)) return result;
        if (const int result = Require(decoded.value().encoding == encoding, 6)) return result;
    }

    if (const int result = Require(
            DetectLineEnding("a\r\nb\r\n") == LineEnding::crlf, 7)) return result;
    if (const int result = Require(
            DetectLineEnding("a\nb\n") == LineEnding::lf, 8)) return result;
    if (const int result = Require(
            DetectLineEnding("a\r\nb\n") == LineEnding::mixed, 9)) return result;
    if (const int result = Require(
            NormalizeLineEndings("a\r\nb\nc\r", LineEnding::crlf) ==
                "a\r\nb\r\nc\r\n", 10)) return result;
    return RunFileServiceTests();
}
