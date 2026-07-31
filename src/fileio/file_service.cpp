#include "markdownmay/fileio/file_service.hpp"

#include "markdownmay/fileio/path_utils.hpp"

#include <windows.h>

#include <atomic>
#include <algorithm>
#include <fstream>
#include <span>
#include <vector>

namespace markdownmay::fileio {
namespace {

std::atomic_uint64_t g_temporary_sequence{1};

class Handle final {
public:
    explicit Handle(HANDLE value) noexcept : value_(value) {}
    ~Handle() { if (value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    [[nodiscard]] HANDLE get() const noexcept { return value_; }
private:
    HANDLE value_{INVALID_HANDLE_VALUE};
};

std::filesystem::path TemporaryPathBeside(
    const std::filesystem::path& target) {
    const auto suffix = L".markdownmay.tmp-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(g_temporary_sequence.fetch_add(1));
    return target.parent_path() / (target.filename().wstring() + suffix);
}

ErrorCode WriteTemporary(
    const std::filesystem::path& path,
    std::span<const std::byte> bytes) {
    Handle file(CreateFileW(
        path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) {
        return ErrorCode::file_write_failed;
    }
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto chunk = static_cast<DWORD>((std::min)(
            remaining, static_cast<std::size_t>(0x7ffff000U)));
        DWORD written = 0;
        if (!WriteFile(file.get(), bytes.data() + offset, chunk, &written, nullptr) ||
            written != chunk) {
            return ErrorCode::file_write_failed;
        }
        offset += written;
    }
    return FlushFileBuffers(file.get())
        ? ErrorCode::ok : ErrorCode::file_write_failed;
}

}  // namespace

Result<LoadedFile> LoadTextFile(
    const std::filesystem::path& path,
    std::uint64_t maximum_bytes) {
    auto normalized = NormalizeAbsolutePath(path);
    if (!normalized.is_ok()) {
        return Result<LoadedFile>::failure(normalized.error());
    }
    std::error_code error;
    const auto size = std::filesystem::file_size(normalized.value(), error);
    if (error) {
        return Result<LoadedFile>::failure(
            std::filesystem::exists(normalized.value())
                ? ErrorCode::file_read_failed : ErrorCode::file_not_found);
    }
    if (size > maximum_bytes) {
        return Result<LoadedFile>::failure(ErrorCode::file_too_large);
    }
    std::ifstream input(normalized.value(), std::ios::binary);
    if (!input) {
        return Result<LoadedFile>::failure(ErrorCode::file_read_failed);
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return Result<LoadedFile>::failure(ErrorCode::file_read_failed);
    }
    auto decoded = DecodeText(bytes);
    if (!decoded.is_ok()) {
        return Result<LoadedFile>::failure(decoded.error());
    }
    LoadedFile result{
        normalized.value(), decoded.value().utf8, decoded.value().encoding,
        DetectLineEnding(decoded.value().utf8)};
    return Result<LoadedFile>::success(std::move(result));
}

ErrorCode SaveTextFileAtomic(const SaveRequest& request) {
    auto normalized = NormalizeAbsolutePath(request.target);
    if (!normalized.is_ok() || request.line_ending == LineEnding::mixed) {
        return ErrorCode::file_write_failed;
    }
    const auto normalized_text = NormalizeLineEndings(
        request.source, request.line_ending);
    auto encoded = EncodeText(normalized_text, request.encoding);
    if (!encoded.is_ok()) {
        return encoded.error();
    }
    const auto temporary = TemporaryPathBeside(normalized.value());
    const auto write_result = WriteTemporary(temporary, encoded.value());
    if (write_result != ErrorCode::ok) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return write_result;
    }
    const bool exists = std::filesystem::exists(normalized.value());
    const BOOL replaced = exists
        ? ReplaceFileW(normalized.value().c_str(), temporary.c_str(), nullptr,
                       REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)
        : MoveFileExW(temporary.c_str(), normalized.value().c_str(),
                      MOVEFILE_WRITE_THROUGH);
    if (!replaced) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return ErrorCode::file_write_failed;
    }
    return ErrorCode::ok;
}

}  // namespace markdownmay::fileio
