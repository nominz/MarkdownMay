#include "markdownmay/editor/block_interaction_controller.hpp"

#include <chrono>

int main() {
    using namespace markdownmay;
    document::DocumentSession session(
        "# Title\nparagraph\n\n- first\n- second\n\n> quote\n\n```cpp\ncode\n```\n\n| A | B |\n|---|---|\n| 1 | 2 |\n");
    auto snapshot = session.snapshot();
    auto projection = editor::BuildInlineProjection(*snapshot.semantic, snapshot.source);
    editor::BlockInteractionController controller;
    constexpr std::uint64_t document_id = 101;
    if (!controller.refresh(document_id, snapshot, projection) || controller.revision() != 1) return 1;
    if (controller.items().size() != 7) return 2;
    if (controller.items()[0].kind != document::NodeKind::heading ||
        controller.items()[0].heading_level != 1 ||
        controller.items()[1].kind != document::NodeKind::paragraph) return 3;

    const auto heading = controller.items()[0];
    const auto paragraph = controller.items()[1];
    if (!controller.set_visible_layout(snapshot.source_revision, {
            {heading.node_id, {0, 0, 40, 20}},
            {paragraph.node_id, {0, 20, 40, 40}}})) return 4;
    const auto hit = controller.hit_test(10, 5);
    if (!hit || hit->node_id != heading.node_id ||
        !controller.validate(*hit, document_id, snapshot)) return 5;
    if (controller.hit_test(50, 5) || controller.hit_test(10, 40)) return 6;
    if (controller.set_visible_layout(snapshot.source_revision + 1,
            {{heading.node_id, {0, 0, 40, 20}}})) return 7;

    const auto old_context = *hit;
    document::EditTransaction edit{1, snapshot.source_revision, document::EditOrigin::source_view,
        {{{paragraph.source_range.begin, paragraph.source_range.begin}, "x"}}};
    if (session.commit(edit) != ErrorCode::ok) return 8;
    const auto changed = session.snapshot();
    if (controller.validate(old_context, document_id, changed) ||
        controller.validate(old_context, document_id + 1, snapshot)) return 9;
    if (!controller.refresh(document_id, changed,
            editor::BuildInlineProjection(*changed.semantic, changed.source))) return 10;

    document::DocumentSession plain("# not markdown\n", document::DocumentKind::plain_text);
    if (controller.refresh(document_id, plain.snapshot(), {} ) || !controller.items().empty() ||
        controller.revision() != 0) return 11;

    std::string many;
    many.reserve(190000);
    for (int index = 0; index < 10000; ++index)
        many += "paragraph " + std::to_string(index) + "\n\n";
    document::DocumentSession large(std::move(many));
    const auto large_snapshot = large.snapshot();
    const auto large_projection = editor::BuildInlineProjection(
        *large_snapshot.semantic, large_snapshot.source);
    const auto started = std::chrono::steady_clock::now();
    if (!controller.refresh(document_id + 1, large_snapshot, large_projection) ||
        controller.items().size() != 10000) return 12;
    const auto elapsed = std::chrono::steady_clock::now() - started;
    if (elapsed > std::chrono::milliseconds(250)) return 13;
    return 0;
}
