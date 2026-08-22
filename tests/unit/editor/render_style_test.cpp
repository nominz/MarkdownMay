#include "markdownmay/editor/render_style.hpp"

int main() {
    using namespace markdownmay::editor;
    const auto& song = ProfileFor(RenderStyle::song_ying);
    const auto& yuan = ProfileFor(RenderStyle::yuan_lang);
    const auto& ming = ProfileFor(RenderStyle::ming_zheng);
    const auto& qing = ProfileFor(RenderStyle::qing_xi);
    if (song.body_size != 230 || yuan.body_size != 220 ||
        ming.body_size != 220 || qing.body_size != 220) return 1;
    if (song.heading_sizes != std::array<long, 6>{400, 360, 320, 300, 280, 260} ||
        yuan.heading_sizes != std::array<long, 6>{420, 380, 340, 300, 280, 260} ||
        ming.heading_sizes != std::array<long, 6>{440, 400, 360, 320, 300, 280} ||
        qing.heading_sizes != std::array<long, 6>{480, 440, 400, 360, 320, 280}) return 2;
    for (std::size_t level = 0; level < 6; ++level)
        if (!(song.heading_sizes[level] <= yuan.heading_sizes[level] &&
              yuan.heading_sizes[level] <= ming.heading_sizes[level] &&
              ming.heading_sizes[level] <= qing.heading_sizes[level])) return 3;
    return 0;
}
