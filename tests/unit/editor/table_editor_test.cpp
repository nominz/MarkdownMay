#include "markdownmay/editor/table_editor.hpp"
#include "markdownmay/editor/rich_projection.hpp"
#include "markdownmay/markdown/markdown_writer.hpp"

#include <algorithm>

namespace {
const markdownmay::document::Node* Table(const markdownmay::document::Node& node) {
    if (node.kind == markdownmay::document::NodeKind::table) return &node;
    for (const auto& child : node.children) if (const auto* found = Table(*child)) return found;
    return nullptr;
}
markdownmay::editor::TablePosition LastCell(const markdownmay::document::Node& table) {
    std::size_t rows{}, columns{};
    for (const auto& section : table.children) for (const auto& row : section->children) {
        if (row->kind != markdownmay::document::NodeKind::table_row) continue;
        ++rows; columns = (std::max)(columns, row->children.size());
    }
    return {rows - 1, columns - 1};
}
}

int main() {
    using namespace markdownmay;
    document::DocumentSession session("");
    editor::ParagraphEditor paragraphs(session);
    editor::TableEditor tables(session, paragraphs);
    if (tables.insert(2, 2) != ErrorCode::ok ||
        session.snapshot().source != "| 列1 | 列2 |\n| --- | --- |\n|  |  |") return 1;
    auto* table = Table(*session.snapshot().semantic->root());
    if (!table) return 20;
    const auto set_result = tables.set_cell(table->id, {1, 0}, "甲|乙");
    if (set_result != ErrorCode::ok)
        return set_result == ErrorCode::editor_unmapped_rich_edit_change ? 23 :
               set_result == ErrorCode::document_invalid_state ? 24 :
               set_result == ErrorCode::editor_selection_mapping_failed ? 25 : 26;
    if (session.snapshot().source.find("甲\\|乙") == std::string::npos) return 22;
    table = Table(*session.snapshot().semantic->root());
    if (!table || tables.paste_tsv(table->id, {1, 1}, "B\tC\r\nD\tE") != ErrorCode::ok ||
        session.snapshot().source.find("| 甲\\|乙 | B | C |") == std::string::npos ||
        session.snapshot().source.find("|  | D | E |") == std::string::npos) return 3;
    table = Table(*session.snapshot().semantic->root());
    if (!table || tables.insert_row(table->id, 1) != ErrorCode::ok) return 4;
    table = Table(*session.snapshot().semantic->root());
    if (!table || tables.delete_row(table->id, 1) != ErrorCode::ok) return 5;
    table = Table(*session.snapshot().semantic->root());
    if (!table || tables.insert_column(table->id, 1) != ErrorCode::ok) return 6;
    table = Table(*session.snapshot().semantic->root());
    if (!table || tables.delete_column(table->id, 1) != ErrorCode::ok) return 7;
    table = Table(*session.snapshot().semantic->root());
    const auto last = table ? LastCell(*table) : editor::TablePosition{};
    auto previous = table ? tables.navigate(table->id, {1, 0}, false)
                          : Result<editor::TablePosition>::failure(ErrorCode::document_invalid_state);
    if (!previous.is_ok() || previous.value().row != 0 ||
        previous.value().column != last.column) return 86;
    auto moved = table ? tables.navigate(table->id, last, true)
                       : Result<editor::TablePosition>::failure(ErrorCode::document_invalid_state);
    if (!moved.is_ok())
        return moved.error() == ErrorCode::editor_selection_mapping_failed ? 82 :
               moved.error() == ErrorCode::editor_unmapped_rich_edit_change ? 83 :
               moved.error() == ErrorCode::document_invalid_state ? 84 : 85;
    if (moved.value().row != last.row + 1 || moved.value().column != 0) return 81;
    table = Table(*session.snapshot().semantic->root());
    const auto projection = editor::BuildInlineProjection(
        *session.snapshot().semantic, session.snapshot().source);
    if (!table || projection.text.find("---") != std::string::npos ||
        projection.text.find('\t') == std::string::npos ||
        projection.text.find("甲|乙") == std::string::npos) return 9;
    std::size_t cells{};
    for (const auto& span : projection.spans)
        if (span.kind == document::NodeKind::table_cell) ++cells;
    if (cells != (last.row + 2) * (last.column + 1)) return 10;
    const auto written = markdown::WriteMarkdown(*session.snapshot().semantic,
        {fileio::LineEnding::lf, true});
    if (written.find("| --- |") == std::string::npos) return 11;
    if (tables.remove(table->id) != ErrorCode::ok || !session.snapshot().source.empty()) return 12;
    if (paragraphs.undo() != ErrorCode::ok || !Table(*session.snapshot().semantic->root())) return 13;
    document::DocumentSession inline_session("前文后文");
    editor::ParagraphEditor inline_paragraphs(inline_session);
    editor::TableEditor inline_tables(inline_session, inline_paragraphs);
    if (inline_paragraphs.set_selection({6, 6}) != ErrorCode::ok ||
        inline_tables.insert(2, 2) != ErrorCode::ok ||
        inline_session.snapshot().source.find("前文\n\n| 列1 |") == std::string::npos ||
        !Table(*inline_session.snapshot().semantic->root())) return 14;
    return 0;
}
