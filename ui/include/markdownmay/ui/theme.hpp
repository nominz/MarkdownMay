#pragma once

#include <windows.h>

#include <cstdint>

namespace markdownmay::ui {

enum class ThemePreference : std::uint8_t { follow_system, light, dark };
enum class ThemeKind : std::uint8_t { light, dark, high_contrast };

struct SystemThemeState final { bool apps_use_light{true}; bool high_contrast{}; };
struct ThemePalette final {
    COLORREF window{};
    COLORREF surface{};
    COLORREF text{};
    COLORREF muted{};
    COLORREF accent{};
};

[[nodiscard]] ThemeKind ResolveTheme(ThemePreference preference,
                                     SystemThemeState system) noexcept;
[[nodiscard]] ThemePalette PaletteFor(ThemeKind kind) noexcept;
[[nodiscard]] int ScaleForDpi(int value, UINT dpi) noexcept;
[[nodiscard]] SystemThemeState ReadSystemTheme() noexcept;
[[nodiscard]] bool ApplyTitleBarTheme(HWND window, ThemeKind kind) noexcept;

}  // namespace markdownmay::ui
