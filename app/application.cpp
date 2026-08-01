#include "markdownmay/app/application.hpp"

namespace markdownmay::app {

Application::Application(HINSTANCE instance) : instance_(instance) {}

int Application::run(int show_command) {
    if (main_window_.create(instance_, show_command) != ErrorCode::ok) return 1;
    MSG message{};
    int result{};
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return result < 0 ? 2 : static_cast<int>(message.wParam);
}

ui::MainWindow& Application::main_window() noexcept { return main_window_; }

}  // namespace markdownmay::app
