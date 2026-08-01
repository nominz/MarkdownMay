#include "markdownmay/ui/document_window.hpp"

namespace markdownmay::ui {

DocumentWindow::DocumentWindow(document::DocumentSession& session) : modes_(session) {}

ErrorCode DocumentWindow::create(HWND parent, const RECT& bounds) {
    return modes_.create(parent, bounds);
}

void DocumentWindow::resize(const RECT& bounds) {
    if (!modes_.handle()) return;
    MoveWindow(modes_.handle(), bounds.left, bounds.top,
        bounds.right - bounds.left, bounds.bottom - bounds.top, TRUE);
}

HWND DocumentWindow::handle() const noexcept { return modes_.handle(); }
editor::ViewModeController& DocumentWindow::modes() noexcept { return modes_; }

}  // namespace markdownmay::ui
