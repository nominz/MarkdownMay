#pragma once

#include "markdownmay/document_session.hpp"

namespace markdownmay {

enum class EditorCommand : std::uint16_t {
    undo,
    redo,
    cut,
    copy,
    paste,
    select_all,
    bold,
    italic,
    strike,
    inline_code,
    heading_1,
    heading_2,
    heading_3,
    unordered_list,
    ordered_list,
    task_list,
    quote,
    code_block,
    insert_link,
    insert_image,
    insert_table,
};

struct ViewLocation final {
    NodePosition semantic;
    std::uint32_t source_line{};
    std::uint32_t source_column{};
};

class IEditorSurface {
public:
    virtual ~IEditorSurface() = default;
    virtual void bind(IDocumentSession& session) = 0;
    virtual void unbind() noexcept = 0;
    [[nodiscard]] virtual bool can_execute(EditorCommand command) const = 0;
    [[nodiscard]] virtual Status execute(EditorCommand command) = 0;
    [[nodiscard]] virtual ViewLocation location() const = 0;
    virtual void restore_location(const ViewLocation& location) = 0;
    virtual void set_read_only(bool read_only) = 0;
    virtual void commit_composition() = 0;
};

class IViewModeController {
public:
    virtual ~IViewModeController() = default;
    [[nodiscard]] virtual Status switch_to(ViewMode target) = 0;
    [[nodiscard]] virtual ViewMode current_mode() const noexcept = 0;
    virtual void on_parse_snapshot(const ParseSnapshot& snapshot) = 0;
};

}  // namespace markdownmay
