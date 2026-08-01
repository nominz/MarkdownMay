#pragma once

#include "markdownmay/editor/view_mode_controller.hpp"

#include <windows.h>

namespace markdownmay::ui {

class DocumentWindow final {
public:
    explicit DocumentWindow(document::DocumentSession& session);
    [[nodiscard]] ErrorCode create(HWND parent, const RECT& bounds);
    void resize(const RECT& bounds);
    [[nodiscard]] HWND handle() const noexcept;
    [[nodiscard]] editor::ViewModeController& modes() noexcept;

private:
    editor::ViewModeController modes_;
};

}  // namespace markdownmay::ui
