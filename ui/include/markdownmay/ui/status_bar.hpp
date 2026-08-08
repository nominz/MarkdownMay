#pragma once

#include "markdownmay/document/document_session.hpp"
#include "markdownmay/editor/view_mode_controller.hpp"
#include "markdownmay/fileio/line_endings.hpp"
#include "markdownmay/fileio/text_encoding.hpp"

#include <windows.h>
#include <commctrl.h>
#include <array>
#include <string>

namespace markdownmay::ui {

class StatusBar final {
public:
    StatusBar(document::DocumentSession& session,
              editor::ViewModeController& modes);
    ~StatusBar();
    [[nodiscard]] bool create(HWND parent);
    void resize(int width, int client_height);
    void refresh();
    void set_file_format(fileio::TextEncoding encoding,
                         fileio::LineEnding line_ending);
    [[nodiscard]] HWND handle() const noexcept;
    [[nodiscard]] int height() const noexcept;
    void apply_appearance(COLORREF text, COLORREF background, UINT dpi);

private:
    static LRESULT CALLBACK SubclassProcedure(HWND window, UINT message,
        WPARAM w_param, LPARAM l_param, UINT_PTR id, DWORD_PTR data);
    void Paint(HDC dc);
    document::DocumentSession& session_;
    editor::ViewModeController& modes_;
    HWND handle_{};
    int height_{24};
    fileio::TextEncoding encoding_{fileio::TextEncoding::utf8};
    fileio::LineEnding line_ending_{fileio::LineEnding::crlf};
    HFONT font_{};
    COLORREF text_color_{RGB(70, 70, 70)};
    COLORREF background_color_{RGB(249, 249, 249)};
    UINT dpi_{96};
    std::array<std::wstring, 5> labels_{};
};

}  // namespace markdownmay::ui
