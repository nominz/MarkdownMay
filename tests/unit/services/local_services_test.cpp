#include "markdownmay/services/local_services.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>

int main() {
    using namespace markdownmay;
    using namespace markdownmay::services;
    const auto root = std::filesystem::temp_directory_path() /
        (L"markdownmay-services-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ignored; std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const auto cleanup = [&] { std::filesystem::remove_all(root, ignored); };

    SettingsStore settings_store(root / L"settings.ini");
    Settings settings; settings.default_mode = DefaultViewMode::split;
    settings.theme = ThemeSetting::dark; settings.recovery_interval_seconds = 45;
    settings.print_landscape = true; settings.margin_left_hundredths_mm = 1234;
    settings.unknown["future_option"] = "保留";
    if (settings_store.save(settings) != ErrorCode::ok) { cleanup(); return 1; }
    auto loaded_settings = settings_store.load();
    if (!loaded_settings.is_ok() ||
        loaded_settings.value().default_mode != DefaultViewMode::split ||
        loaded_settings.value().theme != ThemeSetting::dark ||
        loaded_settings.value().recovery_interval_seconds != 45 ||
        !loaded_settings.value().print_landscape ||
        loaded_settings.value().margin_left_hundredths_mm != 1234 ||
        loaded_settings.value().unknown.at("future_option") != "保留") {
        cleanup(); return 2;
    }
    const auto corrupt_path = root / L"corrupt.ini";
    {
        std::ofstream corrupt(corrupt_path, std::ios::binary);
        corrupt << "default_mode=impossible\n";
    }
    SettingsStore corrupt_store(corrupt_path);
    const auto defaults = corrupt_store.load();
    if (!defaults.is_ok() || defaults.value().default_mode != DefaultViewMode::render ||
        std::filesystem::exists(corrupt_path)) { cleanup(); return 10; }
    bool bad_copy_found = false;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (entry.path().filename().wstring().find(L"corrupt.ini.bad-") == 0) bad_copy_found = true;
    }
    if (!bad_copy_found) { cleanup(); return 11; }

    const auto legacy_path = root / L"legacy.ini";
    {
        std::ofstream legacy(legacy_path, std::ios::binary);
        legacy << "schema_version=1\nfollow_system_theme=false\nfuture_legacy=kept\n";
    }
    const auto migrated = SettingsStore(legacy_path).load();
    if (!migrated.is_ok() || migrated.value().schema_version != 2 ||
        migrated.value().theme != ThemeSetting::light ||
        migrated.value().unknown.at("future_legacy") != "kept") {
        cleanup(); return 12;
    }

    RecoveryStore recovery(root / L"recovery");
    RecoverySnapshot snapshot{77, root / L"原文.md", "# 未保存\r\n\r\n正文 😀\n", 9};
    if (recovery.write(snapshot) != ErrorCode::ok) { cleanup(); return 3; }
    auto discovered = recovery.discover();
    if (!discovered.is_ok() || discovered.value().size() != 1 ||
        discovered.value()[0].document != 77 || discovered.value()[0].revision != 9 ||
        discovered.value()[0].source != snapshot.source ||
        discovered.value()[0].original_path != snapshot.original_path) {
        cleanup(); return 4;
    }
    const auto discarded = recovery.discard(77);
    const auto after_discard = recovery.discover();
    if (discarded != ErrorCode::ok || !after_discard.is_ok() ||
        !after_discard.value().empty()) {
        cleanup(); return 5;
    }

    RecentFilesStore recent(root / L"recent.ini", 2);
    if (recent.touch(root / L"一.md") != ErrorCode::ok ||
        recent.touch(root / L"二.md") != ErrorCode::ok ||
        recent.touch(root / L"一.md") != ErrorCode::ok) { cleanup(); return 6; }
    auto recent_files = recent.load();
    if (!recent_files.is_ok() || recent_files.value().size() != 2 ||
        recent_files.value()[0].filename() != L"一.md") { cleanup(); return 7; }

    if (recent.clear() != ErrorCode::ok) { cleanup(); return 10; }
    const auto after_clear = recent.load();
    if (!after_clear.is_ok() || !after_clear.value().empty()) {
        cleanup(); return 11;
    }

    const auto log_path = root / L"logs" / L"app.log";
    PrivacyLogger logger(log_path);
    const auto secret_path = root / L"绝密客户名单.md";
    if (logger.append({3002, "document\n伪造正文", 5, 1234, secret_path}) != ErrorCode::ok) {
        cleanup(); return 8;
    }
    std::ifstream log(log_path, std::ios::binary);
    const std::string log_text((std::istreambuf_iterator<char>(log)), {});
    if (log_text.find("绝密") != std::string::npos ||
        log_text.find("伪造正文") != std::string::npos ||
        log_text.find(root.string()) != std::string::npos ||
        log_text.find("path_hash=") == std::string::npos ||
        log_text.find("extension=.md") == std::string::npos) {
        cleanup(); return 9;
    }
    cleanup();
    return 0;
}
