#pragma once

#include "markdownmay/editor/richedit_host.hpp"
#include "markdownmay/editor/find_replace_controller.hpp"
#include "markdownmay/editor/split_view.hpp"
#include "markdownmay/editor/heading_fold_controller.hpp"
#include "markdownmay/fileio/file_service.hpp"

#include <windows.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace markdownmay::editor {

enum class ViewMode : std::uint8_t { render, source, split };

class ViewModeController final {
public:
    explicit ViewModeController(document::DocumentSession& session);
    ~ViewModeController();
    ViewModeController(const ViewModeController&) = delete;
    ViewModeController& operator=(const ViewModeController&) = delete;

    [[nodiscard]] ErrorCode create(HWND parent, const RECT& bounds);
    [[nodiscard]] ErrorCode switch_to(ViewMode target);
    [[nodiscard]] ErrorCode undo();
    [[nodiscard]] ErrorCode redo();
    [[nodiscard]] ErrorCode cut();
    [[nodiscard]] ErrorCode copy();
    [[nodiscard]] ErrorCode paste();
    [[nodiscard]] ErrorCode select_all();
    [[nodiscard]] ErrorCode execute(EditorCommand command);
    [[nodiscard]] ErrorCode save(const std::filesystem::path& target,
        fileio::TextEncoding encoding, fileio::LineEnding line_ending,
        fileio::BeforeAtomicReplace before_replace = {});
    [[nodiscard]] ErrorCode reload(std::string source,
        document::DocumentKind kind = document::DocumentKind::markdown);
    [[nodiscard]] ErrorCode change_document_kind(document::DocumentKind kind);
    [[nodiscard]] bool supports_markdown_commands() const noexcept;
    void set_document_path(std::filesystem::path path);
    [[nodiscard]] bool can_undo() const noexcept;
    [[nodiscard]] bool can_redo() const noexcept;
    [[nodiscard]] ViewMode mode() const noexcept;
    [[nodiscard]] bool inline_active(InlineFormat format) const noexcept;
    [[nodiscard]] std::uint8_t current_heading_level() const;
    [[nodiscard]] ErrorCode navigate_to_source(std::uint64_t offset);
    [[nodiscard]] bool toggle_heading_fold_at(std::uint64_t heading_source_offset);
    [[nodiscard]] Result<TextSelection> find_text(std::string_view query,
        bool forward, bool case_sensitive, bool wildcards, bool wrap = true);
    [[nodiscard]] ErrorCode replace_current_text(std::string_view query,
        std::string_view replacement, bool case_sensitive, bool wildcards);
    [[nodiscard]] Result<std::size_t> replace_all_text(std::string_view query,
        std::string_view replacement, bool case_sensitive, bool wildcards);
    [[nodiscard]] Result<TextSelection> synchronized_source_selection();
    [[nodiscard]] ErrorCode refresh_after_session_edit();
    [[nodiscard]] HWND handle() const noexcept;
    [[nodiscard]] RichEditHost& render_view() noexcept;
    [[nodiscard]] SourceView& source_view() noexcept;
    [[nodiscard]] SplitView& split_view() noexcept;
    void apply_appearance(COLORREF text, COLORREF background,
                          COLORREF accent, UINT dpi);
    static LRESULT CALLBACK HostProcedure(HWND window, UINT message,
                                           WPARAM w_param, LPARAM l_param);

private:
    struct HistoryEntry final { std::string before; std::string after; };
    void Layout(int width, int height);
    void ObserveChange(const document::DocumentEvent& event);
    [[nodiscard]] TextSelection CaptureSelection();
    void RestoreSelection(TextSelection selection);
    [[nodiscard]] ErrorCode RefreshActive();
    [[nodiscard]] ErrorCode ApplyHistory(std::string source,
                                          document::EditOrigin origin);
    [[nodiscard]] ErrorCode SynchronizeActive();
    void ApplyHeadingFolds();

    document::DocumentSession& session_;
    ParagraphEditor search_editor_;
    FindReplaceController find_replace_;
    HeadingFoldController heading_folds_;
    RichEditHost render_;
    SplitView split_;
    HWND host_{};
    ViewMode mode_{ViewMode::render};
    std::string observed_source_;
    std::vector<HistoryEntry> undo_;
    std::vector<HistoryEntry> redo_;
    std::shared_ptr<int> lifetime_{std::make_shared<int>(0)};
    bool applying_history_{};
    bool switching_mode_{};
    std::uint64_t next_transaction_{1};
};

}  // namespace markdownmay::editor
