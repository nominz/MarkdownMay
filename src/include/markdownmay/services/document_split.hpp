#pragma once

#include "markdownmay/document/document_session.hpp"
#include "markdownmay/fileio/line_endings.hpp"
#include "markdownmay/fileio/text_encoding.hpp"

#include <filesystem>
#include <functional>
#include <string>

namespace markdownmay::services {

struct SplitRequest final {
    document::SessionSnapshot snapshot;
    std::filesystem::path source_path;
    std::filesystem::path first_target;
    std::filesystem::path second_target;
    std::uint64_t utf8_offset{};
    fileio::TextEncoding encoding{fileio::TextEncoding::utf8};
    fileio::LineEnding line_ending{fileio::LineEnding::crlf};
};

struct SplitPlan final {
    std::string first_source;
    std::string second_source;
    std::filesystem::path first_target;
    std::filesystem::path second_target;
    fileio::TextEncoding encoding{fileio::TextEncoding::utf8};
    fileio::LineEnding line_ending{fileio::LineEnding::crlf};
    bool first_candidate_valid{};
    bool second_candidate_valid{};

    [[nodiscard]] bool requires_confirmation() const noexcept {
        return !first_candidate_valid || !second_candidate_valid;
    }
};

using BeforeSecondSplitCommit = std::function<ErrorCode(
    const std::filesystem::path& first_target,
    const std::filesystem::path& second_target)>;

class DocumentSplitService final {
public:
    [[nodiscard]] static Result<SplitPlan> Prepare(const SplitRequest& request);
    [[nodiscard]] static ErrorCode Commit(
        const SplitPlan& plan,
        bool candidate_warning_confirmed = false,
        BeforeSecondSplitCommit before_second_commit = {});
};

}  // namespace markdownmay::services
