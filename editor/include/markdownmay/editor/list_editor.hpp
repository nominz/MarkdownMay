#pragma once

#include "markdownmay/editor/paragraph_editor.hpp"

#include <cstdint>

namespace markdownmay::editor {

class ListEditor final {
public:
    enum class Kind { none, unordered, ordered, task };
    ListEditor(document::DocumentSession& session, ParagraphEditor& editor);
    [[nodiscard]] ErrorCode toggle_unordered();
    [[nodiscard]] ErrorCode toggle_ordered(std::uint32_t start = 1);
    [[nodiscard]] ErrorCode toggle_task();
    [[nodiscard]] ErrorCode toggle_checked();
    [[nodiscard]] ErrorCode indent();
    [[nodiscard]] ErrorCode outdent();
    [[nodiscard]] ErrorCode continue_item();
    [[nodiscard]] ErrorCode set_ordered_start(std::uint32_t start);

private:
    [[nodiscard]] ErrorCode Toggle(Kind target, std::uint32_t start);
    [[nodiscard]] ErrorCode ShiftIndent(bool increase);
    document::DocumentSession& session_;
    ParagraphEditor& editor_;
};

}  // namespace markdownmay::editor
