#pragma once

#include "markdownmay/app/command_id.hpp"

#include <windows.h>

#include <functional>
#include <filesystem>
#include <list>
#include <string>
#include <vector>

namespace markdownmay::ui {

class MenuController final {
public:
    using Query = std::function<app::CommandState(app::CommandId)>;
    using Execute = std::function<void(app::CommandId)>;

    MenuController(Query query, Execute execute);
    ~MenuController();
    MenuController(const MenuController&) = delete;
    MenuController& operator=(const MenuController&) = delete;

    [[nodiscard]] bool create(HWND window);
    [[nodiscard]] bool dispatch(std::uint16_t native_id);
    void refresh();
    void set_recent_files(std::vector<std::filesystem::path> files);
    void apply_appearance(COLORREF text, COLORREF surface, UINT dpi);
    [[nodiscard]] bool measure(MEASUREITEMSTRUCT& item) const;
    [[nodiscard]] bool draw(const DRAWITEMSTRUCT& item) const;
    [[nodiscard]] HACCEL accelerator() const noexcept;

private:
    void AddCommand(HMENU menu, app::CommandId command, const wchar_t* text);
    void AddPopup(HMENU menu, HMENU popup, const wchar_t* text);
    void AddSeparator(HMENU menu);
    const wchar_t* KeepLabel(std::wstring text);

    Query query_;
    Execute execute_;
    HWND window_{};
    HMENU menu_{};
    HACCEL accelerator_{};
    HMENU recent_menu_{};
    std::list<std::wstring> labels_;
    COLORREF text_color_{RGB(32, 32, 32)};
    COLORREF surface_color_{RGB(250, 250, 250)};
    UINT dpi_{96};
};

}  // namespace markdownmay::ui
