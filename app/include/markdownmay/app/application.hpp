#pragma once

#include "markdownmay/document/document_session.hpp"
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
};

}  // namespace markdownmay::app
