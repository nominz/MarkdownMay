#pragma once

#include "markdownmay/editor/source_sync.hpp"
#include "markdownmay/editor/paragraph_editor.hpp"
#include "markdownmay/editor/heading_fold_controller.hpp"
#include "markdownmay/editor/document_context_menu.hpp"

#include <windows.h>

#include <utility>

#include <cstdint>
#include <filesystem>
#include <functional>

namespace markdownmay::editor {

class SourceView final {
public:
    explicit SourceView(document::DocumentSession& session);
    ~SourceView();
    SourceView(const SourceView&) = delete;
    SourceView& operator=(const SourceView&) = delete;

    [[nodiscard]] ErrorCode create(HWND parent, const RECT& bounds);
    [[nodiscard]] ErrorCode project();
    [[nodiscard]] ErrorCode synchronize_now();
    [[nodiscard]] ErrorCode save(const std::filesystem::path& target,
        fileio::TextEncoding encoding, fileio::LineEnding line_ending);
    [[nodiscard]] ErrorCode go_to_first_error();
    [[nodiscard]] ErrorCode last_error() const noexcept;
    [[nodiscard]] const std::vector<SourceDiagnostic>& diagnostics() const noexcept;
    [[nodiscard]] HWND handle() const noexcept;
    [[nodiscard]] HWND host_handle() const noexcept;
    void apply_appearance(COLORREF text, COLORREF background, COLORREF accent, UINT dpi);
    [[nodiscard]] TextSelection source_selection() const noexcept;
    [[nodiscard]] ErrorCode select_source_range(TextSelection selection);
    void align_selection_to_top();
    [[nodiscard]] std::pair<std::uint64_t, std::uint64_t> scroll_fraction() const;
    void scroll_to_fraction(std::uint64_t numerator, std::uint64_t denominator);
    [[nodiscard]] ErrorCode cut();
    [[nodiscard]] ErrorCode copy();
    [[nodiscard]] ErrorCode paste();
    [[nodiscard]] ErrorCode select_all();
    [[nodiscard]] ErrorCode erase_selection();
    void set_document_context_menu(DocumentContextStateQuery query,
        DocumentContextCommandHandler handler);
    void set_read_only(bool read_only);
    void set_synchronized_callback(std::function<void(ErrorCode)> callback);
    void set_scroll_callback(std::function<void(std::uint64_t, std::uint64_t)> callback);
    void set_heading_folds(HeadingFoldController* folds);
    void apply_heading_folds();
    [[nodiscard]] bool toggle_heading_fold_at_caret();
    [[nodiscard]] bool show_document_context_menu(POINT screen_point);
    void remember_context_selection_at(POINT client_point);
    void restore_context_selection();
    static LRESULT CALLBACK HostProcedure(HWND window, UINT message,
                                           WPARAM w_param, LPARAM l_param);

private:
    void Configure();
    void ApplyStyles();
    void ApplyDiagnostics();
    void ScheduleSynchronize();
    [[nodiscard]] std::string ReadSource() const;

    document::DocumentSession& session_;
    SourceSync sync_;
    HWND host_{};
    HWND editor_{};
    bool projecting_{};
    bool read_only_{};
    std::uint64_t pending_since_{};
    ErrorCode last_error_{ErrorCode::ok};
    std::function<void(ErrorCode)> synchronized_callback_;
    std::function<void(std::uint64_t, std::uint64_t)> scroll_callback_;
    COLORREF text_color_{RGB(32, 32, 32)};
    COLORREF background_color_{RGB(255, 255, 255)};
    COLORREF accent_color_{RGB(28, 80, 150)};
    UINT dpi_{96};
    HeadingFoldController* folds_{};
    DocumentContextStateQuery document_context_query_;
    DocumentContextCommandHandler document_context_handler_;
    std::uint64_t context_selection_anchor_{};
    std::uint64_t context_selection_caret_{};
    bool context_selection_pending_{};
};

}  // namespace markdownmay::editor
