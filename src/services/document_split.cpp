#include "markdownmay/services/document_split.hpp"

#include "markdownmay/fileio/path_utils.hpp"
#include "markdownmay/fileio/text_encoding.hpp"
#include "markdownmay/markdown/markdown_parser.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <span>
#include <vector>

namespace markdownmay::services {
namespace {

std::atomic_uint64_t g_split_sequence{1};

bool EqualPath(const std::filesystem::path& left,
               const std::filesystem::path& right) noexcept {
    return _wcsicmp(left.c_str(), right.c_str()) == 0;
}

std::wstring LowerExtension(const std::filesystem::path& path) {
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](wchar_t value) { return static_cast<wchar_t>(towlower(value)); });
    return extension;
}

bool ExtensionMatches(document::DocumentKind kind,
                      const std::filesystem::path& source,
                      const std::filesystem::path& target) {
    const auto source_extension = LowerExtension(source);
    const auto target_extension = LowerExtension(target);
    if (kind == document::DocumentKind::plain_text) {
        return source_extension == L".txt" && target_extension == L".txt";
    }
    return (source_extension == L".md" || source_extension == L".markdown") &&
           target_extension == source_extension;
}

bool LegalBoundary(std::string_view source, std::uint64_t offset) noexcept {
    if (offset > source.size()) return false;
    const auto index = static_cast<std::size_t>(offset);
    if (index < source.size() &&
        (static_cast<unsigned char>(source[index]) & 0xc0U) == 0x80U) return false;
    return !(index > 0 && index < source.size() &&
             source[index - 1] == '\r' && source[index] == '\n');
}

std::filesystem::path TemporaryBeside(const std::filesystem::path& target) {
    return target.parent_path() /
        (target.filename().wstring() + L".markdownmay.split-" +
         std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(g_split_sequence.fetch_add(1)) + L".tmp");
}

ErrorCode WriteAndVerify(const std::filesystem::path& path,
                         std::string_view source,
                         fileio::TextEncoding encoding,
                         fileio::LineEnding line_ending) {
    const auto normalized = fileio::NormalizeLineEndings(source, line_ending);
    auto encoded = fileio::EncodeText(normalized, encoding);
    if (!encoded.is_ok()) return encoded.error();
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE) return ErrorCode::split_commit_failed;
    std::size_t offset = 0;
    bool success = true;
    while (offset < encoded.value().size()) {
        const auto count = static_cast<DWORD>((std::min)(
            encoded.value().size() - offset,
            static_cast<std::size_t>(0x7ffff000U)));
        DWORD written = 0;
        if (!WriteFile(handle, encoded.value().data() + offset, count, &written, nullptr) ||
            written != count) { success = false; break; }
        offset += written;
    }
    if (success) success = FlushFileBuffers(handle) != FALSE;
    CloseHandle(handle);
    if (!success) return ErrorCode::split_commit_failed;
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return ErrorCode::split_commit_failed;
    const auto size = input.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) != encoded.value().size())
        return ErrorCode::split_commit_failed;
    input.seekg(0);
    std::vector<std::byte> actual(encoded.value().size());
    input.read(reinterpret_cast<char*>(actual.data()),
               static_cast<std::streamsize>(actual.size()));
    return input.gcount() == static_cast<std::streamsize>(actual.size()) &&
                   actual == encoded.value()
        ? ErrorCode::ok : ErrorCode::split_commit_failed;
}

bool Remove(const std::filesystem::path& path) noexcept {
    std::error_code error;
    if (!std::filesystem::exists(path, error)) return !error;
    return std::filesystem::remove(path, error) && !error;
}

}  // namespace

Result<SplitPlan> DocumentSplitService::Prepare(const SplitRequest& request) {
    if (!LegalBoundary(request.snapshot.source, request.utf8_offset) ||
        request.line_ending == fileio::LineEnding::mixed) {
        return Result<SplitPlan>::failure(ErrorCode::split_invalid_position);
    }
    auto source = fileio::NormalizeAbsolutePath(request.source_path);
    auto first = fileio::NormalizeAbsolutePath(request.first_target);
    auto second = fileio::NormalizeAbsolutePath(request.second_target);
    if (!source.is_ok() || !first.is_ok() || !second.is_ok())
        return Result<SplitPlan>::failure(ErrorCode::split_target_conflict);
    std::error_code ignored;
    if (EqualPath(first.value(), second.value()) ||
        EqualPath(source.value(), first.value()) ||
        EqualPath(source.value(), second.value()) ||
        std::filesystem::exists(first.value(), ignored) ||
        std::filesystem::exists(second.value(), ignored) ||
        !ExtensionMatches(request.snapshot.kind, source.value(), first.value()) ||
        !ExtensionMatches(request.snapshot.kind, source.value(), second.value())) {
        return Result<SplitPlan>::failure(ErrorCode::split_target_conflict);
    }
    const auto index = static_cast<std::size_t>(request.utf8_offset);
    SplitPlan plan{
        request.snapshot.source.substr(0, index),
        request.snapshot.source.substr(index),
        first.value(), second.value(), request.encoding, request.line_ending,
        true, true};
    if (request.snapshot.kind == document::DocumentKind::markdown) {
        plan.first_candidate_valid =
            markdown::ParseMarkdown(plan.first_source, request.snapshot.source_revision) != nullptr;
        plan.second_candidate_valid =
            markdown::ParseMarkdown(plan.second_source, request.snapshot.source_revision) != nullptr;
    }
    return Result<SplitPlan>::success(std::move(plan));
}

ErrorCode DocumentSplitService::Commit(
    const SplitPlan& plan, bool candidate_warning_confirmed,
    BeforeSecondSplitCommit before_second_commit) {
    if (plan.requires_confirmation() && !candidate_warning_confirmed)
        return ErrorCode::split_candidate_warning;
    std::error_code ignored;
    if (std::filesystem::exists(plan.first_target, ignored) ||
        std::filesystem::exists(plan.second_target, ignored))
        return ErrorCode::split_target_conflict;
    const auto first_temp = TemporaryBeside(plan.first_target);
    const auto second_temp = TemporaryBeside(plan.second_target);
    auto cleanup_temporaries = [&] {
        const bool first_removed = Remove(first_temp);
        const bool second_removed = Remove(second_temp);
        return first_removed && second_removed;
    };
    if (WriteAndVerify(first_temp, plan.first_source, plan.encoding, plan.line_ending) !=
            ErrorCode::ok ||
        WriteAndVerify(second_temp, plan.second_source, plan.encoding, plan.line_ending) !=
            ErrorCode::ok) {
        return cleanup_temporaries() ? ErrorCode::split_commit_failed
                                     : ErrorCode::split_rollback_failed;
    }
    if (std::filesystem::exists(plan.first_target, ignored) ||
        std::filesystem::exists(plan.second_target, ignored)) {
        return cleanup_temporaries() ? ErrorCode::split_target_conflict
                                     : ErrorCode::split_rollback_failed;
    }
    if (!MoveFileExW(first_temp.c_str(), plan.first_target.c_str(),
                     MOVEFILE_WRITE_THROUGH)) {
        return cleanup_temporaries() ? ErrorCode::split_commit_failed
                                     : ErrorCode::split_rollback_failed;
    }
    ErrorCode checkpoint = ErrorCode::ok;
    if (before_second_commit) {
        try { checkpoint = before_second_commit(plan.first_target, plan.second_target); }
        catch (...) { checkpoint = ErrorCode::split_commit_failed; }
    }
    if (checkpoint != ErrorCode::ok ||
        std::filesystem::exists(plan.second_target, ignored) ||
        !MoveFileExW(second_temp.c_str(), plan.second_target.c_str(),
                     MOVEFILE_WRITE_THROUGH)) {
        const bool rolled_back = Remove(plan.first_target) && cleanup_temporaries();
        return rolled_back ? (checkpoint == ErrorCode::ok
                                  ? ErrorCode::split_commit_failed : checkpoint)
                           : ErrorCode::split_rollback_failed;
    }
    return ErrorCode::ok;
}

}  // namespace markdownmay::services
