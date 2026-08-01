#pragma once

#include "markdownmay/document/document_session.hpp"
#include "markdownmay/app/command_dispatcher.hpp"
#include "markdownmay/ui/main_window.hpp"

#include <windows.h>

namespace markdownmay::app {

class Application final {
public:
    explicit Application(HINSTANCE instance);
    [[nodiscard]] int run(int show_command);
    [[nodiscard]] ui::MainWindow& main_window() noexcept;

private:
    HINSTANCE instance_{};
    document::DocumentSession session_{""};
    ui::MainWindow main_window_{session_};
    CommandDispatcher dispatcher_{main_window_.document_window(), [this] {
        if (main_window_.handle()) PostMessageW(main_window_.handle(), WM_CLOSE, 0, 0);
    }};
};

}  // namespace markdownmay::app
