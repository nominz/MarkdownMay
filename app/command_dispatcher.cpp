#include "markdownmay/app/command_dispatcher.hpp"

#include <windows.h>

#include <utility>

namespace markdownmay::app {

CommandDispatcher::CommandDispatcher(ui::DocumentWindow& document_window,
                                     std::function<void()> request_exit,
                                     FileCommands file_commands)
    : document_window_(document_window), request_exit_(std::move(request_exit)),
      file_commands_(std::move(file_commands)) {}

CommandState CommandDispatcher::query(CommandId command) const noexcept {
    const auto& modes = document_window_.modes();
    switch (command) {
    case CommandId::file_new:
    case CommandId::file_open:
        return {file_commands_.can_replace && file_commands_.can_replace(), false};
    case CommandId::file_save:
        return {static_cast<bool>(file_commands_.save_document), false};
    case CommandId::file_save_as:
        return {static_cast<bool>(file_commands_.save_document_as), false};
    case CommandId::edit_find:
    case CommandId::edit_replace:
        return {};
    case CommandId::edit_undo:
        return {modes.can_undo(), false};
    case CommandId::edit_redo:
        return {modes.can_redo(), false};
    case CommandId::format_bold:
    case CommandId::format_italic:
    case CommandId::format_strike:
    case CommandId::format_inline_code:
    case CommandId::format_quote:
    case CommandId::format_unordered_list:
    case CommandId::format_ordered_list:
    case CommandId::format_task_list:
        return {modes.mode() == editor::ViewMode::render, false};
    case CommandId::view_render:
        return {true, modes.mode() == editor::ViewMode::render};
    case CommandId::view_source:
        return {true, modes.mode() == editor::ViewMode::source};
    case CommandId::view_split:
        return {true, modes.mode() == editor::ViewMode::split};
    default:
        return {true, false};
    }
}

ErrorCode CommandDispatcher::execute(CommandId command) {
    if (!query(command).enabled) return ErrorCode::document_invalid_state;
    auto& modes = document_window_.modes();
    switch (command) {
    case CommandId::file_new:
        return file_commands_.new_document
            ? file_commands_.new_document() : ErrorCode::document_invalid_state;
    case CommandId::file_open:
        return file_commands_.open_document
            ? file_commands_.open_document() : ErrorCode::document_invalid_state;
    case CommandId::file_save:
        return file_commands_.save_document
            ? file_commands_.save_document() : ErrorCode::document_invalid_state;
    case CommandId::file_save_as:
        return file_commands_.save_document_as
            ? file_commands_.save_document_as() : ErrorCode::document_invalid_state;
    case CommandId::file_exit:
        if (request_exit_) request_exit_();
        return ErrorCode::ok;
    case CommandId::edit_undo: return modes.undo();
    case CommandId::edit_redo: return modes.redo();
    case CommandId::edit_cut: return modes.cut();
    case CommandId::edit_copy: return modes.copy();
    case CommandId::edit_paste: return modes.paste();
    case CommandId::edit_select_all: return modes.select_all();
    case CommandId::format_bold: return modes.execute(editor::EditorCommand::bold);
    case CommandId::format_italic: return modes.execute(editor::EditorCommand::italic);
    case CommandId::format_strike: return modes.execute(editor::EditorCommand::strike);
    case CommandId::format_inline_code:
        return modes.execute(editor::EditorCommand::inline_code);
    case CommandId::format_quote: return modes.execute(editor::EditorCommand::quote);
    case CommandId::format_unordered_list:
        return modes.execute(editor::EditorCommand::unordered_list);
    case CommandId::format_ordered_list:
        return modes.execute(editor::EditorCommand::ordered_list);
    case CommandId::format_task_list:
        return modes.execute(editor::EditorCommand::task_list);
    case CommandId::view_render: return modes.switch_to(editor::ViewMode::render);
    case CommandId::view_source: return modes.switch_to(editor::ViewMode::source);
    case CommandId::view_split: return modes.switch_to(editor::ViewMode::split);
    case CommandId::help_about:
        MessageBoxW(document_window_.handle(),
            L"马冬梅（Markdown May）\n轻量、免费、开源的 Markdown 编辑器",
            L"关于马冬梅", MB_OK | MB_ICONINFORMATION);
        return ErrorCode::ok;
    default:
        return ErrorCode::document_invalid_state;
    }
}

}  // namespace markdownmay::app
