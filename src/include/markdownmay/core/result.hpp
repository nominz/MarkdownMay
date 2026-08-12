#pragma once

#include <cstdint>
#include <optional>
#include <utility>

namespace markdownmay {

enum class ErrorCode : std::uint32_t {
    ok = 0,
    editor_render_projection_failed = 2001,
    editor_source_control_failed = 2002,
    editor_split_control_failed = 2003,
    editor_cannot_enter_render_mode = 2004,
    editor_selection_mapping_failed = 2006,
    editor_transaction_conflict = 2007,
    editor_undo_failed = 2009,
    editor_unmapped_rich_edit_change = 2011,
    app_invalid_command_line = 1002,
    file_not_found = 4001,
    file_too_large = 4003,
    file_read_failed = 4004,
    file_encoding_unsupported = 4005,
    file_encoding_invalid = 4006,
    file_write_failed = 4007,
    file_read_only = 4014,
    platform_registry_access_denied = 7001,
    platform_association_write_failed = 7002,
    platform_association_remove_failed = 7003,
    platform_default_apps_ui_failed = 7004,
    platform_single_instance_failed = 7101,
    platform_ipc_invalid_message = 7102,
    platform_ipc_send_failed = 7103,
    platform_print_failed = 7301,
    document_invalid_state = 3001,
    document_revision_mismatch = 3002,
    document_invariant_failed = 3004,
    markdown_parse_failed = 3501,
    settings_load_failed = 8001,
    settings_save_failed = 8002,
    settings_corrupt = 8003,
    recovery_write_failed = 8101,
    recovery_read_failed = 8102,
    recovery_discard_failed = 8103,
    log_write_failed = 8201,
    image_remote_blocked = 5005,
    image_import_failed = 5006,
    image_assets_path_unsafe = 5007,
    image_mark_deleted_failed = 5008,
    image_restore_name_conflict = 5009,
    export_revision_not_current = 6001,
    export_invalid_options = 6004,
};

template <typename T>
class Result final {
public:
    [[nodiscard]] static Result success(T value) {
        return Result(std::move(value), ErrorCode::ok);
    }
    [[nodiscard]] static Result failure(ErrorCode error) {
        return Result(std::nullopt, error);
    }
    [[nodiscard]] bool is_ok() const noexcept { return value_.has_value(); }
    [[nodiscard]] const T& value() const& { return value_.value(); }
    [[nodiscard]] T&& value() && { return std::move(value_).value(); }
    [[nodiscard]] ErrorCode error() const noexcept { return error_; }

private:
    Result(T value, ErrorCode error)
        : value_(std::move(value)), error_(error) {}
    Result(std::nullopt_t, ErrorCode error) : error_(error) {}

    std::optional<T> value_;
    ErrorCode error_{ErrorCode::ok};
};

}  // namespace markdownmay
