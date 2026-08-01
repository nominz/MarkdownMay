#pragma once

#include "markdownmay/app/command_id.hpp"
#include "markdownmay/core/result.hpp"
#include "markdownmay/ui/document_window.hpp"

#include <functional>

namespace markdownmay::app {

class CommandDispatcher final {
public:
    CommandDispatcher(ui::DocumentWindow& document_window,
                      std::function<void()> request_exit);
    [[nodiscard]] CommandState query(CommandId command) const noexcept;
    [[nodiscard]] ErrorCode execute(CommandId command);

private:
    ui::DocumentWindow& document_window_;
    std::function<void()> request_exit_;
};

}  // namespace markdownmay::app
