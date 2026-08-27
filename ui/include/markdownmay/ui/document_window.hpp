#pragma once

#include "markdownmay/editor/view_mode_controller.hpp"
#include "markdownmay/ui/outline_view.hpp"
#include "markdownmay/ui/find_replace_bar.hpp"

#include <windows.h>

#include <filesystem>

namespace markdownmay::ui {

class DocumentWindow final {
public:
    explicit DocumentWindow(document::DocumentSession& session);
    [[nodiscard]] ErrorCode create(HWND parent, const RECT& bounds);
    void resize(const RECT& bounds);
    [[nodiscard]] HWND handle() const noexcept;
    [[nodiscard]] editor::ViewModeController& modes() noexcept;
    void set_outline_visible(bool visible);
    void toggle_outline();
    [[nodiscard]] bool outline_visible() const noexcept;
    void refresh_outline_state();
    [[nodiscard]] HWND outline_handle() const noexcept;
    [[nodiscard]] bool handle_control(HWND control, std::uint16_t notification);
    [[nodiscard]] bool handle_notify(const NMHDR& notification);
    void show_find(bool replace_mode);
    void toggle_find(bool replace_mode);
    [[nodiscard]] ErrorCode new_document();
    [[nodiscard]] ErrorCode open_document(const std::filesystem::path& path);
    [[nodiscard]] ErrorCode save_document();
    [[nodiscard]] ErrorCode save_document_as(const std::filesystem::path& path);
    [[nodiscard]] ErrorCode reload_document();
    [[nodiscard]] bool is_named() const noexcept;
    [[nodiscard]] bool is_read_only() const noexcept;
    [[nodiscard]] bool has_external_change() const;
    void acknowledge_external_change();
    [[nodiscard]] const std::filesystem::path& path() const noexcept;
    [[nodiscard]] fileio::TextEncoding encoding() const noexcept;
    [[nodiscard]] fileio::LineEnding line_ending() const noexcept;
    void set_line_ending(fileio::LineEnding line_ending) noexcept;
    void apply_appearance(COLORREF text, COLORREF background,
                          COLORREF accent, UINT dpi);
    static LRESULT CALLBACK DividerProcedure(HWND window, UINT message,
        WPARAM w_param, LPARAM l_param, UINT_PTR id, DWORD_PTR data);

private:
    document::DocumentSession& session_;
    editor::ViewModeController modes_;
    FindReplaceBar find_replace_;
    OutlineView outline_;
    RECT bounds_{};
    bool outline_visible_{true};
    HWND outline_divider_{};
    bool dragging_outline_{};
    std::filesystem::path path_;
    fileio::TextEncoding encoding_{fileio::TextEncoding::utf8};
    fileio::LineEnding line_ending_{fileio::LineEnding::crlf};
    bool read_only_{};
    std::string disk_source_;
};

}  // namespace markdownmay::ui
