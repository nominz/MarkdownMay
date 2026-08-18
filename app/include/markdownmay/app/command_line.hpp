#pragma once

#include "markdownmay/core/result.hpp"

#include <filesystem>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace markdownmay::app {

struct StartupOptions final {
    std::vector<std::filesystem::path> paths;
    bool register_file_types{};
    bool unregister_file_types{};
    bool safe_mode{};
    bool repair_file_types{};
    std::optional<std::uint32_t> wait_for_process;
};

struct MultiInstanceLaunchPlan final {
    std::vector<std::filesystem::path> current_process_paths;
    std::vector<std::filesystem::path> child_process_paths;
};

[[nodiscard]] Result<StartupOptions> ParseCommandLine(
    std::span<const std::wstring_view> arguments);
[[nodiscard]] MultiInstanceLaunchPlan BuildMultiInstanceLaunchPlan(
    std::span<const std::filesystem::path> paths);

}  // namespace markdownmay::app
