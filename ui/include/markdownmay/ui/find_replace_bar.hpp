#pragma once

#include "markdownmay/editor/view_mode_controller.hpp"

#include <windows.h>

namespace markdownmay::ui {

class FindReplaceBar final {
public:
    explicit FindReplaceBar(editor::ViewModeController& modes) : modes_(modes) {}
    ~FindReplaceBar();
    [[nodiscard]] bool create(HWND parent);
    void show(bool replace_mode);
    void hide();
    void resize(const RECT& bounds);
    [[nodiscard]] bool visible() const noexcept;
    [[nodiscard]] int height() const noexcept;
    [[nodiscard]] bool handle_control(HWND control, std::uint16_t notification);
    void apply_appearance(COLORREF text, COLORREF background, UINT dpi);

private:
    [[nodiscard]] std::string ReadUtf8(HWND control) const;
    void SetStatus(const wchar_t* text);
    void FindNext();
    void InsertSpecial(const wchar_t* token);

    editor::ViewModeController& modes_;
    HWND parent_{};
    HWND bar_{};
    HWND find_edit_{};
    HWND replace_label_{};
    HWND replace_edit_{};
    HWND find_button_{};
    HWND replace_button_{};
    HWND replace_all_button_{};
    HWND case_box_{};
    HWND wildcard_box_{};
    HWND special_button_{};
    HWND close_button_{};
    HWND status_{};
    HFONT font_{};
    UINT dpi_{96};
    bool replace_mode_{};
};

}  // namespace markdownmay::ui
