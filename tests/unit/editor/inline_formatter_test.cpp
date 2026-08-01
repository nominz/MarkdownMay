#include "markdownmay/editor/inline_formatter.hpp"
#include "markdownmay/editor/rich_projection.hpp"

#include "markdownmay/markdown/markdown_parser.hpp"
#include "markdownmay/markdown/markdown_writer.hpp"

#include <functional>

int main() {
    using namespace markdownmay;
    using document::NodeKind;
    document::DocumentSession session("bold italic strike code link");
    editor::ParagraphEditor editor(session);
    editor::InlineFormatter formatter(session, editor);

    if (editor.set_selection({0, 4}) != ErrorCode::ok ||
        formatter.toggle(editor::InlineFormat::bold) != ErrorCode::ok) return 1;
    if (editor.set_selection({9, 15}) != ErrorCode::ok ||
        formatter.toggle(editor::InlineFormat::italic) != ErrorCode::ok) return 2;
    if (editor.set_selection({18, 24}) != ErrorCode::ok ||
        formatter.toggle(editor::InlineFormat::strike) != ErrorCode::ok) return 3;
    if (editor.set_selection({29, 33}) != ErrorCode::ok ||
        formatter.toggle(editor::InlineFormat::code) != ErrorCode::ok) return 4;
    if (editor.set_selection({36, 40}) != ErrorCode::ok ||
        formatter.set_link("local.md", "本地") != ErrorCode::ok) return 5;

    const auto snapshot = session.snapshot();
    const auto parsed = markdown::ParseMarkdown(snapshot.source, snapshot.source_revision);
    if (!parsed) return 6;
    int strong = 0, emphasis = 0, strike = 0, code = 0, link = 0;
    std::function<void(const document::Node&)> visit = [&](const document::Node& node) {
        strong += node.kind == NodeKind::strong;
        emphasis += node.kind == NodeKind::emphasis;
        strike += node.kind == NodeKind::strike;
        code += node.kind == NodeKind::inline_code;
        link += node.kind == NodeKind::link;
        for (const auto& child : node.children) visit(*child);
    };
    visit(*parsed->root());
    if (strong != 1 || emphasis != 1 || strike != 1 || code != 1 || link != 1) return 7;
    const auto written = markdown::WriteMarkdown(
        *parsed, {fileio::LineEnding::lf, true});
    const auto roundtrip = markdown::ParseMarkdown(written, snapshot.source_revision + 1);
    if (!roundtrip) return 8;
    int nodes = 0;
    std::function<void(const document::Node&)> count = [&](const document::Node& node) {
        if (node.kind == NodeKind::strong || node.kind == NodeKind::emphasis ||
            node.kind == NodeKind::strike || node.kind == NodeKind::inline_code ||
            node.kind == NodeKind::link) ++nodes;
        for (const auto& child : node.children) count(*child);
    };
    count(*roundtrip->root());
    if (nodes != 5) return 9;
    const auto projection = editor::BuildInlineProjection(*parsed, snapshot.source);
    if (projection.text != "bold italic strike code link" ||
        projection.spans.size() != 5 ||
        projection.source_offsets.size() != projection.text.size() + 1) return 10;
    if (editor.undo() != ErrorCode::ok || editor.redo() != ErrorCode::ok) return 11;
    return 0;
}
