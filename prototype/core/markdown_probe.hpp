#pragma once

#include <cstdint>
#include <string_view>

namespace markdownmay::prototype {

struct MarkdownStatistics final {
    std::uint32_t block_count{};
    std::uint32_t span_count{};
    std::uint32_t text_fragment_count{};
    std::uint32_t heading_count{};
    std::uint32_t table_count{};
    std::uint32_t task_item_count{};
};

[[nodiscard]] bool ProbeMarkdown(
    std::string_view markdown,
    MarkdownStatistics& statistics) noexcept;

}  // namespace markdownmay::prototype
