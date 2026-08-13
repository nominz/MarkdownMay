#include "markdownmay/app/command_dispatcher.hpp"

#include <windows.h>

#include <utility>

namespace markdownmay::app {

CommandDispatcher::CommandDispatcher(ui::DocumentWindow& document_window,
                                     std::function<void()> request_exit,
                                     FileCommands file_commands,
                                     AssociationCommands association_commands,
                                     AppearanceCommands appearance_commands)
    : document_window_(document_window), request_exit_(std::move(request_exit)),
      file_commands_(std::move(file_commands)),
      association_commands_(std::move(association_commands)),
      appearance_commands_(std::move(appearance_commands)) {}

CommandState CommandDispatcher::query(CommandId command) const noexcept {
    const auto native = static_cast<std::uint16_t>(command);
    if (native >= static_cast<std::uint16_t>(CommandId::recent_first) &&
        native <= static_cast<std::uint16_t>(CommandId::recent_last))
        return {static_cast<bool>(file_commands_.open_recent), false};
    if (command == CommandId::recent_clear)
        return {static_cast<bool>(file_commands_.clear_recent), false};
    if (command == CommandId::tools_register_association)
        return {association_commands_.can_register &&
            association_commands_.can_register(), false};
    if (command == CommandId::tools_unregister_association)
        return {association_commands_.can_unregister &&
            association_commands_.can_unregister(), false};
    if (command == CommandId::tools_default_apps)
        return {static_cast<bool>(association_commands_.open_default_apps), false};
    if (command == CommandId::tools_settings) return {false, false};
    if (command >= CommandId::view_theme_system && command <= CommandId::view_theme_dark) {
        const auto preference = appearance_commands_.preference
            ? appearance_commands_.preference() : ui::ThemePreference::follow_system;
        const auto expected = command == CommandId::view_theme_light
            ? ui::ThemePreference::light : command == CommandId::view_theme_dark
            ? ui::ThemePreference::dark : ui::ThemePreference::follow_system;
        return {static_cast<bool>(appearance_commands_.set_preference), preference == expected};
    }
    const auto& modes = document_window_.modes();
    const bool markdown = modes.supports_markdown_commands();
    switch (command) {
    case CommandId::file_new:
    case CommandId::file_open:
        return {file_commands_.can_replace && file_commands_.can_replace(), false};
    case CommandId::file_save:
        return {static_cast<bool>(file_commands_.save_document), false};
    case CommandId::file_save_as:
        return {static_cast<bool>(file_commands_.save_document_as), false};
    case CommandId::file_print:
        return {markdown && static_cast<bool>(file_commands_.print_document), false};
    case CommandId::file_page_setup:
        return {markdown && static_cast<bool>(file_commands_.page_setup), false};
    case CommandId::file_export:
        return {markdown && static_cast<bool>(file_commands_.export_document), false};
    case CommandId::edit_find:
    case CommandId::edit_replace:
        return {true, false};
    case CommandId::edit_undo:
        return {modes.can_undo(), false};
    case CommandId::edit_redo:
        return {modes.can_redo(), false};
    case CommandId::format_bold:
        return {markdown && modes.mode() == editor::ViewMode::render,
            modes.inline_active(editor::InlineFormat::bold)};
    case CommandId::format_italic:
        return {markdown && modes.mode() == editor::ViewMode::render,
            modes.inline_active(editor::InlineFormat::italic)};
    case CommandId::format_strike:
        return {markdown && modes.mode() == editor::ViewMode::render,
            modes.inline_active(editor::InlineFormat::strike)};
    case CommandId::format_inline_code:
    case CommandId::format_quote:
    case CommandId::format_unordered_list:
    case CommandId::format_ordered_list:
    case CommandId::format_task_list:
        return {markdown && modes.mode() == editor::ViewMode::render, false};
    case CommandId::format_body:
    case CommandId::format_heading1:
    case CommandId::format_heading2:
    case CommandId::format_heading3:
    case CommandId::format_heading4:
    case CommandId::format_heading5:
    case CommandId::format_heading6: {
        const auto level = command == CommandId::format_body ? 0 :
            static_cast<std::uint8_t>(native - static_cast<std::uint16_t>(CommandId::format_heading1) + 1);
        return {markdown && modes.mode() == editor::ViewMode::render,
            modes.mode() == editor::ViewMode::render && modes.current_heading_level() == level};
    }
    case CommandId::view_render:
        return {markdown, modes.mode() == editor::ViewMode::render};
    case CommandId::view_source:
        return {true, modes.mode() == editor::ViewMode::source};
    case CommandId::view_split:
        return {markdown, modes.mode() == editor::ViewMode::split};
    case CommandId::view_outline:
        return {markdown, document_window_.outline_visible()};
    default:
        return {true, false};
    }
}

ErrorCode CommandDispatcher::execute(CommandId command) {
    if (!query(command).enabled) return ErrorCode::document_invalid_state;
    auto& modes = document_window_.modes();
    const auto native = static_cast<std::uint16_t>(command);
    if (native >= static_cast<std::uint16_t>(CommandId::recent_first) &&
        native <= static_cast<std::uint16_t>(CommandId::recent_last))
        return file_commands_.open_recent
            ? file_commands_.open_recent(native - static_cast<std::uint16_t>(CommandId::recent_first))
            : ErrorCode::document_invalid_state;
    if (command == CommandId::recent_clear)
        return file_commands_.clear_recent ? file_commands_.clear_recent()
                                           : ErrorCode::document_invalid_state;
    if (command == CommandId::tools_register_association)
        return association_commands_.register_application
            ? association_commands_.register_application()
            : ErrorCode::document_invalid_state;
    if (command == CommandId::tools_unregister_association)
        return association_commands_.unregister_application
            ? association_commands_.unregister_application()
            : ErrorCode::document_invalid_state;
    if (command == CommandId::tools_default_apps)
        return association_commands_.open_default_apps
            ? association_commands_.open_default_apps()
            : ErrorCode::document_invalid_state;
    if (command >= CommandId::view_theme_system && command <= CommandId::view_theme_dark) {
        const auto preference = command == CommandId::view_theme_light
            ? ui::ThemePreference::light : command == CommandId::view_theme_dark
            ? ui::ThemePreference::dark : ui::ThemePreference::follow_system;
        if (appearance_commands_.set_preference) appearance_commands_.set_preference(preference);
        return appearance_commands_.set_preference ? ErrorCode::ok : ErrorCode::document_invalid_state;
    }
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
    case CommandId::file_print:
        return file_commands_.print_document
            ? file_commands_.print_document() : ErrorCode::document_invalid_state;
    case CommandId::file_page_setup:
        return file_commands_.page_setup
            ? file_commands_.page_setup() : ErrorCode::document_invalid_state;
    case CommandId::file_export:
        return file_commands_.export_document
            ? file_commands_.export_document() : ErrorCode::document_invalid_state;
    case CommandId::file_exit:
        if (request_exit_) request_exit_();
        return ErrorCode::ok;
    case CommandId::edit_undo: return modes.undo();
    case CommandId::edit_redo: return modes.redo();
    case CommandId::edit_cut: return modes.cut();
    case CommandId::edit_copy: return modes.copy();
    case CommandId::edit_paste: return modes.paste();
    case CommandId::edit_select_all: return modes.select_all();
    case CommandId::edit_find:
        document_window_.toggle_find(false);
        return ErrorCode::ok;
    case CommandId::edit_replace:
        document_window_.show_find(true);
        return ErrorCode::ok;
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
    case CommandId::format_body:
    case CommandId::format_heading1:
    case CommandId::format_heading2:
    case CommandId::format_heading3:
    case CommandId::format_heading4:
    case CommandId::format_heading5:
    case CommandId::format_heading6: {
        const auto heading_command = static_cast<std::uint16_t>(command);
        const auto body = static_cast<std::uint16_t>(CommandId::format_body);
        const auto first = static_cast<std::uint16_t>(CommandId::format_heading1);
        const auto level = heading_command == body ? 0U : heading_command - first + 1U;
        const auto result = modes.render_view().set_heading(
            static_cast<std::uint8_t>(level));
        if (result == ErrorCode::ok) SetFocus(modes.render_view().handle());
        return result;
    }
    case CommandId::view_render: return modes.switch_to(editor::ViewMode::render);
    case CommandId::view_source: return modes.switch_to(editor::ViewMode::source);
    case CommandId::view_split: return modes.switch_to(editor::ViewMode::split);
    case CommandId::view_outline:
        document_window_.toggle_outline();
        return ErrorCode::ok;
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
