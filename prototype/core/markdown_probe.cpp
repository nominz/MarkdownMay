#include "markdown_probe.hpp"

#include <md4c.h>

#include <limits>

namespace markdownmay::prototype {
namespace {

int EnterBlock(
    MD_BLOCKTYPE type,
    void* detail,
    void* user_data) noexcept {
    auto& statistics = *static_cast<MarkdownStatistics*>(user_data);
    ++statistics.block_count;

    if (type == MD_BLOCK_H) {
        ++statistics.heading_count;
    } else if (type == MD_BLOCK_TABLE) {
        ++statistics.table_count;
    } else if (type == MD_BLOCK_LI && detail != nullptr) {
        const auto* list_item = static_cast<const MD_BLOCK_LI_DETAIL*>(detail);
        if (list_item->is_task != 0) {
            ++statistics.task_item_count;
        }
    }
    return 0;
}

int LeaveBlock(
    MD_BLOCKTYPE type,
    void* detail,
    void* user_data) noexcept {
    (void)type;
    (void)detail;
    (void)user_data;
    return 0;
}

int EnterSpan(
    MD_SPANTYPE type,
    void* detail,
    void* user_data) noexcept {
    (void)type;
    (void)detail;
    auto& statistics = *static_cast<MarkdownStatistics*>(user_data);
    ++statistics.span_count;
    return 0;
}

int LeaveSpan(
    MD_SPANTYPE type,
    void* detail,
    void* user_data) noexcept {
    (void)type;
    (void)detail;
    (void)user_data;
    return 0;
}

int Text(
    MD_TEXTTYPE type,
    const MD_CHAR* text,
    MD_SIZE size,
    void* user_data) noexcept {
    (void)type;
    (void)text;
    (void)size;
    auto& statistics = *static_cast<MarkdownStatistics*>(user_data);
    ++statistics.text_fragment_count;
    return 0;
}

}  // namespace

bool ProbeMarkdown(
    std::string_view markdown,
    MarkdownStatistics& statistics) noexcept {
    statistics = {};
    if (markdown.size() >
        static_cast<std::size_t>(std::numeric_limits<MD_SIZE>::max())) {
        return false;
    }

    MD_PARSER parser{};
    parser.abi_version = 0;
    parser.flags =
        MD_DIALECT_GITHUB |
        MD_FLAG_TASKLISTS |
        MD_FLAG_TABLES;
    parser.enter_block = EnterBlock;
    parser.leave_block = LeaveBlock;
    parser.enter_span = EnterSpan;
    parser.leave_span = LeaveSpan;
    parser.text = Text;

    return md_parse(
               markdown.data(),
               static_cast<MD_SIZE>(markdown.size()),
               &parser,
               &statistics) == 0;
}

}  // namespace markdownmay::prototype
