#include "markdownmay/editor/render_style.hpp"

#include <string_view>

int main() {
    using namespace markdownmay::editor;
    const auto& song = ProfileFor(RenderStyle::song_ying);
    const auto& yuan = ProfileFor(RenderStyle::yuan_lang);
    const auto& ming = ProfileFor(RenderStyle::ming_zheng);
    const auto& qing = ProfileFor(RenderStyle::qing_xi);
    if (song.body_size != 230 || yuan.body_size != 220 ||
        ming.body_size != 220 || qing.body_size != 220) return 1;
    if (std::wstring_view(song.body_font) != L"SimSun" ||
        std::wstring_view(song.heading_font) != L"SimHei" ||
        std::wstring_view(yuan.body_font) != L"DengXian" ||
        std::wstring_view(yuan.heading_font) != L"DengXian" ||
        std::wstring_view(ming.body_font) != L"SimHei" ||
        std::wstring_view(ming.heading_font) != L"SimHei" ||
        std::wstring_view(qing.body_font) != L"Microsoft YaHei UI" ||
        std::wstring_view(qing.heading_font) != L"Microsoft YaHei UI") return 4;
    if (song.heading_sizes != std::array<long, 6>{400, 360, 320, 300, 280, 260} ||
        yuan.heading_sizes != std::array<long, 6>{420, 380, 340, 300, 280, 260} ||
        ming.heading_sizes != std::array<long, 6>{440, 400, 360, 320, 300, 280} ||
        qing.heading_sizes != std::array<long, 6>{480, 440, 400, 360, 320, 280}) return 2;
    for (std::size_t level = 0; level < 6; ++level)
        if (!(song.heading_sizes[level] <= yuan.heading_sizes[level] &&
              yuan.heading_sizes[level] <= ming.heading_sizes[level] &&
              ming.heading_sizes[level] <= qing.heading_sizes[level])) return 3;
    if (song.minimum_line_spacing != 345 || song.paragraph_space_after != 120 ||
        yuan.minimum_line_spacing != 319 || yuan.paragraph_space_after != 120 ||
        ming.minimum_line_spacing != 330 || ming.paragraph_space_after != 140 ||
        qing.minimum_line_spacing != 352 || qing.paragraph_space_after != 160) return 5;
    if (song.heading_space_before.front() != 360 ||
        yuan.heading_space_before.front() != 380 ||
        ming.heading_space_before.front() != 400 ||
        qing.heading_space_before.front() != 440) return 6;
    return 0;
}
