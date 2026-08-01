#pragma once

#include "markdownmay/editor/paragraph_editor.hpp"
#include "markdownmay/editor/inline_formatter.hpp"
#include "markdownmay/editor/rich_projection.hpp"

#include <windows.h>

namespace markdownmay::editor {

class RichEditHost final {
public:
    explicit RichEditHost(document::DocumentSession& session);
    ~RichEditHost();
    RichEditHost(const RichEditHost&) = delete;
    RichEditHost& operator=(const RichEditHost&) = delete;

    [[nodiscard]] ErrorCode create(HWND parent, const RECT& bounds);
    [[nodiscard]] ErrorCode project();
    [[nodiscard]] ErrorCode synchronize_change();
    [[nodiscard]] ErrorCode toggle_inline(InlineFormat format);
    [[nodiscard]] ErrorCode set_link(std::string_view target, std::string_view title = {});
    [[nodiscard]] ErrorCode undo();
    [[nodiscard]] ErrorCode redo();
    [[nodiscard]] HWND handle() const noexcept;

private:
    document::DocumentSession& session_;
    ParagraphEditor editor_;
    InlineFormatter formatter_;
    HWND handle_{};
    HMODULE rich_edit_module_{};
    bool projecting_{};
    RichProjection projection_;
};

}  // namespace markdownmay::editor
