#pragma once

#include <array>
#include <cstdint>

namespace markdownmay::editor {

enum class RenderStyle : std::uint8_t { song_ying, yuan_lang, ming_zheng, qing_xi };

struct RenderStyleProfile final {
    const wchar_t* body_font;
    long body_size;
    const wchar_t* heading_font;
    std::array<long, 6> heading_sizes;
    bool heading_bold;
    long minimum_line_spacing;
    long paragraph_space_before;
    long paragraph_space_after;
    std::array<long, 6> heading_space_before;
    std::array<long, 6> heading_space_after;
};

[[nodiscard]] const RenderStyleProfile& ProfileFor(RenderStyle style) noexcept;

}  // namespace markdownmay::editor
