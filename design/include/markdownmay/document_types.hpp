#pragma once

#include "markdownmay/common.hpp"

#include <memory>
#include <optional>
#include <variant>

namespace markdownmay {

enum class NodeKind : std::uint8_t {
    document,
    paragraph,
    heading,
    quote,
    list,
    list_item,
    code_block,
    table,
    table_row,
    table_cell,
    thematic_break,
    unknown_block,
    text,
    emphasis,
    strong,
    strike,
    inline_code,
    link,
    image,
};

struct NodePosition final {
    NodeId node{};
    std::uint32_t text_offset{};
};

struct SelectionSnapshot final {
    NodePosition anchor;
    NodePosition caret;
};

struct Node final {
    NodeId id{};
    NodeKind kind{NodeKind::paragraph};
    SourceRange source;
    Utf8Text text;
    Utf8Text target;
    Utf8Text title;
    std::uint32_t numeric_value{};
    std::vector<std::shared_ptr<const Node>> children;
};

struct SemanticDocument final {
    NodeId root_id{};
    std::shared_ptr<const Node> root;
    Revision revision{};
};

struct ParseIssue final {
    ErrorCode code{};
    SourceRange range;
    std::uint32_t line{};
    std::uint32_t column{};
    Utf8Text message_key;
};

struct ParseSnapshot final {
    ParseState state{ParseState::empty};
    Revision source_revision{};
    std::shared_ptr<const SemanticDocument> document;
    std::vector<ParseIssue> issues;
};

}  // namespace markdownmay
