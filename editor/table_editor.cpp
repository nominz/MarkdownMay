#include "markdownmay/editor/table_editor.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace markdownmay::editor {
namespace {

std::uint64_t LineStart(std::string_view source, std::uint64_t position) {
    position = (std::min)(position, static_cast<std::uint64_t>(source.size()));
    while (position > 0 && source[static_cast<std::size_t>(position - 1)] != '\n') --position;
    return position;
}
std::uint64_t LineEnd(std::string_view source, std::uint64_t position) {
    position = (std::min)(position, static_cast<std::uint64_t>(source.size()));
    while (position < source.size() && source[static_cast<std::size_t>(position)] != '\r' &&
           source[static_cast<std::size_t>(position)] != '\n') ++position;
    return position;
}
std::string Trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
    return std::string(value);
}
std::vector<std::string> ParseRow(std::string_view line) {
    const auto trimmed = Trim(line);
    line = trimmed;
    if (!line.empty() && line.front() == '|') line.remove_prefix(1);
    if (!line.empty() && line.back() == '|') line.remove_suffix(1);
    std::vector<std::string> cells;
    std::string current;
    bool escaped = false;
    for (const auto value : line) {
        if (escaped) { current.push_back(value); escaped = false; }
        else if (value == '\\') escaped = true;
        else if (value == '|') { cells.push_back(Trim(current)); current.clear(); }
        else current.push_back(value);
    }
    if (escaped) current.push_back('\\');
    cells.push_back(Trim(current));
    return cells;
}
std::string NormalizeCell(std::string_view value) {
    std::string result;
    for (const auto character : value) {
        if (character == '\r' || character == '\n' || character == '\t') result.push_back(' ');
        else result.push_back(character);
    }
    return Trim(result);
}
std::string SerializeCell(std::string_view value) {
    std::string result;
    for (const auto character : NormalizeCell(value)) {
        if (character == '|') result.push_back('\\');
        result.push_back(character);
    }
    return result;
}
std::vector<std::vector<std::string>> ParseTsv(std::string_view text) {
    std::vector<std::vector<std::string>> rows(1);
    std::string cell;
    for (std::size_t index = 0; index <= text.size(); ++index) {
        const auto value = index < text.size() ? text[index] : '\n';
        if (value == '\t') { rows.back().push_back(NormalizeCell(cell)); cell.clear(); }
        else if (value == '\r' || value == '\n') {
            rows.back().push_back(NormalizeCell(cell)); cell.clear();
            if (value == '\r' && index + 1 < text.size() && text[index + 1] == '\n') ++index;
            if (index + 1 < text.size()) rows.emplace_back();
        } else cell.push_back(value);
    }
    return rows;
}

}  // namespace

TableEditor::TableEditor(document::DocumentSession& session, ParagraphEditor& editor)
    : session_(session), editor_(editor) {}

Result<TableEditor::TableData> TableEditor::Read(document::NodeId table) const {
    const auto snapshot = session_.snapshot();
    const auto* node = snapshot.semantic ? snapshot.semantic->find(table) : nullptr;
    if (!node || node->kind != document::NodeKind::table)
        return Result<TableData>::failure(ErrorCode::editor_selection_mapping_failed);
    TableData result;
    result.begin = LineStart(snapshot.source, node->source.begin);
    auto cursor = result.begin;
    while (cursor < snapshot.source.size()) {
        const auto line_end = LineEnd(snapshot.source, cursor);
        const auto line = std::string_view(snapshot.source).substr(
            static_cast<std::size_t>(cursor), static_cast<std::size_t>(line_end - cursor));
        if (line.find('|') == std::string_view::npos) break;
        result.end = line_end;
        cursor = line_end;
        if (cursor < snapshot.source.size() && snapshot.source[static_cast<std::size_t>(cursor)] == '\r') ++cursor;
        if (cursor < snapshot.source.size() && snapshot.source[static_cast<std::size_t>(cursor)] == '\n') ++cursor;
    }
    auto block = std::string_view(snapshot.source).substr(static_cast<std::size_t>(result.begin),
        static_cast<std::size_t>(result.end - result.begin));
    std::size_t line_index{};
    while (!block.empty()) {
        const auto newline = block.find('\n');
        auto line = block.substr(0, newline);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line_index != 1) result.rows.push_back(ParseRow(line));
        ++line_index;
        if (newline == std::string_view::npos) break;
        block.remove_prefix(newline + 1);
    }
    if (result.rows.empty() || result.rows.front().empty())
        return Result<TableData>::failure(ErrorCode::document_invariant_failed);
    const auto columns = result.rows.front().size();
    for (auto& row : result.rows) row.resize(columns);
    return Result<TableData>::success(std::move(result));
}

ErrorCode TableEditor::Write(const TableData& table) {
    if (table.rows.empty() || table.rows.front().empty()) return ErrorCode::document_invariant_failed;
    std::string markdown;
    const auto append = [&](const std::vector<std::string>& row, std::string& target) {
        target += "|";
        for (const auto& cell : row) target += " " + SerializeCell(cell) + " |";
        target += "\n";
    };
    append(table.rows.front(), markdown);
    markdown += "|";
    for (std::size_t column = 0; column < table.rows.front().size(); ++column) markdown += " --- |";
    markdown += "\n";
    for (std::size_t row = 1; row < table.rows.size(); ++row) append(table.rows[row], markdown);
    if (!markdown.empty()) markdown.pop_back();
    markdown = table.prefix + markdown + table.suffix;
    return editor_.replace_source_range(table.begin, table.end, std::move(markdown),
        {table.begin, table.begin});
}

ErrorCode TableEditor::insert(std::size_t rows, std::size_t columns) {
    if (rows == 0 || columns == 0 || rows > 1000 || columns > 100)
        return ErrorCode::document_invariant_failed;
    TableData table;
    const auto selection = editor_.selection();
    table.begin = (std::min)(selection.anchor, selection.caret);
    table.end = (std::max)(selection.anchor, selection.caret);
    const auto source = session_.snapshot().source;
    if (table.begin > 0 && source[static_cast<std::size_t>(table.begin - 1)] != '\n')
        table.prefix = "\n\n";
    if (table.end < source.size() && source[static_cast<std::size_t>(table.end)] != '\r' &&
        source[static_cast<std::size_t>(table.end)] != '\n') table.suffix = "\n\n";
    table.rows.assign(rows, std::vector<std::string>(columns));
    for (std::size_t column = 0; column < columns; ++column)
        table.rows[0][column] = "列" + std::to_string(column + 1);
    return Write(table);
}

ErrorCode TableEditor::set_cell(document::NodeId table, TablePosition cell, std::string_view text) {
    auto data = Read(table); if (!data.is_ok()) return data.error();
    if (cell.row >= data.value().rows.size() || cell.column >= data.value().rows.front().size())
        return ErrorCode::editor_selection_mapping_failed;
    auto changed = data.value(); changed.rows[cell.row][cell.column] = NormalizeCell(text);
    return Write(changed);
}

Result<TablePosition> TableEditor::navigate(document::NodeId table, TablePosition cell, bool forward) {
    auto data = Read(table); if (!data.is_ok()) return Result<TablePosition>::failure(data.error());
    auto changed = data.value();
    const auto columns = changed.rows.front().size();
    if (cell.row >= changed.rows.size() || cell.column >= columns)
        return Result<TablePosition>::failure(ErrorCode::editor_selection_mapping_failed);
    if (forward) {
        if (++cell.column == columns) { cell.column = 0; ++cell.row; }
        if (cell.row == changed.rows.size()) {
            changed.rows.emplace_back(columns);
            const auto result = Write(changed);
            if (result != ErrorCode::ok) return Result<TablePosition>::failure(result);
        }
    } else if (cell.column > 0) --cell.column;
    else if (cell.row > 0) { --cell.row; cell.column = columns - 1; }
    return Result<TablePosition>::success(cell);
}

ErrorCode TableEditor::insert_row(document::NodeId table, std::size_t before) {
    auto data = Read(table); if (!data.is_ok()) return data.error(); auto changed = data.value();
    before = (std::min)(before, changed.rows.size());
    changed.rows.insert(changed.rows.begin() + static_cast<std::ptrdiff_t>(before),
                        std::vector<std::string>(changed.rows.front().size()));
    return Write(changed);
}
ErrorCode TableEditor::delete_row(document::NodeId table, std::size_t row) {
    auto data = Read(table); if (!data.is_ok()) return data.error(); auto changed = data.value();
    if (row >= changed.rows.size() || changed.rows.size() == 1)
        return ErrorCode::document_invariant_failed;
    changed.rows.erase(changed.rows.begin() + static_cast<std::ptrdiff_t>(row));
    return Write(changed);
}
ErrorCode TableEditor::insert_column(document::NodeId table, std::size_t before) {
    auto data = Read(table); if (!data.is_ok()) return data.error(); auto changed = data.value();
    before = (std::min)(before, changed.rows.front().size());
    for (auto& row : changed.rows)
        row.insert(row.begin() + static_cast<std::ptrdiff_t>(before), {});
    return Write(changed);
}
ErrorCode TableEditor::delete_column(document::NodeId table, std::size_t column) {
    auto data = Read(table); if (!data.is_ok()) return data.error(); auto changed = data.value();
    if (column >= changed.rows.front().size() || changed.rows.front().size() == 1)
        return ErrorCode::document_invariant_failed;
    for (auto& row : changed.rows)
        row.erase(row.begin() + static_cast<std::ptrdiff_t>(column));
    return Write(changed);
}
ErrorCode TableEditor::paste_tsv(document::NodeId table, TablePosition start, std::string_view text) {
    auto data = Read(table); if (!data.is_ok()) return data.error(); auto changed = data.value();
    if (start.row >= changed.rows.size() || start.column >= changed.rows.front().size())
        return ErrorCode::editor_selection_mapping_failed;
    const auto pasted = ParseTsv(text);
    const auto needed_rows = start.row + pasted.size();
    std::size_t needed_columns = start.column;
    for (const auto& row : pasted) needed_columns = (std::max)(needed_columns, start.column + row.size());
    changed.rows.resize((std::max)(changed.rows.size(), needed_rows),
                        std::vector<std::string>(changed.rows.front().size()));
    for (auto& row : changed.rows) row.resize((std::max)(row.size(), needed_columns));
    for (std::size_t row = 0; row < pasted.size(); ++row)
        for (std::size_t column = 0; column < pasted[row].size(); ++column)
            changed.rows[start.row + row][start.column + column] = pasted[row][column];
    return Write(changed);
}
ErrorCode TableEditor::remove(document::NodeId table) {
    auto data = Read(table); if (!data.is_ok()) return data.error();
    return editor_.replace_source_range(data.value().begin, data.value().end, {},
        {data.value().begin, data.value().begin});
}

}  // namespace markdownmay::editor
