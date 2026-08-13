#include "markdownmay/platform/file_association.hpp"

#include "markdownmay/fileio/path_utils.hpp"

#include <shellapi.h>
#include <shlobj.h>

#include <array>
#include <cwchar>
#include <optional>
#include <string_view>

namespace markdownmay::platform {
namespace {
constexpr wchar_t kMarkdownProgId[] = L"MarkdownMay.Document.Markdown";
constexpr wchar_t kTextProgId[] = L"MarkdownMay.Document.Text";
constexpr wchar_t kLegacyProgId[] = L"MarkdownMay.Document";
constexpr wchar_t kRegisteredName[] = L"Markdown May";
constexpr wchar_t kCapabilities[] = L"Software\\MarkdownMay\\Capabilities";
constexpr wchar_t kIgnoredRepair[] = L"Software\\MarkdownMay";

class Key final {
public:
    ~Key() { if (value_) RegCloseKey(value_); }
    HKEY* out() noexcept { return &value_; }
    HKEY get() const noexcept { return value_; }
private:
    HKEY value_{};
};

LONG CreateKey(HKEY root, const wchar_t* path, REGSAM access, Key& key) {
    return RegCreateKeyExW(root, path, 0, nullptr, REG_OPTION_NON_VOLATILE,
        access, nullptr, key.out(), nullptr);
}

LONG OpenKey(HKEY root, const wchar_t* path, REGSAM access, Key& key) {
    return RegOpenKeyExW(root, path, 0, access, key.out());
}

LONG SetString(HKEY root, const wchar_t* path, const wchar_t* name,
               std::wstring_view value) {
    Key key;
    auto result = CreateKey(root, path, KEY_SET_VALUE, key);
    if (result != ERROR_SUCCESS) return result;
    return RegSetValueExW(key.get(), name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.data()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

LONG SetNone(HKEY root, const wchar_t* path, const wchar_t* name) {
    Key key;
    auto result = CreateKey(root, path, KEY_SET_VALUE, key);
    if (result != ERROR_SUCCESS) return result;
    return RegSetValueExW(key.get(), name, 0, REG_NONE, nullptr, 0);
}

std::optional<std::wstring> ReadString(HKEY root, const wchar_t* path,
                                       const wchar_t* name) {
    Key key;
    if (OpenKey(root, path, KEY_QUERY_VALUE, key) != ERROR_SUCCESS) return std::nullopt;
    DWORD type{};
    DWORD bytes{};
    if (RegQueryValueExW(key.get(), name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < sizeof(wchar_t)) return std::nullopt;
    std::wstring result(bytes / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key.get(), name, nullptr, &type,
            reinterpret_cast<BYTE*>(result.data()), &bytes) != ERROR_SUCCESS) return std::nullopt;
    while (!result.empty() && result.back() == L'\0') result.pop_back();
    return result;
}

bool HasValue(HKEY root, const wchar_t* path, const wchar_t* name) {
    Key key;
    if (OpenKey(root, path, KEY_QUERY_VALUE, key) != ERROR_SUCCESS) return false;
    return RegQueryValueExW(key.get(), name, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

bool Same(std::wstring_view left, std::wstring_view right) noexcept {
    return left.size() == right.size() &&
        _wcsnicmp(left.data(), right.data(), left.size()) == 0;
}

LONG DeleteValueIfPresent(HKEY root, const wchar_t* path, const wchar_t* name) {
    Key key;
    const auto opened = OpenKey(root, path, KEY_SET_VALUE, key);
    if (opened == ERROR_FILE_NOT_FOUND) return ERROR_SUCCESS;
    if (opened != ERROR_SUCCESS) return opened;
    const auto deleted = RegDeleteValueW(key.get(), name);
    return deleted == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : deleted;
}

LONG DeleteValueIfEqual(HKEY root, const wchar_t* path, const wchar_t* name,
                        std::wstring_view expected) {
    const auto current = ReadString(root, path, name);
    if (!current || !Same(*current, expected)) return ERROR_SUCCESS;
    return DeleteValueIfPresent(root, path, name);
}

}

FileAssociationRegistry::FileAssociationRegistry(HKEY current_user) noexcept
    : current_user_(current_user) {}

std::wstring FileAssociationRegistry::open_command(
    const std::filesystem::path& executable) {
    const auto normalized = std::filesystem::absolute(executable).lexically_normal().wstring();
    return L"\"" + normalized + L"\" \"%1\"";
}

ErrorCode FileAssociationRegistry::register_application(
    const std::filesystem::path& executable) const {
    const auto normalized = std::filesystem::absolute(executable).lexically_normal();
    const auto exe = normalized.wstring();
    const auto icon = L"\"" + exe + L"\",0";
    const auto command = open_command(normalized);
    const std::array<LONG, 15> results{
        SetString(current_user_, L"Software\\Classes\\MarkdownMay.Document.Markdown", nullptr,
            L"马冬梅 Markdown 文档"),
        SetString(current_user_, L"Software\\Classes\\MarkdownMay.Document.Markdown\\DefaultIcon",
            nullptr, icon),
        SetString(current_user_, L"Software\\Classes\\MarkdownMay.Document.Markdown\\shell\\open\\command",
            nullptr, command),
        SetString(current_user_, L"Software\\Classes\\MarkdownMay.Document.Text", nullptr,
            L"马冬梅纯文本文档"),
        SetString(current_user_, L"Software\\Classes\\MarkdownMay.Document.Text\\DefaultIcon",
            nullptr, icon),
        SetString(current_user_, L"Software\\Classes\\MarkdownMay.Document.Text\\shell\\open\\command",
            nullptr, command),
        SetNone(current_user_, L"Software\\Classes\\.md\\OpenWithProgids", kMarkdownProgId),
        SetNone(current_user_, L"Software\\Classes\\.markdown\\OpenWithProgids", kMarkdownProgId),
        SetNone(current_user_, L"Software\\Classes\\.txt\\OpenWithProgids", kTextProgId),
        SetString(current_user_, kCapabilities, L"ApplicationName", kRegisteredName),
        SetString(current_user_, kCapabilities, L"ApplicationDescription",
            L"轻量、免费、开源的 Markdown 与纯文本编辑器"),
        SetString(current_user_, L"Software\\MarkdownMay\\Capabilities\\FileAssociations",
            L".md", kMarkdownProgId),
        SetString(current_user_, L"Software\\MarkdownMay\\Capabilities\\FileAssociations",
            L".markdown", kMarkdownProgId),
        SetString(current_user_, L"Software\\MarkdownMay\\Capabilities\\FileAssociations",
            L".txt", kTextProgId),
        SetString(current_user_, L"Software\\RegisteredApplications", kRegisteredName,
            kCapabilities),
    };
    for (const auto result : results) {
        if (result != ERROR_SUCCESS)
            return result == ERROR_ACCESS_DENIED ? ErrorCode::platform_registry_access_denied
                                                 : ErrorCode::platform_association_write_failed;
    }
    const auto legacy_command = ReadString(current_user_,
        L"Software\\Classes\\MarkdownMay.Document\\shell\\open\\command", nullptr);
    if (legacy_command && Same(*legacy_command, command)) {
        (void)DeleteValueIfPresent(current_user_,
            L"Software\\Classes\\MarkdownMay.Document\\shell\\open\\command", nullptr);
        (void)DeleteValueIfPresent(current_user_,
            L"Software\\Classes\\.md\\OpenWithProgids", kLegacyProgId);
        (void)DeleteValueIfPresent(current_user_,
            L"Software\\Classes\\.markdown\\OpenWithProgids", kLegacyProgId);
    }
    (void)DeleteValueIfPresent(current_user_, kIgnoredRepair,
        L"IgnoredAssociationRepairPath");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ErrorCode::ok;
}

ErrorCode FileAssociationRegistry::unregister_application(
    const std::filesystem::path& executable) const {
    const auto normalized = std::filesystem::absolute(executable).lexically_normal();
    const auto exe = normalized.wstring();
    const auto command = open_command(normalized);
    const auto icon = L"\"" + exe + L"\",0";
    const std::array<LONG, 16> value_results{
        DeleteValueIfEqual(current_user_, L"Software\\Classes\\MarkdownMay.Document.Markdown",
            nullptr, L"马冬梅 Markdown 文档"),
        DeleteValueIfEqual(current_user_, L"Software\\Classes\\MarkdownMay.Document.Markdown\\DefaultIcon",
            nullptr, icon),
        DeleteValueIfEqual(current_user_, L"Software\\Classes\\MarkdownMay.Document.Markdown\\shell\\open\\command",
            nullptr, command),
        DeleteValueIfEqual(current_user_, L"Software\\Classes\\MarkdownMay.Document.Text",
            nullptr, L"马冬梅纯文本文档"),
        DeleteValueIfEqual(current_user_, L"Software\\Classes\\MarkdownMay.Document.Text\\DefaultIcon",
            nullptr, icon),
        DeleteValueIfEqual(current_user_, L"Software\\Classes\\MarkdownMay.Document.Text\\shell\\open\\command",
            nullptr, command),
        DeleteValueIfPresent(current_user_, L"Software\\Classes\\.md\\OpenWithProgids", kMarkdownProgId),
        DeleteValueIfPresent(current_user_, L"Software\\Classes\\.markdown\\OpenWithProgids", kMarkdownProgId),
        DeleteValueIfPresent(current_user_, L"Software\\Classes\\.txt\\OpenWithProgids", kTextProgId),
        DeleteValueIfEqual(current_user_, L"Software\\RegisteredApplications", kRegisteredName,
            kCapabilities),
        DeleteValueIfEqual(current_user_, L"Software\\MarkdownMay\\Capabilities\\FileAssociations",
            L".md", kMarkdownProgId),
        DeleteValueIfEqual(current_user_, L"Software\\MarkdownMay\\Capabilities\\FileAssociations",
            L".markdown", kMarkdownProgId),
        DeleteValueIfEqual(current_user_, L"Software\\MarkdownMay\\Capabilities\\FileAssociations",
            L".txt", kTextProgId),
        DeleteValueIfEqual(current_user_,
            L"Software\\Classes\\MarkdownMay.Document\\shell\\open\\command",
            nullptr, command),
        DeleteValueIfPresent(current_user_, L"Software\\Classes\\.md\\OpenWithProgids",
            kLegacyProgId),
        DeleteValueIfPresent(current_user_, L"Software\\Classes\\.markdown\\OpenWithProgids",
            kLegacyProgId),
    };
    LONG first_error = ERROR_SUCCESS;
    for (const auto result : value_results)
        if (first_error == ERROR_SUCCESS && result != ERROR_SUCCESS) first_error = result;
    const auto ignored = DeleteValueIfPresent(current_user_, kIgnoredRepair,
        L"IgnoredAssociationRepairPath");
    if (first_error == ERROR_SUCCESS && ignored != ERROR_SUCCESS) first_error = ignored;
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    if (first_error == ERROR_SUCCESS) return ErrorCode::ok;
    return first_error == ERROR_ACCESS_DENIED ? ErrorCode::platform_registry_access_denied
                                              : ErrorCode::platform_association_remove_failed;
}

AssociationState FileAssociationRegistry::state(
    const std::filesystem::path& executable) const {
    const auto command = ReadString(current_user_,
        L"Software\\Classes\\MarkdownMay.Document.Markdown\\shell\\open\\command", nullptr);
    const auto text_command = ReadString(current_user_,
        L"Software\\Classes\\MarkdownMay.Document.Text\\shell\\open\\command", nullptr);
    const auto registered = ReadString(current_user_,
        L"Software\\RegisteredApplications", kRegisteredName);
    const bool md = HasValue(current_user_, L"Software\\Classes\\.md\\OpenWithProgids", kMarkdownProgId);
    const bool markdown = HasValue(current_user_,
        L"Software\\Classes\\.markdown\\OpenWithProgids", kMarkdownProgId);
    const bool text = HasValue(current_user_, L"Software\\Classes\\.txt\\OpenWithProgids", kTextProgId);
    if (!command && !text_command && !registered && !md && !markdown && !text)
        return AssociationState::not_registered;
    if (!command || !text_command || !registered || !md || !markdown || !text ||
        !Same(*command, open_command(executable)) ||
        !Same(*text_command, open_command(executable)) || !Same(*registered, kCapabilities))
        return AssociationState::needs_repair;
    return AssociationState::current;
}

bool FileAssociationRegistry::repair_prompt_ignored(
    const std::filesystem::path& executable) const {
    const auto ignored = ReadString(current_user_, kIgnoredRepair,
        L"IgnoredAssociationRepairPath");
    return ignored && Same(*ignored,
        std::filesystem::absolute(executable).lexically_normal().wstring());
}

ErrorCode FileAssociationRegistry::ignore_repair_prompt(
    const std::filesystem::path& executable) const {
    const auto value = std::filesystem::absolute(executable).lexically_normal().wstring();
    const auto result = SetString(current_user_, kIgnoredRepair,
        L"IgnoredAssociationRepairPath", value);
    return result == ERROR_SUCCESS ? ErrorCode::ok : ErrorCode::settings_save_failed;
}

const wchar_t* DefaultAppsSettingsUri() noexcept {
    return L"ms-settings:defaultapps?registeredAppUser=Markdown%20May";
}

ErrorCode OpenDefaultAppsSettings(HWND owner) {
    auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open",
        DefaultAppsSettingsUri(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) result = reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open",
        L"ms-settings:defaultapps", nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32 ? ErrorCode::ok : ErrorCode::platform_default_apps_ui_failed;
}

}  // namespace markdownmay::platform
