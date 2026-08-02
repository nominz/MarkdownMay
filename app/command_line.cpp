#include "markdownmay/app/command_line.hpp"

#include "markdownmay/fileio/path_utils.hpp"

namespace markdownmay::app {

Result<StartupOptions> ParseCommandLine(
    std::span<const std::wstring_view> arguments) {
    StartupOptions result;
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const auto value = arguments[index];
        if (value == L"--register") result.register_file_types = true;
        else if (value == L"--unregister") result.unregister_file_types = true;
        else if (value == L"--safe-mode") result.safe_mode = true;
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
         !result.paths.empty()))
        return Result<StartupOptions>::failure(ErrorCode::app_invalid_command_line);
    return Result<StartupOptions>::success(std::move(result));
}

}  // namespace markdownmay::app
