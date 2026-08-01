#pragma once

#include "markdownmay/editor/view_mode_controller.hpp"

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
    [[nodiscard]] ErrorCode new_document();
    [[nodiscard]] ErrorCode open_document(const std::filesystem::path& path);
    [[nodiscard]] ErrorCode save_document();
    [[nodiscard]] ErrorCode save_document_as(const std::filesystem::path& path);
    [[nodiscard]] bool is_named() const noexcept;
    [[nodiscard]] const std::filesystem::path& path() const noexcept;
    [[nodiscard]] fileio::TextEncoding encoding() const noexcept;
    [[nodiscard]] fileio::LineEnding line_ending() const noexcept;
    void set_line_ending(fileio::LineEnding line_ending) noexcept;

private:
    editor::ViewModeController modes_;
    std::filesystem::path path_;
    fileio::TextEncoding encoding_{fileio::TextEncoding::utf8};
    fileio::LineEnding line_ending_{fileio::LineEnding::crlf};
};

}  // namespace markdownmay::ui
