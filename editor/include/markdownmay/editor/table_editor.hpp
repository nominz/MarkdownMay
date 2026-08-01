#pragma once

#include "markdownmay/editor/paragraph_editor.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace markdownmay::editor {

struct TablePosition final { std::size_t row{}; std::size_t column{}; };

class TableEditor final {
public:
    TableEditor(document::DocumentSession& session, ParagraphEditor& editor);
    [[nodiscard]] ErrorCode insert(std::size_t rows, std::size_t columns);
    [[nodiscard]] ErrorCode set_cell(document::NodeId table, TablePosition cell,
                                     std::string_view text);
    [[nodiscard]] Result<TablePosition> navigate(document::NodeId table,
        TablePosition cell, bool forward);
    [[nodiscard]] ErrorCode insert_row(document::NodeId table, std::size_t before);
    [[nodiscard]] ErrorCode delete_row(document::NodeId table, std::size_t row);
    [[nodiscard]] ErrorCode insert_column(document::NodeId table, std::size_t before);
    [[nodiscard]] ErrorCode delete_column(document::NodeId table, std::size_t column);
    [[nodiscard]] ErrorCode paste_tsv(document::NodeId table, TablePosition start,
                                      std::string_view text);
    [[nodiscard]] ErrorCode remove(document::NodeId table);

private:
    struct TableData final {
        std::uint64_t begin{};
        std::uint64_t end{};
        std::string prefix;
        std::string suffix;
        std::vector<std::vector<std::string>> rows;
    };
    [[nodiscard]] Result<TableData> Read(document::NodeId table) const;
    [[nodiscard]] ErrorCode Write(const TableData& table);
    document::DocumentSession& session_;
    ParagraphEditor& editor_;
};

}  // namespace markdownmay::editor
