#pragma once

#include "markdownmay/ui/document_window.hpp"
#include "markdownmay/ui/menu_controller.hpp"
#include "markdownmay/ui/status_bar.hpp"
#include "markdownmay/ui/toolbar.hpp"

#include <windows.h>

#include <memory>
#include <filesystem>

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
    void set_drop_callback(
        std::function<void(const std::filesystem::path&)> callback);
    void set_close_callback(std::function<bool()> callback);
    void set_activate_callback(std::function<void()> callback);
    void set_open_request_callback(std::function<void()> callback);
    void notify_open_requests() noexcept;
    void refresh_document_chrome();
    void set_recent_files(std::vector<std::filesystem::path> files);
    void set_pending_open_count(std::size_t count);
    [[nodiscard]] HACCEL accelerator() const noexcept;
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message,
                                             WPARAM w_param, LPARAM l_param);

private:
    [[nodiscard]] ErrorCode CreateDocumentWindow();
    void Layout();

    HINSTANCE instance_{};
    HWND handle_{};
    document::DocumentSession& session_;
    DocumentWindow document_window_;
    std::unique_ptr<MenuController> menu_controller_;
    std::unique_ptr<Toolbar> toolbar_;
    StatusBar status_bar_;
    std::shared_ptr<int> lifetime_{std::make_shared<int>(0)};
    std::function<void(const std::filesystem::path&)> drop_callback_;
    std::function<bool()> close_callback_;
    std::function<void()> activate_callback_;
    std::function<void()> open_request_callback_;
    std::size_t pending_open_count_{};
};

}  // namespace markdownmay::ui
