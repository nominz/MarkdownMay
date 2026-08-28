#pragma once

#include "markdownmay/editor/paragraph_editor.hpp"
#include "markdownmay/editor/inline_formatter.hpp"
#include "markdownmay/editor/block_formatter.hpp"
#include "markdownmay/editor/list_editor.hpp"
#include "markdownmay/editor/image_controller.hpp"
#include "markdownmay/editor/table_editor.hpp"
#include "markdownmay/editor/clipboard_controller.hpp"
#include "markdownmay/editor/find_replace_controller.hpp"
#include "markdownmay/editor/rich_projection.hpp"
#include "markdownmay/editor/heading_fold_controller.hpp"
#include "markdownmay/editor/block_interaction_controller.hpp"
#include "markdownmay/editor/render_style.hpp"

#include <windows.h>

#include <utility>
#include <functional>

namespace markdownmay::editor {

enum class EditorCommand : std::uint8_t {
    bold, italic, strike, inline_code, quote, unordered_list, ordered_list, task_list,
    clear_format
};

enum class BlockFormat : std::uint8_t {
    quote, code_block, unordered_list, ordered_list, task_list
};

enum class BlockMenuCommand : std::uint8_t {
    convert_paragraph, convert_h1, convert_h2, convert_h3, convert_h4, convert_h5,
    convert_h6, remove, copy, cut, indent, outdent, add_below
};

struct BlockMenuCapabilities final {
    bool convert{};
    bool remove{};
    bool copy{};
    bool cut{};
    bool indent{};
    bool outdent{};
    bool add_below{};
};

class RichEditHost final {
public:
    explicit RichEditHost(document::DocumentSession& session);
    ~RichEditHost();
    RichEditHost(const RichEditHost&) = delete;
    RichEditHost& operator=(const RichEditHost&) = delete;

    [[nodiscard]] ErrorCode create(HWND parent, const RECT& bounds);
    [[nodiscard]] ErrorCode project();
    [[nodiscard]] ErrorCode run_deferred_reproject();
    [[nodiscard]] ErrorCode show_status_message(std::wstring_view message);
    void set_read_only(bool read_only);
    void scroll_to_fraction(std::uint64_t numerator, std::uint64_t denominator);
    [[nodiscard]] std::pair<std::uint64_t, std::uint64_t> scroll_fraction() const;
    void reset_to_start();
    [[nodiscard]] Result<TextSelection> source_selection();
    [[nodiscard]] ErrorCode select_source_range(TextSelection selection);
    void align_selection_to_top();
    [[nodiscard]] ErrorCode synchronize_change();
    void note_change_notification();
    void trace_table_event(std::string_view event) const;
    [[nodiscard]] bool is_projecting() const noexcept { return projecting_; }
    [[nodiscard]] ErrorCode complete_thematic_break();
    [[nodiscard]] ErrorCode toggle_inline(InlineFormat format);
    [[nodiscard]] ErrorCode set_link(std::string_view target, std::string_view title = {});
    [[nodiscard]] ErrorCode set_heading(std::uint8_t level);
    [[nodiscard]] ErrorCode toggle_quote();
    [[nodiscard]] ErrorCode toggle_code_block(std::string_view language = {});
    [[nodiscard]] ErrorCode insert_thematic_break();
    [[nodiscard]] ErrorCode toggle_unordered_list();
    [[nodiscard]] ErrorCode toggle_ordered_list(std::uint32_t start = 1);
    [[nodiscard]] ErrorCode toggle_task_list();
    [[nodiscard]] ErrorCode toggle_task_checked();
    [[nodiscard]] ErrorCode indent_list();
    [[nodiscard]] ErrorCode outdent_list();
    void set_document_path(std::filesystem::path path);
    [[nodiscard]] ErrorCode insert_image_reference(std::string_view target,
        std::string_view alternative, std::string_view title = {});
    [[nodiscard]] ErrorCode insert_image_file(const std::filesystem::path& image,
        bool copy_to_assets, std::string_view alternative);
    [[nodiscard]] ErrorCode replace_image(document::NodeId image, std::string_view target,
        std::string_view alternative, std::string_view title = {});
    [[nodiscard]] ErrorCode resize_image(document::NodeId image, std::uint16_t percent);
    [[nodiscard]] ErrorCode remove_image(document::NodeId image);
    [[nodiscard]] ErrorCode insert_table(std::size_t rows, std::size_t columns);
    [[nodiscard]] ErrorCode set_table_cell(document::NodeId table, TablePosition cell,
                                           std::string_view text);
    [[nodiscard]] Result<TablePosition> navigate_table(document::NodeId table,
        TablePosition cell, bool forward);
    [[nodiscard]] ErrorCode insert_table_row(document::NodeId table, std::size_t before);
    [[nodiscard]] ErrorCode delete_table_row(document::NodeId table, std::size_t row);
    [[nodiscard]] ErrorCode insert_table_column(document::NodeId table, std::size_t before);
    [[nodiscard]] ErrorCode delete_table_column(document::NodeId table, std::size_t column);
    [[nodiscard]] ErrorCode paste_table(document::NodeId table, TablePosition start,
                                        std::string_view tsv);
    [[nodiscard]] ErrorCode remove_table(document::NodeId table);
    [[nodiscard]] Result<TextSelection> find_text(std::string_view query, bool forward,
        bool case_sensitive, bool wrap = true);
    [[nodiscard]] ErrorCode replace_text(std::string_view query,
        std::string_view replacement, bool case_sensitive);
    [[nodiscard]] Result<std::size_t> replace_all_text(std::string_view query,
        std::string_view replacement, bool case_sensitive);
    [[nodiscard]] ErrorCode paste_plain(std::string_view text);
    [[nodiscard]] ErrorCode paste_html(std::string_view html);
    [[nodiscard]] Result<DropResult> drop_files(
        std::span<const std::filesystem::path> files, bool copy_images_to_assets = true);
    [[nodiscard]] ErrorCode paste_from_clipboard();
    [[nodiscard]] ErrorCode copy();
    [[nodiscard]] ErrorCode cut();
    [[nodiscard]] ErrorCode select_all();
    [[nodiscard]] ErrorCode execute(EditorCommand command);
    [[nodiscard]] ErrorCode clear_paragraph_formatting();
    [[nodiscard]] bool inline_active(InlineFormat format) const noexcept;
    [[nodiscard]] bool block_active(BlockFormat format) const noexcept;
    [[nodiscard]] std::uint8_t heading_level();
    [[nodiscard]] ErrorCode undo();
    [[nodiscard]] ErrorCode redo();
    [[nodiscard]] HWND handle() const noexcept;
    void apply_appearance(COLORREF text, COLORREF background, UINT dpi);
    void set_render_style(RenderStyle style);
    [[nodiscard]] RenderStyle render_style() const noexcept;
    void refresh_layout_after_resize();
    [[nodiscard]] bool begin_native_table_pointer_gesture(POINT point);
    void end_native_table_pointer_gesture();
    [[nodiscard]] bool is_native_table_column_boundary(POINT point) const;
    void draw_table_grid(HDC dc) const;
    void draw_quote_guides(HDC dc) const;
    void draw_code_block_frames(HDC dc) const;
    void set_heading_folds(HeadingFoldController* folds);
    void apply_heading_folds();
    void draw_heading_folds(HDC dc) const;
    [[nodiscard]] bool handle_heading_fold_click(POINT point);
    [[nodiscard]] bool toggle_heading_fold_at_caret();
    void restore_heading_fold_scroll();
    [[nodiscard]] bool update_block_hover(POINT point);
    void clear_block_hover();
    void invalidate_block_layout();
    void draw_block_interaction(HDC dc) const;
    [[nodiscard]] std::optional<BlockCommandContext> hovered_block() const noexcept;
    [[nodiscard]] RECT block_type_hit_rect() const noexcept;
    [[nodiscard]] RECT block_handle_hit_rect() const noexcept;
    [[nodiscard]] BlockMenuCapabilities query_block_menu(
        const BlockCommandContext& context) const noexcept;
    [[nodiscard]] ErrorCode execute_block_menu(
        BlockMenuCommand command, const BlockCommandContext& context);
    [[nodiscard]] HMENU create_block_context_menu(
        const BlockCommandContext& context) const;
    void set_block_menu_callback(std::function<void(
        BlockMenuCommand, const BlockCommandContext&)> callback);
    [[nodiscard]] bool show_block_context_menu(const BlockCommandContext& context,
        POINT screen_point);
    [[nodiscard]] bool show_block_context_menu_at_caret();
    [[nodiscard]] bool handle_block_handle_click(POINT point);
    [[nodiscard]] HWND block_type_window() const noexcept;
    [[nodiscard]] HWND block_handle_window() const noexcept;
    [[nodiscard]] std::optional<BlockCommandContext> block_context_at_source(
        std::uint64_t source_offset) const noexcept;
    void draw_block_accessible_button(const DRAWITEMSTRUCT& item) const;

private:
    document::DocumentSession& session_;
    ParagraphEditor editor_;
    InlineFormatter formatter_;
    BlockFormatter block_formatter_;
    ListEditor list_editor_;
    ImageController image_controller_;
    TableEditor table_editor_;
    ClipboardController clipboard_controller_;
    FindReplaceController find_replace_controller_;
    std::filesystem::path document_path_;
    HWND handle_{};
    HMODULE rich_edit_module_{};
    bool projecting_{};
    bool reset_native_table_structure_{};
    bool native_table_pointer_read_only_{};
    bool native_table_pointer_was_read_only_{};
    bool native_table_format_change_pending_{};
    bool deferred_reproject_pending_{};
    RichProjection projection_;
    COLORREF text_color_{RGB(32, 32, 32)};
    COLORREF background_color_{RGB(255, 255, 255)};
    UINT dpi_{96};
    RenderStyle render_style_{RenderStyle::yuan_lang};
    HeadingFoldController* folds_{};
    BlockInteractionController block_interactions_;
    std::optional<BlockCommandContext> hovered_block_;
    bool tracking_mouse_leave_{};
    bool block_layout_valid_{};
    HWND block_type_window_{};
    HWND block_handle_window_{};
    std::function<void(BlockMenuCommand, const BlockCommandContext&)> block_menu_callback_;
    POINT pending_fold_scroll_{};
    bool fold_scroll_pending_{};

    [[nodiscard]] bool refresh_block_layout();
    [[nodiscard]] bool block_hidden_by_fold(const VisibleBlockItem& item) const noexcept;
    void update_block_accessible_windows();
};

}  // namespace markdownmay::editor
