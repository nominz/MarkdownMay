#pragma once

#include "markdownmay/ui/document_window.hpp"
#include "markdownmay/ui/menu_controller.hpp"

#include <windows.h>

#include <memory>

namespace markdownmay::ui {

class MainWindow final {
public:
    explicit MainWindow(document::DocumentSession& session);
    [[nodiscard]] ErrorCode create(HINSTANCE instance, int show_command);
    [[nodiscard]] HWND handle() const noexcept;
    [[nodiscard]] DocumentWindow& document_window() noexcept;
    void set_command_callbacks(MenuController::Query query,
                               MenuController::Execute execute);
    [[nodiscard]] HACCEL accelerator() const noexcept;
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message,
                                             WPARAM w_param, LPARAM l_param);

private:
    [[nodiscard]] ErrorCode CreateDocumentWindow();
    void Layout();

    HINSTANCE instance_{};
    HWND handle_{};
    DocumentWindow document_window_;
    std::unique_ptr<MenuController> menu_controller_;
};

}  // namespace markdownmay::ui
