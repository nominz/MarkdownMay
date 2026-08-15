#include "markdownmay/platform/application_placement.hpp"

#include "markdownmay/fileio/path_utils.hpp"

#include <windows.h>
#include <shlobj.h>

#include <array>
#include <algorithm>
#include <cstddef>
#include <string>
#include <system_error>

namespace markdownmay::platform {
namespace {

class Handle final {
public:
    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) noexcept : value_(value) {}
    ~Handle() { if (valid()) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(other.value_) {
        other.value_ = INVALID_HANDLE_VALUE;
    }
    [[nodiscard]] bool valid() const noexcept {
        return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
    }
    [[nodiscard]] HANDLE get() const noexcept { return value_; }
private:
    HANDLE value_;
};

bool SamePath(const std::filesystem::path& left,
              const std::filesystem::path& right) {
    std::error_code error;
    if (std::filesystem::exists(left, error) && !error &&
        std::filesystem::exists(right, error) && !error) {
        const bool equivalent = std::filesystem::equivalent(left, right, error);
        if (!error) return equivalent;
    }
    return _wcsicmp(std::filesystem::absolute(left).lexically_normal().c_str(),
                    std::filesystem::absolute(right).lexically_normal().c_str()) == 0;
}

bool SameBytes(const std::filesystem::path& left,
               const std::filesystem::path& right) {
    Handle first(CreateFileW(left.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    Handle second(CreateFileW(right.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!first.valid() || !second.valid()) return false;
    LARGE_INTEGER first_size{}, second_size{};
    if (!GetFileSizeEx(first.get(), &first_size) ||
        !GetFileSizeEx(second.get(), &second_size) ||
        first_size.QuadPart != second_size.QuadPart) return false;
    std::array<std::byte, 64 * 1024> a{}, b{};
    for (;;) {
        DWORD read_a{}, read_b{};
        if (!ReadFile(first.get(), a.data(), static_cast<DWORD>(a.size()), &read_a, nullptr) ||
            !ReadFile(second.get(), b.data(), static_cast<DWORD>(b.size()), &read_b, nullptr) ||
            read_a != read_b) return false;
        if (read_a == 0) return true;
        if (!std::equal(a.begin(), a.begin() + read_a, b.begin())) return false;
    }
}

std::filesystem::path UniqueTemporaryPath(const std::filesystem::path& folder) {
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        GUID guid{};
        if (FAILED(CoCreateGuid(&guid))) return {};
        std::array<wchar_t, 40> value{};
        if (!StringFromGUID2(guid, value.data(), static_cast<int>(value.size())))
            return {};
        const auto name = L".MarkdownMay.place." + std::wstring(value.data()) + L".tmp";
        const auto candidate = folder / name;
        Handle handle(CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr));
        if (handle.valid()) return candidate;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
            return {};
    }
    return {};
}

bool CopyAndFlush(const std::filesystem::path& source,
                  const std::filesystem::path& temporary) {
    Handle input(CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    Handle output(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!input.valid() || !output.valid()) return false;
    std::array<std::byte, 64 * 1024> buffer{};
    for (;;) {
        DWORD read{};
        if (!ReadFile(input.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
            return false;
        if (read == 0) break;
        DWORD offset{};
        while (offset < read) {
            DWORD written{};
            if (!WriteFile(output.get(), buffer.data() + offset, read - offset, &written, nullptr) ||
                written == 0) return false;
            offset += written;
        }
    }
    return FlushFileBuffers(output.get()) != FALSE;
}

bool TargetUnchanged(const PlacementPlan& plan) {
    std::error_code error;
    const bool exists = std::filesystem::exists(plan.target, error);
    if (error || exists != plan.target_exists) return false;
    if (!exists) return true;
    return std::filesystem::file_size(plan.target, error) == plan.target_size && !error &&
        std::filesystem::last_write_time(plan.target, error) == plan.target_write_time && !error;
}

}  // namespace

Result<PlacementPlan> ApplicationPlacementService::inspect(
    const std::filesystem::path& source,
    const std::filesystem::path& folder) const {
    if (source.empty() || folder.empty())
        return Result<PlacementPlan>::failure(ErrorCode::platform_placement_invalid_target);
    std::error_code error;
    const auto normalized_source = std::filesystem::absolute(source, error).lexically_normal();
    if (error || !std::filesystem::is_regular_file(normalized_source, error) || error)
        return Result<PlacementPlan>::failure(ErrorCode::platform_placement_invalid_target);
    const auto normalized_folder = std::filesystem::absolute(folder, error).lexically_normal();
    if (error) return Result<PlacementPlan>::failure(ErrorCode::platform_placement_invalid_target);
    if (std::filesystem::exists(normalized_folder, error) &&
        (!std::filesystem::is_directory(normalized_folder, error) || error))
        return Result<PlacementPlan>::failure(ErrorCode::platform_placement_invalid_target);

    PlacementPlan plan{normalized_source, normalized_folder,
        normalized_folder / L"MarkdownMay.exe"};
    plan.same_path = SamePath(plan.source, plan.target);
    plan.target_exists = std::filesystem::exists(plan.target, error) && !error;
    if (error) return Result<PlacementPlan>::failure(ErrorCode::platform_placement_invalid_target);
    if (plan.target_exists) {
        plan.target_size = std::filesystem::file_size(plan.target, error);
        if (error) return Result<PlacementPlan>::failure(ErrorCode::platform_placement_invalid_target);
        plan.target_write_time = std::filesystem::last_write_time(plan.target, error);
        if (error) return Result<PlacementPlan>::failure(ErrorCode::platform_placement_invalid_target);
        plan.same_content = SameBytes(plan.source, plan.target);
    }
    return Result<PlacementPlan>::success(std::move(plan));
}

ErrorCode ApplicationPlacementService::place(
    const PlacementPlan& plan, bool replace_existing) const {
    if (plan.same_path) return ErrorCode::ok;
    if (plan.target_exists && !replace_existing)
        return ErrorCode::platform_placement_target_changed;
    std::error_code error;
    std::filesystem::create_directories(plan.folder, error);
    if (error || !std::filesystem::is_directory(plan.folder, error) || error)
        return ErrorCode::platform_placement_invalid_target;
    const auto temporary = UniqueTemporaryPath(plan.folder);
    if (temporary.empty()) return ErrorCode::platform_placement_copy_failed;
    const auto cleanup = [&] { std::error_code ignored; std::filesystem::remove(temporary, ignored); };
    if (!CopyAndFlush(plan.source, temporary)) {
        cleanup();
        return ErrorCode::platform_placement_copy_failed;
    }
    if (!SameBytes(plan.source, temporary)) {
        cleanup();
        return ErrorCode::platform_placement_verify_failed;
    }
    if (!TargetUnchanged(plan)) {
        cleanup();
        return ErrorCode::platform_placement_target_changed;
    }
    const DWORD flags = MOVEFILE_WRITE_THROUGH |
        (replace_existing ? MOVEFILE_REPLACE_EXISTING : 0);
    if (!MoveFileExW(temporary.c_str(), plan.target.c_str(), flags)) {
        cleanup();
        return ErrorCode::platform_placement_copy_failed;
    }
    return ErrorCode::ok;
}

std::filesystem::path DefaultPlacementFolder() {
    PWSTR raw{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT,
            nullptr, &raw))) return {};
    std::filesystem::path result(raw);
    CoTaskMemFree(raw);
    return result / L"Programs" / L"MarkdownMay";
}

}  // namespace markdownmay::platform
