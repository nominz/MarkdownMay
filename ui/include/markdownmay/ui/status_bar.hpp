#pragma once

#include "markdownmay/document/document_session.hpp"
#include "markdownmay/editor/view_mode_controller.hpp"
#include "markdownmay/fileio/line_endings.hpp"
#include "markdownmay/fileio/text_encoding.hpp"

#include <windows.h>

namespace markdownmay::ui {

class StatusBar final {
public:
    StatusBar(document::DocumentSession& session,
              editor::ViewModeController& modes);
    [[nodiscard]] bool create(HWND parent);
    void resize(int width, int client_height);
    void refresh();
    void set_file_format(fileio::TextEncoding encoding,
                         fileio::LineEnding line_ending);
    [[nodiscard]] HWND handle() const noexcept;
    [[nodiscard]] int height() const noexcept;

private:
    document::DocumentSession& session_;
    editor::ViewModeController& modes_;
    HWND handle_{};
    int height_{24};
    fileio::TextEncoding encoding_{fileio::TextEncoding::utf8};
    fileio::LineEnding line_ending_{fileio::LineEnding::crlf};
};

}  // namespace markdownmay::ui
