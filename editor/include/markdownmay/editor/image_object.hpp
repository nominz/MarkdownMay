#pragma once

#include "markdownmay/fileio/image_store.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace markdownmay::editor {

enum class ImageDisplayState : std::uint8_t { ready, remote_blocked, missing, decode_failed };

struct ImageObject final {
    fileio::ImageReference reference;
    std::string alternative;
    std::uint32_t pixel_width{};
    std::uint32_t pixel_height{};
    std::uint16_t display_percent{100};
    ImageDisplayState state{ImageDisplayState::missing};
};

[[nodiscard]] ImageObject LoadImageObject(
    const std::filesystem::path& document_path,
    std::string target,
    std::string alternative,
    std::uint16_t display_percent = 100) noexcept;

}  // namespace markdownmay::editor
