#pragma once

#include <cstdint>
#include <optional>
#include <utility>

namespace markdownmay {

enum class ErrorCode : std::uint32_t {
    ok = 0,
    file_not_found = 4001,
    file_too_large = 4003,
    file_read_failed = 4004,
    file_encoding_unsupported = 4005,
    file_encoding_invalid = 4006,
    file_write_failed = 4007,
    document_invalid_state = 3001,
    document_revision_mismatch = 3002,
    document_invariant_failed = 3004,
    settings_load_failed = 8001,
    settings_save_failed = 8002,
    settings_corrupt = 8003,
    recovery_write_failed = 8101,
    recovery_read_failed = 8102,
    recovery_discard_failed = 8103,
    log_write_failed = 8201,
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
