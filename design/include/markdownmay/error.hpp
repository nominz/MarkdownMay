#pragma once

#include "markdownmay/common.hpp"
#include "markdownmay/error_codes.hpp"

#include <source_location>
#include <utility>
#include <variant>

namespace markdownmay {

enum class Subsystem : std::uint8_t {
    app,
    ui,
    editor,
    document,
    markdown,
    fileio,
    image,
    pdf,
    docx,
    platform,
    services,
};

enum class Severity : std::uint8_t {
    info,
    warning,
    error,
    fatal,
};

struct Error final {
    ErrorCode code{};
    Severity severity{Severity::error};
    Subsystem subsystem{Subsystem::app};
    std::uint32_t system_code{};
    std::string detail_key;
    std::source_location source = std::source_location::current();
};

class Status final {
public:
    [[nodiscard]] static Status ok() noexcept;
    [[nodiscard]] static Status failure(Error error) noexcept;
    [[nodiscard]] bool is_ok() const noexcept;
    [[nodiscard]] const Error& error() const;

private:
    std::variant<std::monostate, Error> value_;
};

template <typename T>
class Result final {
public:
    [[nodiscard]] static Result success(T value) noexcept;
    [[nodiscard]] static Result failure(Error error) noexcept;
    [[nodiscard]] bool is_ok() const noexcept;
    [[nodiscard]] const T& value() const&;
    [[nodiscard]] T&& value() &&;
    [[nodiscard]] const Error& error() const;

private:
    std::variant<T, Error> value_;
};

}  // namespace markdownmay
