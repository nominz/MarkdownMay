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

#include <windows.h>

namespace markdownmay::editor {

enum class EditorCommand : std::uint8_t {
    bold, italic, strike, inline_code, quote, unordered_list, ordered_list, task_list
};

class RichEditHost final {
public:
    explicit RichEditHost(document::DocumentSession& session);
    ~RichEditHost();
    RichEditHost(const RichEditHost&) = delete;
    RichEditHost& operator=(const RichEditHost&) = delete;

    [[nodiscard]] ErrorCode create(HWND parent, const RECT& bounds);
    [[nodiscard]] ErrorCode project();
    [[nodiscard]] ErrorCode show_status_message(std::wstring_view message);
    void set_read_only(bool read_only);
    void scroll_to_fraction(std::uint64_t numerator, std::uint64_t denominator);
    [[nodiscard]] Result<TextSelection> source_selection();
    [[nodiscard]] ErrorCode select_source_range(TextSelection selection);
    [[nodiscard]] ErrorCode synchronize_change();
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
    [[nodiscard]] ErrorCode undo();
    [[nodiscard]] ErrorCode redo();
    [[nodiscard]] HWND handle() const noexcept;
    void apply_appearance(COLORREF text, COLORREF background, UINT dpi);

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
    RichProjection projection_;
    COLORREF text_color_{RGB(32, 32, 32)};
    COLORREF background_color_{RGB(255, 255, 255)};
    UINT dpi_{96};
};

}  // namespace markdownmay::editor
