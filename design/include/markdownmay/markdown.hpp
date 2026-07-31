#pragma once

#include "markdownmay/document_types.hpp"
#include "markdownmay/error.hpp"

namespace markdownmay {

struct ParseRequest final {
    DocumentId document{};
    Revision source_revision{};
    Utf8View source;
    CancellationToken cancellation;
};

struct WriteOptions final {
    LineEnding line_ending{LineEnding::crlf};
    bool preserve_unknown_blocks{true};
};

class IMarkdownParser {
public:
    virtual ~IMarkdownParser() = default;
    [[nodiscard]] virtual Result<ParseSnapshot> parse(
        const ParseRequest& request) const = 0;
};

class IMarkdownWriter {
public:
    virtual ~IMarkdownWriter() = default;
    [[nodiscard]] virtual Result<Utf8Text> write(
        const SemanticDocument& document,
        const WriteOptions& options) const = 0;
};

}  // namespace markdownmay
