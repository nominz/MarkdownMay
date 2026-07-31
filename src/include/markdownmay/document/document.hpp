#pragma once

#include "markdownmay/core/result.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace markdownmay::document {

using NodeId = std::uint64_t;

struct SourceRange final {
    std::uint64_t begin{};
    std::uint64_t end{};
};

enum class NodeKind : std::uint8_t {
    document, paragraph, heading, quote, list, list_item, code_block,
    table, table_head, table_body, table_row, table_cell, thematic_break,
    unknown_block, text, emphasis, strong, strike, inline_code, link, image
};

struct HeadingAttributes final { std::uint8_t level{1}; };
struct ListAttributes final { bool ordered{}; std::uint32_t start{1}; bool tight{}; };
struct ListItemAttributes final { bool task{}; bool checked{}; };
struct LinkAttributes final { std::string target; std::string title; };
struct CodeAttributes final { std::string language; };
using NodeAttributes = std::variant<std::monostate, HeadingAttributes,
    ListAttributes, ListItemAttributes, LinkAttributes, CodeAttributes>;

struct Node final {
    NodeId id{};
    NodeKind kind{NodeKind::paragraph};
    SourceRange source;
    NodeAttributes attributes;
    std::string text;
    std::vector<std::shared_ptr<const Node>> children;
};

class Document final {
public:
    Document(std::shared_ptr<const Node> root, std::uint64_t revision);
    [[nodiscard]] const std::shared_ptr<const Node>& root() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] const Node* find(NodeId id) const noexcept;
    [[nodiscard]] bool validate(std::uint64_t source_size) const noexcept;
private:
    void Index(const std::shared_ptr<const Node>& node);
    std::shared_ptr<const Node> root_;
    std::uint64_t revision_{};
    std::unordered_map<NodeId, const Node*> index_;
    bool duplicate_id_{};
};

}  // namespace markdownmay::document
