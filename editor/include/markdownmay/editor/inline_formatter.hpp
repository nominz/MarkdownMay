#pragma once

#include "markdownmay/editor/paragraph_editor.hpp"

#include <string_view>

namespace markdownmay::editor {

enum class InlineFormat { bold, italic, strike, code };

class InlineFormatter final {
public:
    InlineFormatter(document::DocumentSession& session, ParagraphEditor& editor);
    [[nodiscard]] ErrorCode toggle(InlineFormat format);
    [[nodiscard]] ErrorCode set_link(std::string_view target, std::string_view title = {});

private:
    document::DocumentSession& session_;
    ParagraphEditor& editor_;
};

}  // namespace markdownmay::editor
