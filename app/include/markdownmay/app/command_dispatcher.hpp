#pragma once

#include "markdownmay/app/command_id.hpp"
#include "markdownmay/core/result.hpp"
#include "markdownmay/ui/document_window.hpp"

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
    };

    CommandDispatcher(ui::DocumentWindow& document_window,
                      std::function<void()> request_exit,
                      FileCommands file_commands = {});
    [[nodiscard]] CommandState query(CommandId command) const noexcept;
    [[nodiscard]] ErrorCode execute(CommandId command);

private:
    ui::DocumentWindow& document_window_;
    std::function<void()> request_exit_;
    FileCommands file_commands_;
};

}  // namespace markdownmay::app
