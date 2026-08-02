#pragma once

#include <cstdint>

namespace markdownmay::app {

enum class CommandId : std::uint16_t {
    file_new = 100,
    file_open,
    file_save,
    file_save_as,
    file_exit,
    edit_undo = 200,
    edit_redo,
    edit_cut,
    edit_copy,
    edit_paste,
    edit_select_all,
    edit_find,
    edit_replace,
    format_bold = 300,
    format_italic,
    format_strike,
    format_inline_code,
    format_quote,
    format_unordered_list,
    format_ordered_list,
    format_task_list,
    view_render = 400,
    view_source,
    view_split,
    view_theme_system,
    view_theme_light,
    view_theme_dark,
    help_about = 500,
    recent_first = 600,
    recent_last = 619,
    recent_clear = 620,
    tools_register_association = 700,
    tools_unregister_association,
    tools_default_apps,
};

struct CommandState final {
    bool enabled{};
    bool checked{};
};

}  // namespace markdownmay::app
