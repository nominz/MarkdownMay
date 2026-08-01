#pragma once

#include "markdownmay/editor/richedit_host.hpp"
#include "markdownmay/editor/split_view.hpp"
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
    [[nodiscard]] ErrorCode reload(std::string source);
    void set_document_path(std::filesystem::path path);
    [[nodiscard]] bool can_undo() const noexcept;
    [[nodiscard]] bool can_redo() const noexcept;
    [[nodiscard]] ViewMode mode() const noexcept;
    [[nodiscard]] HWND handle() const noexcept;
    [[nodiscard]] RichEditHost& render_view() noexcept;
    [[nodiscard]] SourceView& source_view() noexcept;
    [[nodiscard]] SplitView& split_view() noexcept;
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

    document::DocumentSession& session_;
    RichEditHost render_;
    SplitView split_;
    HWND host_{};
    ViewMode mode_{ViewMode::render};
    std::string observed_source_;
    std::vector<HistoryEntry> undo_;
    std::vector<HistoryEntry> redo_;
    std::shared_ptr<int> lifetime_{std::make_shared<int>(0)};
    bool applying_history_{};
    std::uint64_t next_transaction_{1};
};

}  // namespace markdownmay::editor
