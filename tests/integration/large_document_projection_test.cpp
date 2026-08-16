#include "markdownmay/ui/main_window.hpp"

#include <windows.h>

#include <chrono>
#include <string>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    using namespace markdownmay;
    std::string source;
    source.reserve(60000);
    for (int section = 0; section < 120; ++section) {
        source += "## Section " + std::to_string(section) + "\n\n";
        source += "A paragraph with **bold**, *italic*, `code`, and [link](local.md).\n\n";
        source += "- first item\n- second item\n\n";
        if ((section % 12) == 0)
            source += "| A | B | C |\n|---|---|---|\n| one | two | three |\n\n";
        if ((section % 15) == 0)
            source += "```text\nplain fenced content\n```\n\n";
    }
    document::DocumentSession session("");
    ui::MainWindow window(session);
    if (window.create(instance, SW_SHOW) != ErrorCode::ok) return 1;
    const auto started = std::chrono::steady_clock::now();
    const auto result = window.document_window().modes().reload(std::move(source));
    const auto elapsed = std::chrono::steady_clock::now() - started;
    DestroyWindow(window.handle());
    if (result != ErrorCode::ok) return 2;
    return elapsed < std::chrono::seconds(10) ? 0 : 3;
}
