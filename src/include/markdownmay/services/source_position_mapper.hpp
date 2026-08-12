#pragma once

#include "markdownmay/core/result.hpp"
#include "markdownmay/document/document_session.hpp"

#include <cstdint>

namespace markdownmay::services {

struct SourcePosition final {
    std::uint64_t utf8_byte{};
    std::uint64_t revision{};
};

class SourcePositionMapper final {
public:
    [[nodiscard]] static Result<SourcePosition> MapCaret(
        const document::SessionSnapshot& snapshot,
        std::uint64_t surface_revision,
        std::uint64_t anchor,
        std::uint64_t caret) noexcept;
};

}  // namespace markdownmay::services
