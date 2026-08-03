#pragma once

#include "markdownmay/app/command_id.hpp"

#include <windows.h>

#include <functional>

namespace markdownmay::ui {

class Toolbar final {
public:
    using Query = std::function<app::CommandState(app::CommandId)>;

    explicit Toolbar(Query query);
    ~Toolbar();
    [[nodiscard]] bool create(HWND parent);
    void resize(int width, int top = 0);
    void refresh();
    [[nodiscard]] HWND handle() const noexcept;
    [[nodiscard]] int height() const noexcept;
    void apply_appearance(COLORREF text, COLORREF background, UINT dpi);
    [[nodiscard]] static const wchar_t* tooltip(std::uint16_t command) noexcept;

private:
    Query query_;
    HWND handle_{};
    int height_{34};
    HFONT font_{};
};

}  // namespace markdownmay::ui
