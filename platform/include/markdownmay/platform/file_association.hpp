#pragma once

#include "markdownmay/core/result.hpp"

#include <windows.h>

#include <filesystem>
#include <string>

namespace markdownmay::platform {

enum class AssociationState { not_registered, current, needs_repair };

class FileAssociationRegistry final {
public:
    explicit FileAssociationRegistry(HKEY current_user = HKEY_CURRENT_USER) noexcept;
    [[nodiscard]] ErrorCode register_application(
        const std::filesystem::path& executable) const;
    [[nodiscard]] ErrorCode unregister_application(
        const std::filesystem::path& executable) const;
    [[nodiscard]] AssociationState state(
        const std::filesystem::path& executable) const;
    [[nodiscard]] bool repair_prompt_ignored(
        const std::filesystem::path& executable) const;
    [[nodiscard]] ErrorCode ignore_repair_prompt(
        const std::filesystem::path& executable) const;

    [[nodiscard]] static std::wstring open_command(
        const std::filesystem::path& executable);
    [[nodiscard]] static std::wstring icon_reference(
        const std::filesystem::path& executable, int resource_id);

private:
    HKEY current_user_{};
};

[[nodiscard]] const wchar_t* DefaultAppsSettingsUri() noexcept;
[[nodiscard]] ErrorCode OpenDefaultAppsSettings(HWND owner = nullptr);

}  // namespace markdownmay::platform
