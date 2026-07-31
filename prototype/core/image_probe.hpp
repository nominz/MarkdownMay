#pragma once

#include <cstdint>
#include <filesystem>

namespace markdownmay::prototype {

struct ImageInformation final {
    std::uint32_t width{};
    std::uint32_t height{};
};

[[nodiscard]] bool ProbeLocalImage(
    const std::filesystem::path& path,
    ImageInformation& information) noexcept;

}  // namespace markdownmay::prototype
