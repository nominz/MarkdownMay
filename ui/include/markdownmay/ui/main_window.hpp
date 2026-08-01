#pragma once

#include "markdownmay/ui/document_window.hpp"
#include "markdownmay/ui/menu_controller.hpp"
#include "markdownmay/ui/status_bar.hpp"
#include "markdownmay/ui/toolbar.hpp"

#include <windows.h>

#include <memory>

namespace markdownmay::ui {

class MainWindow final {
public:
    explicit MainWindow(document::DocumentSession& session);
    ~MainWindow();
    [[nodiscard]] ErrorCode create(HINSTANCE instance, int show_command);
    [[nodiscard]] HWND handle() const noexcept;
    [[nodiscard]] DocumentWindow& document_window() noexcept;
    [[nodiscard]] Toolbar* toolbar() noexcept;
    [[nodiscard]] StatusBar& status_bar() noexcept;
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
    std::unique_ptr<Toolbar> toolbar_;
    StatusBar status_bar_;
    std::shared_ptr<int> lifetime_{std::make_shared<int>(0)};
};

}  // namespace markdownmay::ui
