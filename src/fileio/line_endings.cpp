#include "markdownmay/fileio/line_endings.hpp"

namespace markdownmay::fileio {

LineEnding DetectLineEnding(std::string_view text) noexcept {
    std::size_t crlf = 0;
    std::size_t lf = 0;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\n') {
            if (index > 0 && text[index - 1] == '\r') {
                ++crlf;
            } else {
                ++lf;
            }
        }
    }
    return crlf > 0 && lf > 0 ? LineEnding::mixed
                              : (lf > 0 ? LineEnding::lf : LineEnding::crlf);
}

std::string NormalizeLineEndings(
    std::string_view text,
    LineEnding target) {
    const std::string_view separator =
        target == LineEnding::lf ? "\n" : "\r\n";
    std::string output;
    output.reserve(text.size() + text.size() / 16);
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\r') {
            if (index + 1 < text.size() && text[index + 1] == '\n') {
                ++index;
            }
            output.append(separator);
        } else if (text[index] == '\n') {
            output.append(separator);
        } else {
            output.push_back(text[index]);
        }
    }
    return output;
}

}  // namespace markdownmay::fileio
