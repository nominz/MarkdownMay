#pragma once

#include "markdownmay/ui/document_window.hpp"

#include <windows.h>

namespace markdownmay::ui {

class MainWindow final {
public:
    explicit MainWindow(document::DocumentSession& session);
    [[nodiscard]] ErrorCode create(HINSTANCE instance, int show_command);
    [[nodiscard]] HWND handle() const noexcept;
    [[nodiscard]] DocumentWindow& document_window() noexcept;
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message,
                                             WPARAM w_param, LPARAM l_param);

private:
    [[nodiscard]] ErrorCode CreateDocumentWindow();
    void Layout();

    HINSTANCE instance_{};
    HWND handle_{};
    DocumentWindow document_window_;
};

}  // namespace markdownmay::ui
