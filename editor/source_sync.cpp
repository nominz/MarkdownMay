#include "markdownmay/editor/source_sync.hpp"

#include "markdownmay/fileio/text_encoding.hpp"

#include <algorithm>

namespace markdownmay::editor {
namespace {

std::uint64_t FirstInvalidUtf8(std::string_view value) {
    for (std::size_t at = 0; at < value.size();) {
        const auto first = static_cast<unsigned char>(value[at]);
        std::size_t length{};
        if (first <= 0x7f) length = 1;
        else if (first >= 0xc2 && first <= 0xdf) length = 2;
        else if (first >= 0xe0 && first <= 0xef) length = 3;
        else if (first >= 0xf0 && first <= 0xf4) length = 4;
        else return at;
        if (at + length > value.size()) return at;
        for (std::size_t index = 1; index < length; ++index) {
            if ((static_cast<unsigned char>(value[at + index]) & 0xc0U) != 0x80U) return at;
        }
        if (length == 3) {
            const auto second = static_cast<unsigned char>(value[at + 1]);
            if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0)) return at;
        } else if (length == 4) {
            const auto second = static_cast<unsigned char>(value[at + 1]);
            if ((first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90)) return at;
        }
        at += length;
    }
    return value.size();
}

}  // namespace

SourceSync::SourceSync(document::DocumentSession& session) : session_(session) {}

ErrorCode SourceSync::synchronize(std::string source) {
    const auto snapshot = session_.snapshot();
    if (source == snapshot.source) {
        diagnostics_.clear();
        return ErrorCode::ok;
    }
    if (!fileio::IsValidUtf8(source)) {
        SetDiagnostic(ErrorCode::file_encoding_invalid, source,
            FirstInvalidUtf8(source), "源码不是有效的 UTF-8");
        return ErrorCode::file_encoding_invalid;
    }
    document::EditTransaction transaction{next_transaction_++, snapshot.source_revision,
        document::EditOrigin::source_view,
        {{{0, static_cast<std::uint64_t>(snapshot.source.size())}, std::move(source)}}};
    const auto result = session_.commit(transaction);
    if (result != ErrorCode::ok) {
        const auto current = session_.snapshot().source;
        SetDiagnostic(result, current, 0, "Markdown 源码无法同步");
        return result;
    }
    const auto updated = session_.snapshot();
    if (!updated.semantic || updated.parsed_revision != updated.source_revision) {
        SetDiagnostic(ErrorCode::markdown_parse_failed, updated.source, 0,
            "Markdown 解析失败");
        return ErrorCode::markdown_parse_failed;
    }
    diagnostics_.clear();
    return ErrorCode::ok;
}

ErrorCode SourceSync::save(const std::filesystem::path& target,
                           fileio::TextEncoding encoding,
                           fileio::LineEnding line_ending) {
    const auto snapshot = session_.snapshot();
    const auto result = fileio::SaveTextFileAtomic(
        {target, snapshot.source, encoding, line_ending});
    if (result != ErrorCode::ok) return result;
    return session_.mark_saved(snapshot.source_revision);
}

const std::vector<SourceDiagnostic>& SourceSync::diagnostics() const noexcept {
    return diagnostics_;
}

bool SourceSync::valid() const noexcept { return diagnostics_.empty(); }

void SourceSync::SetDiagnostic(ErrorCode code, std::string_view source,
                               std::uint64_t offset, std::string message) {
    offset = (std::min)(offset, static_cast<std::uint64_t>(source.size()));
    std::uint64_t line = 1;
    std::uint64_t column = 1;
    for (std::uint64_t index = 0; index < offset; ++index) {
        if (source[static_cast<std::size_t>(index)] == '\n') { ++line; column = 1; }
        else ++column;
    }
    diagnostics_ = {{code, offset,
        (std::min)(offset + 1, static_cast<std::uint64_t>(source.size())),
        line, column, std::move(message)}};
}

}  // namespace markdownmay::editor
