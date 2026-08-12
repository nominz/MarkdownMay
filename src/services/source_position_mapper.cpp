#include "markdownmay/services/source_position_mapper.hpp"

namespace markdownmay::services {
namespace {

bool IsContinuation(unsigned char value) noexcept {
    return (value & 0xc0U) == 0x80U;
}

bool IsLegalBoundary(const std::string& source, std::uint64_t offset) noexcept {
    if (offset > source.size()) return false;
    const auto index = static_cast<std::size_t>(offset);
    if (index < source.size() &&
        IsContinuation(static_cast<unsigned char>(source[index]))) return false;
    return !(index > 0 && index < source.size() &&
             source[index - 1] == '\r' && source[index] == '\n');
}

}  // namespace

Result<SourcePosition> SourcePositionMapper::MapCaret(
    const document::SessionSnapshot& snapshot,
    std::uint64_t surface_revision,
    std::uint64_t anchor,
    std::uint64_t caret) noexcept {
    if (surface_revision != snapshot.source_revision) {
        return Result<SourcePosition>::failure(
            ErrorCode::document_revision_mismatch);
    }
    if (anchor != caret || !IsLegalBoundary(snapshot.source, caret)) {
        return Result<SourcePosition>::failure(
            ErrorCode::editor_selection_mapping_failed);
    }
    return Result<SourcePosition>::success({caret, snapshot.source_revision});
}

}  // namespace markdownmay::services
