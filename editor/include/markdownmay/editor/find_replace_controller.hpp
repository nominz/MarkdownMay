#pragma once

#include "markdownmay/editor/paragraph_editor.hpp"

#include <string_view>

namespace markdownmay::editor {

class FindReplaceController final {
public:
    FindReplaceController(document::DocumentSession& session, ParagraphEditor& editor);
    [[nodiscard]] Result<TextSelection> find(std::string_view query, bool forward,
        bool case_sensitive, bool wrap = true, bool wildcards = false);
    [[nodiscard]] ErrorCode replace_current(std::string_view query,
        std::string_view replacement, bool case_sensitive, bool wildcards = false);
    [[nodiscard]] Result<std::size_t> replace_all(std::string_view query,
        std::string_view replacement, bool case_sensitive, bool wildcards = false);
    [[nodiscard]] static std::string expand_special_format(std::string_view value);
private:
    document::DocumentSession& session_;
    ParagraphEditor& editor_;
};

}  // namespace markdownmay::editor
