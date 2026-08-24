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
const markdownmay::document::Node* Find(const markdownmay::document::Node& node,
              markdownmay::document::NodeKind kind) {
    if (node.kind == kind) return &node;
    for (const auto& child : node.children) if (const auto* found = Find(*child, kind)) return found;
    return nullptr;
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
        const std::string heading = "## 标题\n\n";
        document::DocumentSession session(heading + "第一段\n\n第二段");
        editor::ParagraphEditor editor(session);
        editor::BlockFormatter blocks(session, editor);
        if (editor.set_selection({heading.size(), heading.size() + 9}) != ErrorCode::ok ||
            blocks.toggle_code_block("python") != ErrorCode::ok ||
            session.snapshot().source != "## 标题\n\n```python\n第一段\n```\n\n第二段") return 18;
        const auto* code = Find(*session.snapshot().semantic->root(), document::NodeKind::code_block);
        const auto* attributes = code ? std::get_if<document::CodeAttributes>(&code->attributes) : nullptr;
        if (!attributes || attributes->language != "python" || code->text != "第一段\n") return 19;
    }
    {
        document::DocumentSession session("第一段\n\n第二段\n\n第三段");
        editor::ParagraphEditor editor(session);
        editor::BlockFormatter blocks(session, editor);
        if (editor.set_selection({0, 20}) != ErrorCode::ok ||
            blocks.toggle_code_block() != ErrorCode::ok ||
            session.snapshot().source != "```\n第一段\n\n第二段\n```\n\n第三段") return 20;
    }
    {
        const std::string source =
            "## Heading\n\n"
            "Paragraph A **bold text** tail.\n\n"
            "Paragraph B";
        document::DocumentSession session(source);
        editor::ParagraphEditor editor(session);
        editor::BlockFormatter blocks(session, editor);
        const auto projection = editor::BuildInlineProjection(
            *session.snapshot().semantic, session.snapshot().source);
        const auto visible_begin = projection.text.find("Paragraph A");
        const auto visible_end = projection.text.find(" tail.") + 6;
        const auto source_begin = source.find("Paragraph A");
        const auto source_end = source.find("\n\nParagraph B");
        if (visible_begin == std::string::npos || visible_end == std::string::npos ||
            projection.source_offsets[visible_begin] != source_begin ||
            projection.source_offsets[visible_end] != source_end) return 23;
        if (editor.set_selection({projection.source_offsets[visible_begin],
                projection.source_offsets[visible_end]}) != ErrorCode::ok ||
            blocks.toggle_code_block() != ErrorCode::ok ||
            session.snapshot().source !=
                "## Heading\n\n```\nParagraph A **bold text** tail.\n```\n\nParagraph B") return 24;
    }
    {
        const std::string source =
            "## Heading\n\nParagraph A\n\nParagraph B";
        const auto paragraph_a = source.find("Paragraph A");
        const auto paragraph_b = source.find("Paragraph B");
        {
            document::DocumentSession session(source);
            editor::ParagraphEditor editor(session);
            editor::BlockFormatter blocks(session, editor);
            if (editor.set_selection({paragraph_b + 11, paragraph_b}) != ErrorCode::ok ||
                blocks.toggle_code_block() != ErrorCode::ok ||
                session.snapshot().source !=
                    "## Heading\n\nParagraph A\n\n```\nParagraph B\n```") return 25;
        }
        {
            document::DocumentSession session(source);
            editor::ParagraphEditor editor(session);
            editor::BlockFormatter blocks(session, editor);
            if (editor.set_selection({paragraph_a, source.size()}) != ErrorCode::ok ||
                blocks.toggle_code_block() != ErrorCode::ok ||
                session.snapshot().source !=
                    "## Heading\n\n```\nParagraph A\n\nParagraph B\n```") return 26;
        }
    }
    {
        const std::string content = "**\n##\n[]\n#\n`\n\n    indented";
        document::DocumentSession session(content);
        editor::ParagraphEditor editor(session);
        editor::BlockFormatter blocks(session, editor);
        if (editor.set_selection({0, content.size()}) != ErrorCode::ok ||
            blocks.toggle_code_block("text") != ErrorCode::ok) return 21;
        const auto fenced = session.snapshot().source;
        document::DocumentSession reopened(fenced);
        const auto* code = Find(*reopened.snapshot().semantic->root(), document::NodeKind::code_block);
        const auto* attributes = code ? std::get_if<document::CodeAttributes>(&code->attributes) : nullptr;
        if (!attributes || attributes->language != "text" || code->text != content + "\n") return 22;
    }
    {
        document::DocumentSession session("");
        editor::ParagraphEditor editor(session);
        editor::BlockFormatter blocks(session, editor);
        if (blocks.set_heading(1) != ErrorCode::ok ||
            session.snapshot().source != "# " ||
            editor.selection().caret != 2 || editor.selection().anchor != 2) return 16;
        const auto projection = editor::BuildInlineProjection(
            *session.snapshot().semantic, session.snapshot().source);
        if (!projection.text.empty() || projection.source_offsets.size() != 1 ||
            projection.source_offsets.front() != 2) return 17;
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
