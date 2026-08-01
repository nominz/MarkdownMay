#include "markdownmay/editor/list_editor.hpp"
#include "markdownmay/editor/rich_projection.hpp"

#include "markdownmay/markdown/markdown_parser.hpp"
#include "markdownmay/markdown/markdown_writer.hpp"

#include <functional>

namespace {
std::size_t Count(const markdownmay::document::Node& node,
                  markdownmay::document::NodeKind kind) {
    std::size_t result = node.kind == kind ? 1U : 0U;
    for (const auto& child : node.children) result += Count(*child, kind);
    return result;
}
}

int main() {
    using namespace markdownmay;
    {
        const std::string source = "- first\n- [x] done\n\n3. third\n4. fourth\n\n- parent\n    - child";
        const auto parsed = markdown::ParseMarkdown(source, 1);
        if (!parsed) return 13;
        const auto projection = editor::BuildInlineProjection(*parsed, source);
        if (projection.text.find("• first") == std::string::npos ||
            projection.text.find("☑ done") == std::string::npos ||
            projection.text.find("3. third") == std::string::npos ||
            projection.text.find("• child") == std::string::npos) return 14;
    }
    {
        document::DocumentSession session("one\ntwo");
        editor::ParagraphEditor editor(session);
        editor::ListEditor lists(session, editor);
        if (editor.set_selection({0, 7}) != ErrorCode::ok ||
            lists.toggle_unordered() != ErrorCode::ok ||
            session.snapshot().source != "- one\n- two") return 1;
        if (Count(*session.snapshot().semantic->root(), document::NodeKind::list_item) != 2) return 2;
        if (editor.set_selection({0, 11}) != ErrorCode::ok ||
            lists.toggle_unordered() != ErrorCode::ok ||
            session.snapshot().source != "one\ntwo") return 3;
        if (editor.undo() != ErrorCode::ok || session.snapshot().source != "- one\n- two") return 4;
    }
    {
        document::DocumentSession session("first\nsecond");
        editor::ParagraphEditor editor(session);
        editor::ListEditor lists(session, editor);
        if (editor.set_selection({0, 12}) != ErrorCode::ok ||
            lists.toggle_ordered(3) != ErrorCode::ok ||
            session.snapshot().source != "3. first\n4. second") return 5;
        const auto* attributes = std::get_if<document::ListAttributes>(
            &session.snapshot().semantic->root()->children.front()->attributes);
        if (!attributes || !attributes->ordered || attributes->start != 3) return 6;
        if (editor.set_selection({18, 18}) != ErrorCode::ok ||
            lists.continue_item() != ErrorCode::ok ||
            session.snapshot().source != "3. first\n4. second\n5. ") return 18;
    }
    {
        document::DocumentSession session("todo");
        editor::ParagraphEditor editor(session);
        editor::ListEditor lists(session, editor);
        if (editor.set_selection({0, 4}) != ErrorCode::ok ||
            lists.toggle_task() != ErrorCode::ok ||
            session.snapshot().source != "- [ ] todo") return 7;
        if (lists.toggle_checked() != ErrorCode::ok ||
            session.snapshot().source != "- [x] todo") return 8;
        if (editor.set_selection({10, 10}) != ErrorCode::ok ||
            lists.continue_item() != ErrorCode::ok ||
            session.snapshot().source != "- [x] todo\n- [ ] ") return 15;
        if (lists.continue_item() != ErrorCode::ok ||
            session.snapshot().source != "- [x] todo\n") return 16;
    }
    {
        document::DocumentSession session("- parent\n- child");
        editor::ParagraphEditor editor(session);
        editor::ListEditor lists(session, editor);
        if (editor.set_selection({9, 16}) != ErrorCode::ok || lists.indent() != ErrorCode::ok ||
            session.snapshot().source != "- parent\n    - child") return 9;
        if (Count(*session.snapshot().semantic->root(), document::NodeKind::list) != 2) return 10;
        const auto written = markdown::WriteMarkdown(*session.snapshot().semantic,
            {fileio::LineEnding::lf, true});
        if (written != "- parent\n    - child\n") return 17;
        const auto roundtrip = markdown::ParseMarkdown(written, 99);
        if (!roundtrip || Count(*roundtrip->root(), document::NodeKind::list) != 2 ||
            Count(*roundtrip->root(), document::NodeKind::list_item) != 2) return 12;
        if (lists.outdent() != ErrorCode::ok || session.snapshot().source != "- parent\n- child") return 11;
    }
    return 0;
}
