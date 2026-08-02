#pragma once

#include "markdownmay/app/command_id.hpp"

#include <windows.h>

#include <functional>
#include <filesystem>
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
    [[nodiscard]] HACCEL accelerator() const noexcept;

private:
    void AddCommand(HMENU menu, app::CommandId command, const wchar_t* text);

    Query query_;
    Execute execute_;
    HWND window_{};
    HMENU menu_{};
    HACCEL accelerator_{};
    HMENU recent_menu_{};
};

}  // namespace markdownmay::ui
