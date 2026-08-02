#include "markdownmay/platform/printing.hpp"
#include "markdownmay/services/local_services.hpp"

#include <windows.h>

#include <filesystem>

int main() {
    using namespace markdownmay;
    platform::PageSetup page;
    const auto layout = platform::CalculatePrintLayout(
        {300, 300, 2480, 3508, 50, 50}, page);
    if (layout.page_twips.right <= 0 || layout.page_twips.bottom <= 0 ||
        layout.content_twips.left <= 0 || layout.content_twips.top <= 0 ||
        layout.content_twips.right <= layout.content_twips.left ||
        layout.content_twips.bottom <= layout.content_twips.top) return 2;

    const auto root = std::filesystem::temp_directory_path() /
        (L"markdownmay-phase6-settings-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    services::SettingsStore store(root / L"settings.ini");
    services::Settings settings;
    settings.theme = services::ThemeSetting::dark;
    settings.default_mode = services::DefaultViewMode::split;
    settings.print_landscape = true;
    settings.margin_left_hundredths_mm = 1750;
    settings.unknown["future_setting"] = "kept";
    if (store.save(settings) != ErrorCode::ok) return 3;
    const auto loaded = store.load();
    const bool valid = loaded.is_ok() && loaded.value().print_landscape &&
        loaded.value().margin_left_hundredths_mm == 1750 &&
        loaded.value().theme == services::ThemeSetting::dark &&
        loaded.value().default_mode == services::DefaultViewMode::split &&
        loaded.value().unknown.at("future_setting") == "kept";
    std::filesystem::remove_all(root, ignored);
    return valid ? 0 : 4;
}
