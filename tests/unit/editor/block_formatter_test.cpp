#include "markdownmay/editor/block_formatter.hpp"
#include "markdownmay/editor/rich_projection.hpp"

#include "markdownmay/markdown/markdown_parser.hpp"
#include "markdownmay/markdown/markdown_writer.hpp"

#include <functional>

namespace {
bool Contains(const markdownmay::document::Node& node,
              markdownmay::document::NodeKind kind) {
    if (node.kind == kind) return true;
    for (const auto& child : node.children) if (Contains(*child, kind)) return true;
    return false;
}
}

int main() {
    using namespace markdownmay;
    {
        const std::string source = "# 标题\n\n> 引用\n\n```cpp\n代码\n```\n\n---\n";
        const auto parsed = markdown::ParseMarkdown(source, 1);
        if (!parsed) return 13;
        const auto projection = editor::BuildInlineProjection(*parsed, source);
        const auto visible_code = projection.text.find("代码");
        const auto source_code = source.find("代码");
        if (visible_code == std::string::npos || source_code == std::string::npos ||
            projection.source_offsets[visible_code + 3] != source_code + 3) return 14;
        const auto written = markdown::WriteMarkdown(
            *parsed, {fileio::LineEnding::lf, true});
        const auto roundtrip = markdown::ParseMarkdown(written, 2);
        if (!roundtrip ||
            !Contains(*roundtrip->root(), document::NodeKind::heading) ||
            !Contains(*roundtrip->root(), document::NodeKind::quote) ||
            !Contains(*roundtrip->root(), document::NodeKind::code_block) ||
            !Contains(*roundtrip->root(), document::NodeKind::thematic_break)) return 15;
    }
    {
        document::DocumentSession session("标题");
        editor::ParagraphEditor editor(session);
        editor::BlockFormatter blocks(session, editor);
        if (editor.set_selection({0, 6}) != ErrorCode::ok ||
            blocks.set_heading(2) != ErrorCode::ok || session.snapshot().source != "## 标题") return 1;
        if (!Contains(*session.snapshot().semantic->root(), document::NodeKind::heading)) return 2;
        if (blocks.set_heading(0) != ErrorCode::ok || session.snapshot().source != "标题") return 3;
        if (editor.undo() != ErrorCode::ok || session.snapshot().source != "## 标题") return 4;
    }
    {
        document::DocumentSession session("甲\n乙");
        editor::ParagraphEditor editor(session);
        editor::BlockFormatter blocks(session, editor);
        if (editor.set_selection({0, 7}) != ErrorCode::ok || blocks.toggle_quote() != ErrorCode::ok ||
            session.snapshot().source != "> 甲\n> 乙") return 5;
        if (!Contains(*session.snapshot().semantic->root(), document::NodeKind::quote)) return 6;
        if (editor.set_selection({0, 11}) != ErrorCode::ok || blocks.toggle_quote() != ErrorCode::ok ||
            session.snapshot().source != "甲\n乙") return 7;
    }
    {
        document::DocumentSession session("int x;");
        editor::ParagraphEditor editor(session);
        editor::BlockFormatter blocks(session, editor);
        if (editor.set_selection({0, 6}) != ErrorCode::ok ||
            blocks.toggle_code_block("cpp") != ErrorCode::ok ||
            session.snapshot().source != "```cpp\r\nint x;\r\n```") return 8;
        if (!Contains(*session.snapshot().semantic->root(), document::NodeKind::code_block)) return 9;
        if (blocks.toggle_code_block() != ErrorCode::ok || session.snapshot().source != "int x;") return 10;
    }
    {
        document::DocumentSession session("");
        editor::ParagraphEditor editor(session);
        editor::BlockFormatter blocks(session, editor);
        if (blocks.insert_thematic_break() != ErrorCode::ok ||
            !Contains(*session.snapshot().semantic->root(), document::NodeKind::thematic_break)) return 11;
        const auto written = markdown::WriteMarkdown(*session.snapshot().semantic,
            {fileio::LineEnding::crlf, true});
        if (!markdown::ParseMarkdown(written, 99)) return 12;
    }
    return 0;
}
