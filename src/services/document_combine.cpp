#include "markdownmay/services/document_combine.hpp"

#include "markdownmay/fileio/file_service.hpp"
#include "markdownmay/fileio/path_utils.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_map>

namespace markdownmay::services {
namespace {

std::string PathUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
std::filesystem::path Utf8Path(std::string_view text) {
    std::u8string value;
    for (const unsigned char byte : text) value.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(value);
}
bool StartsWithInsensitive(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && std::equal(
        prefix.begin(), prefix.end(), text.begin(), [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });
}
bool ExternalTarget(std::string_view target) {
    return target.empty() || target[0] == '#' || target[0] == '/' ||
        target[0] == '\\' || target.find(':') != std::string_view::npos;
}
bool LegalBoundary(std::string_view source, std::uint64_t offset) {
    if (offset > source.size()) return false;
    const auto at = static_cast<std::size_t>(offset);
    if (at < source.size() &&
        (static_cast<unsigned char>(source[at]) & 0xc0U) == 0x80U) return false;
    return !(at && at < source.size() && source[at - 1] == '\r' && source[at] == '\n');
}
bool IsImageExtension(const std::filesystem::path& path) {
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
    return extension == L".png" || extension == L".jpg" || extension == L".jpeg" ||
           extension == L".gif" || extension == L".bmp" || extension == L".webp";
}
std::filesystem::path UniqueResourceTarget(
    const std::filesystem::path& assets, const std::filesystem::path& source,
    std::vector<InsertResource>& resources) {
    auto target = assets / source.filename();
    const auto occupied = [&](const std::filesystem::path& candidate) {
        return std::filesystem::exists(candidate) || std::any_of(
            resources.begin(), resources.end(), [&](const InsertResource& item) {
                return _wcsicmp(item.target.c_str(), candidate.c_str()) == 0;
            });
    };
    for (std::uint32_t suffix = 2; occupied(target); ++suffix) {
        target = assets / (source.stem().wstring() + L"_" +
            std::to_wstring(suffix) + source.extension().wstring());
    }
    return target;
}
std::string MinimalBoundary(std::string_view before, std::string insertion,
                            std::string_view after) {
    if (!before.empty() && !insertion.empty() && before.back() != '\n' &&
        insertion.front() != '\n') insertion.insert(insertion.begin(), '\n');
    if (!after.empty() && !insertion.empty() && insertion.back() != '\n' &&
        after.front() != '\n') insertion.push_back('\n');
    return insertion;
}

std::vector<std::pair<std::size_t, std::size_t>> CodeRanges(std::string_view source) {
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    bool fenced = false;
    char fence_char = 0;
    std::size_t fence_begin = 0;
    for (std::size_t line = 0; line < source.size();) {
        const auto line_end = source.find('\n', line);
        const auto end = line_end == std::string_view::npos ? source.size() : line_end + 1;
        auto marker = line;
        while (marker < end && (source[marker] == ' ' || source[marker] == '\t')) ++marker;
        if (marker + 2 < end &&
            ((source[marker] == '`' && source[marker + 1] == '`' && source[marker + 2] == '`') ||
             (source[marker] == '~' && source[marker + 1] == '~' && source[marker + 2] == '~'))) {
            if (!fenced) { fenced = true; fence_char = source[marker]; fence_begin = line; }
            else if (source[marker] == fence_char) {
                ranges.push_back({fence_begin, end}); fenced = false;
            }
        } else if (!fenced) {
            for (auto at = line; at < end;) {
                const auto open = source.find('`', at);
                if (open == std::string_view::npos || open >= end) break;
                const auto close = source.find('`', open + 1);
                if (close == std::string_view::npos || close >= end) break;
                ranges.push_back({open, close + 1}); at = close + 1;
            }
        }
        line = end;
    }
    if (fenced) ranges.push_back({fence_begin, source.size()});
    return ranges;
}

bool Protected(std::size_t position,
               const std::vector<std::pair<std::size_t, std::size_t>>& ranges) {
    return std::any_of(ranges.begin(), ranges.end(), [&](const auto& range) {
        return position >= range.first && position < range.second;
    });
}

std::string EscapePlainTextForMarkdown(std::string_view text) {
    std::string output;
    output.reserve(text.size());
    bool line_start = true;
    for (const char value : text) {
        if ((line_start && (value == '#' || value == '>' || value == '-' ||
                            value == '+' || value == '*')) ||
            value == '\\' || value == '`' || value == '[' || value == ']' ||
            value == '_' || value == '~') output.push_back('\\');
        output.push_back(value);
        line_start = value == '\n';
    }
    return output;
}

Result<std::string> RewriteMarkdown(
    std::string source, const std::filesystem::path& source_document,
    const std::filesystem::path& current_document,
    std::vector<InsertResource>& resources) {
    std::size_t search = 0;
    while ((search = source.find("](", search)) != std::string::npos) {
        if (Protected(search, CodeRanges(source))) { search += 2; continue; }
        const auto label_begin = source.rfind('[', search);
        const bool image = label_begin != std::string::npos && label_begin > 0 &&
                           source[label_begin - 1] == '!';
        const auto begin = search + 2;
        auto end = begin;
        bool escaped = false;
        for (; end < source.size(); ++end) {
            if (!escaped && source[end] == ')') break;
            escaped = !escaped && source[end] == '\\';
            if (source[end] != '\\') escaped = false;
        }
        if (end == source.size()) break;
        auto target_end = source.find_first_of(" \t\"'", begin);
        if (target_end == std::string::npos || target_end > end) target_end = end;
        const auto target = source.substr(begin, target_end - begin);
        if (!ExternalTarget(target)) {
            if (current_document.empty())
                return Result<std::string>::failure(ErrorCode::insert_target_unsaved);
            auto resolved = (source_document.parent_path() / Utf8Path(target)).lexically_normal();
            std::filesystem::path rewritten_path = resolved;
            if (image && IsImageExtension(resolved)) {
                if (!std::filesystem::is_regular_file(resolved))
                    return Result<std::string>::failure(ErrorCode::insert_resource_failed);
                const auto assets = fileio::AssetsDirectoryFor(current_document);
                rewritten_path = UniqueResourceTarget(assets, resolved, resources);
                resources.push_back({resolved, rewritten_path, true});
            }
            const auto base = current_document.parent_path().lexically_normal();
            rewritten_path = rewritten_path.lexically_normal();
            if (_wcsicmp(rewritten_path.root_name().c_str(), base.root_name().c_str()) != 0)
                return Result<std::string>::failure(ErrorCode::insert_path_rewrite_failed);
            auto relative = rewritten_path.lexically_relative(base);
            if (relative.empty()) return Result<std::string>::failure(
                ErrorCode::insert_path_rewrite_failed);
            const auto replacement = PathUtf8(relative);
            source.replace(begin, target_end - begin, replacement);
            end += replacement.size() - target.size();
        }
        search = end + 1;
    }
    return Result<std::string>::success(std::move(source));
}

bool RemoveCreated(const std::vector<std::filesystem::path>& paths) {
    bool success = true;
    for (auto at = paths.rbegin(); at != paths.rend(); ++at) {
        std::error_code error;
        if (std::filesystem::exists(*at, error) &&
            (!std::filesystem::remove(*at, error) || error)) success = false;
    }
    return success;
}
}  // namespace

Result<InsertPlan> DocumentCombineService::Prepare(const InsertRequest& request) {
    if (!LegalBoundary(request.snapshot.source, request.utf8_offset))
        return Result<InsertPlan>::failure(ErrorCode::insert_invalid_position);
    auto loaded = fileio::LoadTextFile(request.source_path);
    if (!loaded.is_ok()) return Result<InsertPlan>::failure(loaded.error());
    std::filesystem::path current_document;
    if (!request.current_document_path.empty()) {
        auto normalized = fileio::NormalizeAbsolutePath(request.current_document_path);
        if (!normalized.is_ok())
            return Result<InsertPlan>::failure(ErrorCode::insert_path_rewrite_failed);
        current_document = normalized.value();
    }
    auto extension = loaded.value().path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
    if (extension != L".md" && extension != L".markdown" && extension != L".txt")
        return Result<InsertPlan>::failure(ErrorCode::insert_source_unsupported);
    std::vector<InsertResource> resources;
    std::string insertion = loaded.value().source;
    if (extension != L".txt") {
        auto rewritten = RewriteMarkdown(std::move(insertion), loaded.value().path,
                                         current_document, resources);
        if (!rewritten.is_ok()) return Result<InsertPlan>::failure(rewritten.error());
        insertion = std::move(rewritten).value();
    } else if (request.snapshot.kind == document::DocumentKind::markdown) {
        insertion = EscapePlainTextForMarkdown(insertion);
    }
    const auto at = static_cast<std::size_t>(request.utf8_offset);
    insertion = MinimalBoundary(
        std::string_view(request.snapshot.source).substr(0, at),
        std::move(insertion),
        std::string_view(request.snapshot.source).substr(at));
    return Result<InsertPlan>::success({request.snapshot.source_revision,
        request.utf8_offset, std::move(insertion), std::move(resources)});
}

Result<InsertResult> DocumentCombineService::Commit(
    document::DocumentSession& session, const InsertPlan& plan,
    std::uint64_t transaction_id) {
    if (session.snapshot().source_revision != plan.base_revision)
        return Result<InsertResult>::failure(ErrorCode::document_revision_mismatch);
    std::vector<std::filesystem::path> created;
    for (const auto& resource : plan.resources) {
        if (!resource.create) continue;
        std::error_code error;
        std::filesystem::create_directories(resource.target.parent_path(), error);
        if (error || !CopyFileW(resource.source.c_str(), resource.target.c_str(), TRUE)) {
            return Result<InsertResult>::failure(
                RemoveCreated(created) ? ErrorCode::insert_resource_failed
                                       : ErrorCode::insert_rollback_failed);
        }
        created.push_back(resource.target);
    }
    document::EditTransaction transaction{transaction_id, plan.base_revision,
        document::EditOrigin::source_view,
        {{{plan.utf8_offset, plan.utf8_offset}, plan.insertion}}};
    const auto committed = session.commit(transaction);
    if (committed != ErrorCode::ok) {
        return Result<InsertResult>::failure(
            RemoveCreated(created) ? ErrorCode::insert_transaction_failed
                                   : ErrorCode::insert_rollback_failed);
    }
    return Result<InsertResult>::success(
        {session.snapshot().source_revision, std::move(created)});
}

}  // namespace markdownmay::services
