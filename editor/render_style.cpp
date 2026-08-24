#include "markdownmay/editor/render_style.hpp"

namespace markdownmay::editor {
namespace {
constexpr RenderStyleProfile kSongYing{L"SimSun", 230, L"SimHei",
    {400, 360, 320, 300, 280, 260}, true, 345, 0, 120,
    {360, 320, 280, 240, 200, 180}, {180, 160, 140, 120, 100, 100}};
constexpr RenderStyleProfile kYuanLang{L"DengXian", 220, L"DengXian",
    {420, 380, 340, 300, 280, 260}, true, 319, 0, 120,
    {380, 340, 300, 260, 220, 180}, {180, 160, 140, 120, 100, 100}};
constexpr RenderStyleProfile kMingZheng{L"SimHei", 220, L"SimHei",
    {440, 400, 360, 320, 300, 280}, true, 330, 0, 140,
    {400, 360, 320, 280, 240, 200}, {200, 180, 160, 140, 120, 100}};
constexpr RenderStyleProfile kQingXi{L"Microsoft YaHei UI", 220, L"Microsoft YaHei UI",
    {480, 440, 400, 360, 320, 280}, true, 352, 0, 160,
    {440, 400, 360, 320, 280, 240}, {220, 200, 180, 160, 140, 120}};
}

const RenderStyleProfile& ProfileFor(RenderStyle style) noexcept {
    switch (style) {
    case RenderStyle::song_ying: return kSongYing;
    case RenderStyle::ming_zheng: return kMingZheng;
    case RenderStyle::qing_xi: return kQingXi;
    default: return kYuanLang;
    }
}
}  // namespace markdownmay::editor
