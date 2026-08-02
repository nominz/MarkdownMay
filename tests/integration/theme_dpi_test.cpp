#include "markdownmay/ui/theme.hpp"

#include <array>

using namespace markdownmay::ui;

int main() {
    if (ResolveTheme(ThemePreference::follow_system, {true, false}) != ThemeKind::light ||
        ResolveTheme(ThemePreference::follow_system, {false, false}) != ThemeKind::dark ||
        ResolveTheme(ThemePreference::light, {false, false}) != ThemeKind::light ||
        ResolveTheme(ThemePreference::dark, {true, false}) != ThemeKind::dark) return 1;
    for (const auto preference : {ThemePreference::follow_system,
            ThemePreference::light, ThemePreference::dark})
        if (ResolveTheme(preference, {true, true}) != ThemeKind::high_contrast) return 2;
    constexpr std::array<UINT, 6> dpis{96, 120, 144, 192, 240, 288};
    int previous{};
    for (const auto dpi : dpis) {
        const auto scaled = ScaleForDpi(100, dpi);
        if (scaled < previous || scaled != MulDiv(100, static_cast<int>(dpi), 96)) return 3;
        previous = scaled;
    }
    if (ScaleForDpi(100, 0) != 100) return 4;
    const auto light = PaletteFor(ThemeKind::light);
    const auto dark = PaletteFor(ThemeKind::dark);
    if (light.window == dark.window || light.text == dark.text ||
        light.surface == dark.surface) return 5;
    return 0;
}
