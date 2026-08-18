#include "markdownmay/editor/heading_fold_controller.hpp"

#include <chrono>

int main() {
    using namespace markdownmay;
    document::DocumentSession session(
        "# A\nintro\n## B\nbody\n### C\ndeep\n## D\ntail\n# E\nend\n");
    editor::HeadingFoldController folds(session);
    if (folds.items().size() != 5) return 1;
    const auto first_id = folds.items()[0].node_id;
    const auto second_id = folds.items()[1].node_id;
    if (session.snapshot().source.substr(
            static_cast<std::size_t>(folds.items()[0].body_range.end), 3) != "# E") return 2;
    if (session.snapshot().source.substr(
            static_cast<std::size_t>(folds.items()[1].body_range.end), 4) != "## D") return 3;
    if (!folds.toggle(second_id) || !folds.toggle(first_id)) return 4;
    if (!folds.items()[0].collapsed || !folds.items()[1].collapsed) return 5;
    if (!folds.toggle(first_id) || folds.items()[0].collapsed || !folds.items()[1].collapsed) return 6;
    if (!folds.expand_to_reveal(folds.items()[1].body_range.begin + 1) || folds.items()[1].collapsed) return 7;
    const auto before = session.snapshot();
    if (!folds.toggle(first_id)) return 8;
    const auto after = session.snapshot();
    if (before.source != after.source || before.source_revision != after.source_revision || session.is_dirty()) return 9;
    static_cast<void>(session.change_kind(document::DocumentKind::plain_text));
    if (!folds.items().empty() || folds.revision() != 0) return 10;

    document::DocumentSession setext("Title\n=====\nbody\n\nEmpty\n-----\n# Last\n");
    editor::HeadingFoldController setext_folds(setext);
    if (setext_folds.items().size() != 3 || setext_folds.items()[0].level != 1 ||
        setext_folds.items()[1].level != 2) return 11;

    std::string many;
    many.reserve(160000);
    for (int index = 0; index < 10000; ++index)
        many += "###### H" + std::to_string(index) + "\nvalue\n";
    document::DocumentSession large(std::move(many));
    editor::HeadingFoldController large_folds(large);
    if (large_folds.items().size() != 10000) return 12;
    const auto started = std::chrono::steady_clock::now();
    if (!large_folds.toggle(large_folds.items()[5000].node_id)) return 13;
    const auto elapsed = std::chrono::steady_clock::now() - started;
    if (elapsed > std::chrono::milliseconds(100)) return 14;
    return 0;
}
