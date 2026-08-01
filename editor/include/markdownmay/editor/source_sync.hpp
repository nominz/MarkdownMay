#pragma once

#include "markdownmay/document/document_session.hpp"
#include "markdownmay/fileio/file_service.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace markdownmay::editor {

struct SourceDiagnostic final {
    ErrorCode code{ErrorCode::markdown_parse_failed};
    std::uint64_t begin{};
    std::uint64_t end{};
    std::uint64_t line{1};
    std::uint64_t column{1};
    std::string message;
};

class SourceSync final {
public:
    explicit SourceSync(document::DocumentSession& session);

    [[nodiscard]] ErrorCode synchronize(std::string source);
    [[nodiscard]] ErrorCode save(const std::filesystem::path& target,
        fileio::TextEncoding encoding, fileio::LineEnding line_ending);
    [[nodiscard]] const std::vector<SourceDiagnostic>& diagnostics() const noexcept;
    [[nodiscard]] bool valid() const noexcept;

private:
    void SetDiagnostic(ErrorCode code, std::string_view source,
                       std::uint64_t offset, std::string message);
    document::DocumentSession& session_;
    std::uint64_t next_transaction_{1};
    std::vector<SourceDiagnostic> diagnostics_;
};

}  // namespace markdownmay::editor
