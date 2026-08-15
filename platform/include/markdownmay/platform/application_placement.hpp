#pragma once

#include "markdownmay/core/result.hpp"

#include <filesystem>

namespace markdownmay::platform {

struct PlacementPlan final {
    std::filesystem::path source;
    std::filesystem::path folder;
    std::filesystem::path target;
    bool same_path{};
    bool target_exists{};
    bool same_content{};
    std::uintmax_t target_size{};
    std::filesystem::file_time_type target_write_time{};
};

class ApplicationPlacementService final {
public:
    [[nodiscard]] Result<PlacementPlan> inspect(
        const std::filesystem::path& source,
        const std::filesystem::path& folder) const;
    [[nodiscard]] ErrorCode place(
        const PlacementPlan& plan, bool replace_existing) const;
};

[[nodiscard]] std::filesystem::path DefaultPlacementFolder();

}  // namespace markdownmay::platform
