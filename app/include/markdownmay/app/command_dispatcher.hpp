#pragma once

#include "markdownmay/app/command_id.hpp"
#include "markdownmay/core/result.hpp"
#include "markdownmay/ui/document_window.hpp"
#include "markdownmay/ui/theme.hpp"

#include <functional>

namespace markdownmay::app {

class CommandDispatcher final {
public:
    struct FileCommands final {
        std::function<bool()> can_replace;
        std::function<ErrorCode()> new_document;
        std::function<ErrorCode()> open_document;
        std::function<ErrorCode()> save_document;
        std::function<ErrorCode()> save_document_as;
        std::function<ErrorCode()> print_document;
        std::function<ErrorCode()> page_setup;
        std::function<ErrorCode()> export_document;
        std::function<ErrorCode(std::size_t)> open_recent;
        std::function<ErrorCode()> clear_recent;
    };
    struct AssociationCommands final {
        std::function<bool()> can_register;
        std::function<bool()> can_unregister;
        std::function<ErrorCode()> register_application;
        std::function<ErrorCode()> unregister_application;
        std::function<ErrorCode()> open_default_apps;
        std::function<ErrorCode()> place_application;
    };
    struct AppearanceCommands final {
        std::function<ui::ThemePreference()> preference;
        std::function<void(ui::ThemePreference)> set_preference;
    };

    CommandDispatcher(ui::DocumentWindow& document_window,
                      std::function<void()> request_exit,
                      FileCommands file_commands = {},
                      AssociationCommands association_commands = {},
                      AppearanceCommands appearance_commands = {});
    [[nodiscard]] CommandState query(CommandId command) const noexcept;
    [[nodiscard]] ErrorCode execute(CommandId command);

private:
    ui::DocumentWindow& document_window_;
    std::function<void()> request_exit_;
    FileCommands file_commands_;
    AssociationCommands association_commands_;
    AppearanceCommands appearance_commands_;
};

}  // namespace markdownmay::app
