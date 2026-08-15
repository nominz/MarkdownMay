#include "markdownmay/app/command_line.hpp"

#include "markdownmay/fileio/path_utils.hpp"

namespace markdownmay::app {
namespace {

bool ParseProcessId(std::wstring_view value, std::uint32_t& result) noexcept {
    if (value.empty()) return false;
    std::uint64_t parsed{};
    for (const auto character : value) {
        if (character < L'0' || character > L'9') return false;
        parsed = parsed * 10 + static_cast<unsigned>(character - L'0');
        if (parsed > 0xFFFFFFFFULL) return false;
    }
    if (parsed == 0) return false;
    result = static_cast<std::uint32_t>(parsed);
    return true;
}

}

Result<StartupOptions> ParseCommandLine(
    std::span<const std::wstring_view> arguments) {
    StartupOptions result;
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const auto value = arguments[index];
        if (value == L"--register") result.register_file_types = true;
        else if (value == L"--unregister") result.unregister_file_types = true;
        else if (value == L"--safe-mode") result.safe_mode = true;
        else if (value == L"--repair-file-types") result.repair_file_types = true;
        else if (value == L"--wait-for-process") {
            if (++index >= arguments.size())
                return Result<StartupOptions>::failure(ErrorCode::app_invalid_command_line);
            std::uint32_t process_id{};
            if (!ParseProcessId(arguments[index], process_id))
                return Result<StartupOptions>::failure(ErrorCode::app_invalid_command_line);
            result.wait_for_process = process_id;
        }
        else if (value.starts_with(L"--"))
            return Result<StartupOptions>::failure(ErrorCode::app_invalid_command_line);
        else {
            if (value.empty())
                return Result<StartupOptions>::failure(ErrorCode::app_invalid_command_line);
            const auto normalized = fileio::NormalizeAbsolutePath(
                std::filesystem::path(value));
            if (!normalized.is_ok())
                return Result<StartupOptions>::failure(normalized.error());
            result.paths.push_back(normalized.value());
        }
    }
    if ((result.register_file_types && result.unregister_file_types) ||
        ((result.register_file_types || result.unregister_file_types) &&
         (!result.paths.empty() || result.repair_file_types || result.wait_for_process)) ||
        (result.repair_file_types && !result.wait_for_process) ||
        (result.wait_for_process && !result.repair_file_types) ||
        (result.repair_file_types && result.paths.size() > 1))
        return Result<StartupOptions>::failure(ErrorCode::app_invalid_command_line);
    return Result<StartupOptions>::success(std::move(result));
}

}  // namespace markdownmay::app
