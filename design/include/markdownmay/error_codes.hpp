#pragma once

#include "markdownmay/common.hpp"

namespace markdownmay {

enum class ErrorCode : std::uint32_t {
    ok = 0,

    app_initialization_failed = 1001,
    app_invalid_command_line = 1002,
    app_background_task_failed = 1003,
    ui_window_creation_failed = 1501,
    ui_dialog_failed = 1502,

    editor_render_projection_failed = 2001,
    editor_source_control_failed = 2002,
    editor_split_control_failed = 2003,
    editor_cannot_enter_render_mode = 2004,
    editor_stale_parse_result = 2005,
    editor_selection_mapping_failed = 2006,
    editor_transaction_conflict = 2007,
    editor_transaction_rollback_failed = 2008,
    editor_undo_failed = 2009,
    editor_ime_commit_failed = 2010,
    editor_unmapped_rich_edit_change = 2011,

    document_invalid_state = 3001,
    document_revision_mismatch = 3002,
    document_node_not_found = 3003,
    document_invariant_failed = 3004,
    document_read_only = 3005,
    markdown_parse_failed = 3501,
    markdown_nesting_limit = 3502,
    markdown_serialize_failed = 3503,
    markdown_unknown_block_conflict = 3504,

    file_not_found = 4001,
    file_access_denied = 4002,
    file_too_large = 4003,
    file_read_failed = 4004,
    file_encoding_unsupported = 4005,
    file_encoding_invalid = 4006,
    file_write_failed = 4007,
    file_flush_failed = 4008,
    file_replace_failed = 4009,
    file_changed_externally = 4010,
    file_deleted_externally = 4011,
    file_disk_full = 4012,
    file_path_too_long = 4013,

    image_format_unsupported = 5001,
    image_decode_failed = 5002,
    image_too_large = 5003,
    image_missing = 5004,
    image_remote_blocked = 5005,
    image_import_failed = 5006,
    image_assets_path_unsafe = 5007,
    image_mark_deleted_failed = 5008,
    image_restore_name_conflict = 5009,

    export_revision_not_current = 6001,
    export_cancelled = 6002,
    export_target_failed = 6003,
    pdf_layout_failed = 6201,
    pdf_font_failed = 6202,
    pdf_write_failed = 6203,
    pdf_validation_failed = 6204,
    docx_xml_failed = 6401,
    docx_media_failed = 6402,
    docx_zip_failed = 6403,
    docx_validation_failed = 6404,

    platform_registry_access_denied = 7001,
    platform_association_write_failed = 7002,
    platform_association_remove_failed = 7003,
    platform_default_apps_ui_failed = 7004,
    platform_single_instance_failed = 7101,
    platform_ipc_invalid_message = 7102,
    platform_ipc_send_failed = 7103,
    platform_shell_open_failed = 7201,
    platform_print_failed = 7301,

    settings_load_failed = 8001,
    settings_save_failed = 8002,
    settings_corrupt = 8003,
    recovery_write_failed = 8101,
    recovery_read_failed = 8102,
    recovery_discard_failed = 8103,
    log_write_failed = 8201,

    fatal_unhandled_exception = 9001,
    fatal_out_of_memory = 9002,
    fatal_internal_invariant = 9003,
};

}  // namespace markdownmay
