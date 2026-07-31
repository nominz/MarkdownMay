#pragma once

#include "markdownmay/document/document.hpp"
#include "markdownmay/fileio/line_endings.hpp"

#include <string>

namespace markdownmay::markdown {

struct WriteOptions final {
    fileio::LineEnding line_ending{fileio::LineEnding::crlf};
    bool preserve_unknown_blocks{true};
};

[[nodiscard]] std::string WriteMarkdown(
    const document::Document& document,
    const WriteOptions& options = {});

}  // namespace markdownmay::markdown
