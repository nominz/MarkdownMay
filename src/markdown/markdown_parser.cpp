#include "markdownmay/markdown/markdown_parser.hpp"

#include <md4c.h>

#include <limits>
#include <utility>

namespace markdownmay::markdown {
namespace {
using namespace document;

struct MutableNode {
    NodeId id{};
    NodeKind kind{};
    SourceRange source;
    NodeAttributes attributes;
    std::string text;
    std::vector<std::unique_ptr<MutableNode>> children;
};

NodeKind BlockKind(MD_BLOCKTYPE type) {
    switch (type) {
        case MD_BLOCK_DOC: return NodeKind::document;
        case MD_BLOCK_QUOTE: return NodeKind::quote;
        case MD_BLOCK_UL: case MD_BLOCK_OL: return NodeKind::list;
        case MD_BLOCK_LI: return NodeKind::list_item;
        case MD_BLOCK_HR: return NodeKind::thematic_break;
        case MD_BLOCK_H: return NodeKind::heading;
        case MD_BLOCK_CODE: return NodeKind::code_block;
        case MD_BLOCK_HTML: return NodeKind::unknown_block;
        case MD_BLOCK_P: return NodeKind::paragraph;
        case MD_BLOCK_TABLE: return NodeKind::table;
        case MD_BLOCK_THEAD: return NodeKind::table_head;
        case MD_BLOCK_TBODY: return NodeKind::table_body;
        case MD_BLOCK_TR: return NodeKind::table_row;
        case MD_BLOCK_TH: case MD_BLOCK_TD: return NodeKind::table_cell;
    }
    return NodeKind::unknown_block;
}

NodeKind SpanKind(MD_SPANTYPE type) {
    switch (type) {
        case MD_SPAN_EM: return NodeKind::emphasis;
        case MD_SPAN_STRONG: return NodeKind::strong;
        case MD_SPAN_A: return NodeKind::link;
        case MD_SPAN_IMG: return NodeKind::image;
        case MD_SPAN_CODE: return NodeKind::inline_code;
        case MD_SPAN_DEL: return NodeKind::strike;
        default: return NodeKind::text;
    }
}

std::string AttributeText(const MD_ATTRIBUTE& attribute) {
    return {attribute.text, attribute.size};
}

struct Builder {
    const char* begin{};
    std::size_t size{};
    std::uint64_t next_id{1};
    std::unique_ptr<MutableNode> root;
    std::vector<MutableNode*> stack;

    NodeId NewId(NodeKind kind) {
        return (static_cast<NodeId>(kind) << 56U) | next_id++;
    }
    MutableNode* Push(NodeKind kind, NodeAttributes attributes = {}) {
        auto node = std::make_unique<MutableNode>();
        node->id = NewId(kind); node->kind = kind;
        node->source = {static_cast<std::uint64_t>(size), 0};
        auto* pointer = node.get();
        if (stack.empty()) root = std::move(node);
        else stack.back()->children.push_back(std::move(node));
        pointer->attributes = std::move(attributes);
        stack.push_back(pointer);
        return pointer;
    }
    void Touch(MutableNode& node, const char* text, std::size_t length) {
        if (text < begin || text > begin + size || length > size ||
            text + length > begin + size) return;
        const auto offset = static_cast<std::uint64_t>(text - begin);
        node.source.begin = (std::min)(node.source.begin, offset);
        node.source.end = (std::max)(node.source.end, offset + length);
    }
    void Pop() {
        auto* node = stack.back(); stack.pop_back();
        if (node->source.begin > node->source.end) node->source = {0, 0};
        if (!stack.empty() && node->source.end > node->source.begin) {
            stack.back()->source.begin = (std::min)(stack.back()->source.begin, node->source.begin);
            stack.back()->source.end = (std::max)(stack.back()->source.end, node->source.end);
        }
    }
};

int EnterBlock(MD_BLOCKTYPE type, void* detail, void* data) {
    auto& builder = *static_cast<Builder*>(data);
    NodeAttributes attributes;
    if (type == MD_BLOCK_H) {
        attributes = HeadingAttributes{static_cast<std::uint8_t>(
            static_cast<MD_BLOCK_H_DETAIL*>(detail)->level)};
    } else if (type == MD_BLOCK_UL) {
        const auto* value = static_cast<MD_BLOCK_UL_DETAIL*>(detail);
        attributes = ListAttributes{false, 1, value->is_tight != 0};
    } else if (type == MD_BLOCK_OL) {
        const auto* value = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
        attributes = ListAttributes{true, value->start, value->is_tight != 0};
    } else if (type == MD_BLOCK_LI) {
        const auto* value = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
        attributes = ListItemAttributes{value->is_task != 0,
            value->is_task != 0 && (value->task_mark == 'x' || value->task_mark == 'X')};
    } else if (type == MD_BLOCK_CODE) {
        const auto* value = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
        attributes = CodeAttributes{AttributeText(value->lang)};
    }
    builder.Push(BlockKind(type), std::move(attributes));
    return 0;
}
int LeaveBlock(MD_BLOCKTYPE, void*, void* data) {
    static_cast<Builder*>(data)->Pop(); return 0;
}
int EnterSpan(MD_SPANTYPE type, void* detail, void* data) {
    NodeAttributes attributes;
    if (type == MD_SPAN_A) {
        const auto* value = static_cast<MD_SPAN_A_DETAIL*>(detail);
        attributes = LinkAttributes{AttributeText(value->href), AttributeText(value->title)};
    } else if (type == MD_SPAN_IMG) {
        const auto* value = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
        attributes = LinkAttributes{AttributeText(value->src), AttributeText(value->title)};
    }
    static_cast<Builder*>(data)->Push(SpanKind(type), std::move(attributes));
    return 0;
}
int LeaveSpan(MD_SPANTYPE, void*, void* data) {
    static_cast<Builder*>(data)->Pop(); return 0;
}
int Text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* data) {
    auto& builder = *static_cast<Builder*>(data);
    if (builder.stack.empty()) return 1;
    auto* parent = builder.stack.back();
    builder.Touch(*parent, text, size);
    if (parent->kind == NodeKind::unknown_block || parent->kind == NodeKind::code_block ||
        parent->kind == NodeKind::inline_code) {
        parent->text.append(text, size);
    } else {
        auto child = std::make_unique<MutableNode>();
        child->id = builder.NewId(NodeKind::text); child->kind = NodeKind::text;
        child->text.assign(text, size);
        child->source = {static_cast<std::uint64_t>(text - builder.begin),
                         static_cast<std::uint64_t>(text - builder.begin + size)};
        if (type == MD_TEXT_BR || type == MD_TEXT_SOFTBR) child->text = "\n";
        parent->children.push_back(std::move(child));
    }
    return 0;
}

std::shared_ptr<const Node> Freeze(const MutableNode& source) {
    auto node = std::make_shared<Node>();
    node->id = source.id; node->kind = source.kind; node->source = source.source;
    node->attributes = source.attributes; node->text = source.text;
    for (const auto& child : source.children) node->children.push_back(Freeze(*child));
    return node;
}

}  // namespace

std::shared_ptr<const Document> ParseMarkdown(std::string_view source, std::uint64_t revision) {
    if (source.size() > static_cast<std::size_t>((std::numeric_limits<MD_SIZE>::max)())) return {};
    Builder builder{source.data(), source.size()};
    MD_PARSER parser{}; parser.abi_version = 0; parser.flags = MD_DIALECT_GITHUB;
    parser.enter_block = EnterBlock; parser.leave_block = LeaveBlock;
    parser.enter_span = EnterSpan; parser.leave_span = LeaveSpan; parser.text = Text;
    if (md_parse(source.data(), static_cast<MD_SIZE>(source.size()), &parser, &builder) != 0 || !builder.root) return {};
    builder.root->source = {0, static_cast<std::uint64_t>(source.size())};
    auto document = std::make_shared<Document>(Freeze(*builder.root), revision);
    return document->validate(source.size()) ? document : nullptr;
}

}  // namespace markdownmay::markdown
