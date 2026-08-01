#pragma once

#include "markdownmay/core/result.hpp"
#include "markdownmay/document/document_session.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace markdownmay::editor {

struct TextSelection final {
    std::uint64_t anchor{};
    std::uint64_t caret{};
};

class ParagraphEditor final {
public:
    explicit ParagraphEditor(document::DocumentSession& session);

    [[nodiscard]] TextSelection selection() const noexcept;
    [[nodiscard]] ErrorCode set_selection(TextSelection selection) noexcept;
    [[nodiscard]] ErrorCode insert_text(std::string_view utf8_text);
    [[nodiscard]] ErrorCode replace_source_range(
        std::uint64_t begin,
        std::uint64_t end,
        std::string replacement,
        TextSelection next_selection);
    [[nodiscard]] ErrorCode delete_backward();
    [[nodiscard]] ErrorCode delete_forward();
    [[nodiscard]] ErrorCode undo();
    [[nodiscard]] ErrorCode redo();
    [[nodiscard]] bool can_undo() const noexcept;
    [[nodiscard]] bool can_redo() const noexcept;
    [[nodiscard]] std::size_t undo_depth() const noexcept;
    [[nodiscard]] std::size_t redo_depth() const noexcept;

private:
    struct HistoryEntry final {
        std::uint64_t begin{};
        std::string removed;
        std::string inserted;
        TextSelection before;
        TextSelection after;
    };

    [[nodiscard]] ErrorCode ReplaceSelection(std::string replacement);
    [[nodiscard]] ErrorCode Apply(
        std::uint64_t begin,
        std::uint64_t end,
        std::string replacement,
        document::EditOrigin origin,
        TextSelection next_selection);
    [[nodiscard]] bool IsValidSelection(TextSelection value) const noexcept;

    document::DocumentSession& session_;
    TextSelection selection_{};
    std::uint64_t next_transaction_{1};
    std::vector<HistoryEntry> undo_;
    std::vector<HistoryEntry> redo_;
};

}  // namespace markdownmay::editor
