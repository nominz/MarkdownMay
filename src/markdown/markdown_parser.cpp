#include "markdownmay/markdown/markdown_parser.hpp"

#include <md4c.h>
extern "C" {
#include <entity.h>
}

#include <limits>
#include <cstdint>
#include <utility>

namespace markdownmay::markdown {
namespace {
using namespace document;

void AppendUtf8(std::string& output, unsigned codepoint) {
    if (codepoint == 0 || codepoint > 0x10ffff ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff)) codepoint = 0xfffd;
    if (codepoint <= 0x7f) output.push_back(static_cast<char>(codepoint));
    else if (codepoint <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
        output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

std::string DecodeEntity(const char* text, std::size_t size) {
    if (size < 3 || text[0] != '&' || text[size - 1] != ';') return {text, size};
    unsigned codepoint{};
    if (size > 3 && text[1] == '#') {
        const bool hexadecimal = text[2] == 'x' || text[2] == 'X';
        const auto begin = hexadecimal ? 3U : 2U;
        for (auto index = begin; index + 1 < size; ++index) {
            const auto value = text[index];
            unsigned digit{};
            if (value >= '0' && value <= '9') digit = static_cast<unsigned>(value - '0');
            else if (hexadecimal && value >= 'a' && value <= 'f') digit = 10U + value - 'a';
            else if (hexadecimal && value >= 'A' && value <= 'F') digit = 10U + value - 'A';
            else return {text, size};
            codepoint = codepoint * (hexadecimal ? 16U : 10U) + digit;
            if (codepoint > 0x10ffff) { codepoint = 0xfffd; break; }
        }
        std::string output;
        AppendUtf8(output, codepoint);
        return output;
    }
    const auto* entity = entity_lookup(text, size);
    if (!entity) return {text, size};
    std::string output;
    AppendUtf8(output, entity->codepoints[0]);
    if (entity->codepoints[1]) AppendUtf8(output, entity->codepoints[1]);
    return output;
}

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
        const auto source_address = reinterpret_cast<std::uintptr_t>(begin);
        const auto text_address = reinterpret_cast<std::uintptr_t>(text);
        if (text_address < source_address || text_address - source_address > size ||
            length > size || text_address - source_address + length > size) {
            const std::string_view source(begin, size);
            const std::string_view fragment(text, length);
            const auto search_from = node.source.begin <= node.source.end
                ? static_cast<std::size_t>(node.source.end) : 0U;
            auto found = source.find(fragment, search_from);
            if (found == std::string_view::npos && node.source.begin > node.source.end)
                found = source.find(fragment);
            if (found != std::string_view::npos) {
                node.source.begin = (std::min)(node.source.begin,
                    static_cast<std::uint64_t>(found));
                node.source.end = (std::max)(node.source.end,
                    static_cast<std::uint64_t>(found + length));
            }
            return;
        }
        const auto offset = static_cast<std::uint64_t>(text_address - source_address);
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
        if (type == MD_TEXT_BR || type == MD_TEXT_SOFTBR) child->text = "\n";
        else if (type == MD_TEXT_NULLCHAR) child->text = "\xEF\xBF\xBD";
        else if (type == MD_TEXT_ENTITY) child->text = DecodeEntity(text, size);
        else child->text.assign(text, size);
        const auto source_address = reinterpret_cast<std::uintptr_t>(builder.begin);
        const auto text_address = reinterpret_cast<std::uintptr_t>(text);
        const bool belongs_to_source = text_address >= source_address &&
            text_address - source_address <= builder.size &&
            size <= builder.size && text_address - source_address + size <= builder.size;
        const auto offset = belongs_to_source
            ? static_cast<std::uint64_t>(text_address - source_address)
            : parent->source.end;
        child->source = {offset, belongs_to_source ? offset + size : offset};
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

bool FindNormalizedFragment(std::string_view source, std::string_view fragment,
                            std::size_t from, SourceRange& range) {
    for (auto candidate = from; candidate < source.size(); ++candidate) {
        std::size_t source_at = candidate;
        std::size_t fragment_at{};
        while (source_at < source.size() && fragment_at < fragment.size()) {
            if (fragment[fragment_at] == '\n') {
                if (source[source_at] == '\r' && source_at + 1 < source.size() &&
                    source[source_at + 1] == '\n') source_at += 2;
                else if (source[source_at] == '\n') ++source_at;
                else break;
                ++fragment_at;
            } else if (source[source_at] == fragment[fragment_at]) {
                ++source_at;
                ++fragment_at;
            } else break;
        }
        if (fragment_at == fragment.size()) {
            range = {static_cast<std::uint64_t>(candidate),
                     static_cast<std::uint64_t>(source_at)};
            return true;
        }
    }
    return false;
}

bool FindThematicBreak(std::string_view source, std::size_t from, SourceRange& range) {
    auto line_begin = from;
    while (line_begin < source.size()) {
        auto line_end = source.find('\n', line_begin);
        if (line_end == std::string_view::npos) line_end = source.size();
        auto content_end = line_end;
        if (content_end > line_begin && source[content_end - 1] == '\r') --content_end;
        auto at = line_begin;
        std::size_t leading{};
        while (at < content_end && source[at] == ' ' && leading < 4) { ++at; ++leading; }
        char marker{};
        std::size_t count{};
        bool valid = leading <= 3;
        for (; valid && at < content_end; ++at) {
            const auto value = source[at];
            if (value == ' ' || value == '\t') continue;
            if (marker == 0 && (value == '-' || value == '*' || value == '_')) marker = value;
            else if (value != marker) valid = false;
            ++count;
        }
        if (valid && marker != 0 && count >= 3) {
            range = {static_cast<std::uint64_t>(line_begin),
                     static_cast<std::uint64_t>(content_end)};
            return true;
        }
        line_begin = line_end < source.size() ? line_end + 1 : source.size();
    }
    return false;
}

void RepairRawBlockRanges(MutableNode& node, std::string_view source,
                          std::uint64_t& cursor) {
    if (node.kind == NodeKind::thematic_break) {
        SourceRange found;
        if (FindThematicBreak(source, static_cast<std::size_t>(cursor), found)) {
            node.source = found;
            cursor = node.source.end;
        }
        return;
    }
    if (node.kind == NodeKind::unknown_block && !node.text.empty()) {
        SourceRange found;
        if (FindNormalizedFragment(source, node.text, static_cast<std::size_t>(cursor), found) ||
            FindNormalizedFragment(source, node.text, 0, found)) {
            node.source = found;
            cursor = node.source.end;
        }
        return;
    }
    for (auto& child : node.children) RepairRawBlockRanges(*child, source, cursor);
    cursor = (std::max)(cursor, node.source.end);
}

}  // namespace

std::shared_ptr<const Document> ParseMarkdown(std::string_view source, std::uint64_t revision) {
    if (source.size() > static_cast<std::size_t>((std::numeric_limits<MD_SIZE>::max)())) return {};
    Builder builder{source.data(), source.size()};
    MD_PARSER parser{}; parser.abi_version = 0; parser.flags = MD_DIALECT_GITHUB;
    parser.enter_block = EnterBlock; parser.leave_block = LeaveBlock;
    parser.enter_span = EnterSpan; parser.leave_span = LeaveSpan; parser.text = Text;
    if (md_parse(source.data(), static_cast<MD_SIZE>(source.size()), &parser, &builder) != 0 || !builder.root) return {};
    std::uint64_t raw_cursor{};
    RepairRawBlockRanges(*builder.root, source, raw_cursor);
    builder.root->source = {0, static_cast<std::uint64_t>(source.size())};
    auto document = std::make_shared<Document>(Freeze(*builder.root), revision);
    return document->validate(source.size()) ? document : nullptr;
}

}  // namespace markdownmay::markdown
