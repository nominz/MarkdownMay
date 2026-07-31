#include "markdown_probe.hpp"

int main() {
    constexpr auto markdown =
        "# 标题\n\n"
        "- [x] 已完成\n"
        "- [ ] 未完成\n\n"
        "| 名称 | 金额 |\n"
        "| --- | ---: |\n"
        "| 现金 | 100 |\n";

    markdownmay::prototype::MarkdownStatistics statistics;
    if (!markdownmay::prototype::ProbeMarkdown(markdown, statistics)) {
        return 1;
    }
    if (statistics.heading_count != 1) {
        return 2;
    }
    if (statistics.table_count != 1) {
        return 3;
    }
    if (statistics.task_item_count != 2) {
        return 4;
    }
    return 0;
}
