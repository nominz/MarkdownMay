#pragma once

#include "markdownmay/app/command_id.hpp"

#include <windows.h>

#include <functional>
#include <commctrl.h>

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
    [[nodiscard]] LRESULT custom_draw(NMTBCUSTOMDRAW& draw);
    [[nodiscard]] static const wchar_t* tooltip(std::uint16_t command) noexcept;

private:
    Query query_;
    HWND handle_{};
    int height_{34};
    HFONT font_{};
    HFONT icon_font_{};
    COLORREF text_color_{RGB(24, 24, 24)};
    COLORREF background_color_{RGB(249, 249, 249)};
    UINT dpi_{96};
};

}  // namespace markdownmay::ui
