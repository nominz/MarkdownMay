#include "markdownmay/ui/theme.hpp"

#include <dwmapi.h>

namespace markdownmay::ui {

ThemeKind ResolveTheme(ThemePreference preference, SystemThemeState system) noexcept {
    if (system.high_contrast) return ThemeKind::high_contrast;
    if (preference == ThemePreference::light) return ThemeKind::light;
    if (preference == ThemePreference::dark) return ThemeKind::dark;
    return system.apps_use_light ? ThemeKind::light : ThemeKind::dark;
}

ThemePalette PaletteFor(ThemeKind kind) noexcept {
    if (kind == ThemeKind::high_contrast) return {
        GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_BTNFACE),
        GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_GRAYTEXT),
        GetSysColor(COLOR_HIGHLIGHT)};
    if (kind == ThemeKind::dark) return {
        RGB(30, 30, 30), RGB(45, 45, 48), RGB(235, 235, 235),
        RGB(180, 180, 180), RGB(75, 145, 230)};
    return {RGB(255, 255, 255), RGB(245, 245, 245), RGB(32, 32, 32),
        RGB(100, 100, 100), RGB(0, 102, 204)};
}

int ScaleForDpi(int value, UINT dpi) noexcept {
    return MulDiv(value, static_cast<int>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI),
        USER_DEFAULT_SCREEN_DPI);
}

SystemThemeState ReadSystemTheme() noexcept {
    HIGHCONTRASTW contrast{sizeof(contrast)};
    const bool high = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast),
        &contrast, 0) && (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    DWORD light{1};
    DWORD bytes = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &bytes);
    return {light != 0, high};
}

bool ApplyTitleBarTheme(HWND window, ThemeKind kind) noexcept {
    const BOOL dark = kind == ThemeKind::dark;
    constexpr DWORD attribute = 20;  // DWMWA_USE_IMMERSIVE_DARK_MODE
    return SUCCEEDED(DwmSetWindowAttribute(window, attribute, &dark, sizeof(dark)));
}

}  // namespace markdownmay::ui
