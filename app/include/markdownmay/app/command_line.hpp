#pragma once

#include "markdownmay/core/result.hpp"

#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace markdownmay::app {

struct StartupOptions final {
    std::vector<std::filesystem::path> paths;
    bool register_file_types{};
    bool unregister_file_types{};
    bool safe_mode{};
};

[[nodiscard]] Result<StartupOptions> ParseCommandLine(
    std::span<const std::wstring_view> arguments);

}  // namespace markdownmay::app
