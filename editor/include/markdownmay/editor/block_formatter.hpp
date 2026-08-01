#pragma once

#include "markdownmay/editor/paragraph_editor.hpp"

#include <cstdint>
#include <string_view>

namespace markdownmay::editor {

class BlockFormatter final {
public:
    BlockFormatter(document::DocumentSession& session, ParagraphEditor& editor);
    [[nodiscard]] ErrorCode set_heading(std::uint8_t level);
    [[nodiscard]] ErrorCode toggle_quote();
    [[nodiscard]] ErrorCode toggle_code_block(std::string_view language = {});
    [[nodiscard]] ErrorCode insert_thematic_break();

private:
    document::DocumentSession& session_;
    ParagraphEditor& editor_;
};

}  // namespace markdownmay::editor
