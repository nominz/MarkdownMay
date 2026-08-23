#pragma once

#include <windows.h>

#include <filesystem>

namespace markdownmay::app {

[[nodiscard]] int RunStartup(HINSTANCE instance, int show_command);
[[nodiscard]] bool LaunchDocumentProcess(const std::filesystem::path& executable,
                                         const std::filesystem::path& document,
                                         bool safe_mode);
[[nodiscard]] constexpr bool ShouldReuseWindowForOpen(bool is_named,
                                                      bool is_dirty,
                                                      bool source_empty) noexcept {
    return !is_named && !is_dirty && source_empty;
}

}  // namespace markdownmay::app
