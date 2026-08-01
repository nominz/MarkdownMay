#include "markdownmay/app/application.hpp"

namespace markdownmay::app {

Application::Application(HINSTANCE instance) : instance_(instance) {
    main_window_.set_command_callbacks(
        [this](CommandId command) { return dispatcher_.query(command); },
        [this](CommandId command) {
            const auto result = dispatcher_.execute(command);
            if (result != ErrorCode::ok && main_window_.handle()) {
                MessageBoxW(main_window_.handle(),
                    L"当前操作无法完成，请检查文档内容后重试。",
                    L"马冬梅", MB_OK | MB_ICONWARNING);
            }
        });
}

int Application::run(int show_command) {
    if (main_window_.create(instance_, show_command) != ErrorCode::ok) return 1;
    MSG message{};
    int result{};
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (main_window_.accelerator() && TranslateAcceleratorW(
                main_window_.handle(), main_window_.accelerator(), &message))
            continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return result < 0 ? 2 : static_cast<int>(message.wParam);
}

ui::MainWindow& Application::main_window() noexcept { return main_window_; }

}  // namespace markdownmay::app
