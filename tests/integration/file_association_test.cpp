#include "markdownmay/platform/file_association.hpp"

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <string>

using namespace markdownmay;

namespace {
bool SetString(HKEY root, const wchar_t* path, const wchar_t* name,
               const wchar_t* value) {
    HKEY key{};
    if (RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
            &key, nullptr) != ERROR_SUCCESS) return false;
    const auto bytes = static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t));
    const auto result = RegSetValueExW(key, name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value), bytes);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

std::wstring ReadString(HKEY root, const wchar_t* path, const wchar_t* name) {
    HKEY key{};
    if (RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return {};
    wchar_t value[32768]{};
    DWORD bytes = sizeof(value);
    DWORD type{};
    const auto result = RegQueryValueExW(key, name, nullptr, &type,
        reinterpret_cast<BYTE*>(value), &bytes);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_SZ ? std::wstring(value) : std::wstring{};
}
}

int main() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto test_path = L"Software\\MarkdownMay.Tests." +
        std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(nonce);
    HKEY root{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, test_path.c_str(), 0, nullptr, 0,
            KEY_ALL_ACCESS, nullptr, &root, nullptr) != ERROR_SUCCESS) return 1;
    const auto cleanup = [&] {
        RegCloseKey(root);
        RegDeleteTreeW(HKEY_CURRENT_USER, test_path.c_str());
    };

    platform::FileAssociationRegistry registry(root);
    const auto first = std::filesystem::path(L"C:\\便携 软件\\MarkdownMay.exe");
    const auto moved = std::filesystem::path(L"D:\\新位置😀\\MarkdownMay.exe");
    if (registry.state(first) != platform::AssociationState::not_registered ||
        registry.register_application(first) != ErrorCode::ok ||
        registry.state(first) != platform::AssociationState::current) {
        cleanup(); return 2;
    }
    const auto command = ReadString(root,
        L"Software\\Classes\\MarkdownMay.Document.Markdown\\shell\\open\\command", nullptr);
    const auto text_command = ReadString(root,
        L"Software\\Classes\\MarkdownMay.Document.Text\\shell\\open\\command", nullptr);
    if (command != platform::FileAssociationRegistry::open_command(first) ||
        text_command != platform::FileAssociationRegistry::open_command(first) ||
        ReadString(root, L"Software\\RegisteredApplications", L"Markdown May") !=
            L"Software\\MarkdownMay\\Capabilities" ||
        ReadString(root, L"Software\\MarkdownMay\\Capabilities\\FileAssociations",
            L".md") != L"MarkdownMay.Document.Markdown" ||
        ReadString(root, L"Software\\MarkdownMay\\Capabilities\\FileAssociations",
            L".txt") != L"MarkdownMay.Document.Text") {
        cleanup(); return 3;
    }
    HKEY forbidden{};
    if (RegOpenKeyExW(root,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.md\\UserChoice",
            0, KEY_READ, &forbidden) == ERROR_SUCCESS) {
        RegCloseKey(forbidden); cleanup(); return 4;
    }
    if (registry.state(moved) != platform::AssociationState::needs_repair ||
        registry.ignore_repair_prompt(moved) != ErrorCode::ok ||
        !registry.repair_prompt_ignored(moved) ||
        registry.repair_prompt_ignored(first)) {
        cleanup(); return 5;
    }
    if (!SetString(root, L"Software\\Classes\\.md\\OpenWithProgids",
            L"Another.Editor", L"keep") ||
        registry.register_application(moved) != ErrorCode::ok ||
        registry.state(moved) != platform::AssociationState::current ||
        registry.repair_prompt_ignored(moved)) {
        cleanup(); return 6;
    }
    if (!SetString(root,
            L"Software\\Classes\\MarkdownMay.Document.Text\\shell\\open\\command",
            nullptr, L"\"C:\\Other.exe\" \"%1\"") ||
        !SetString(root, L"Software\\Classes\\MarkdownMay.Document.Markdown",
            L"ForeignValue", L"keep")) { cleanup(); return 70; }
    if (registry.unregister_application(moved) != ErrorCode::ok) { cleanup(); return 71; }
    if (ReadString(root, L"Software\\Classes\\.md\\OpenWithProgids",
            L"Another.Editor") != L"keep") { cleanup(); return 73; }
    if (ReadString(root,
            L"Software\\Classes\\MarkdownMay.Document.Text\\shell\\open\\command",
            nullptr) != L"\"C:\\Other.exe\" \"%1\"") { cleanup(); return 74; }
    if (ReadString(root, L"Software\\Classes\\MarkdownMay.Document.Markdown",
            L"ForeignValue") != L"keep") { cleanup(); return 75; }
    if (std::wstring(platform::DefaultAppsSettingsUri()) !=
        L"ms-settings:defaultapps?registeredAppUser=Markdown%20May") {
        cleanup(); return 8;
    }
    cleanup();
    return 0;
}
