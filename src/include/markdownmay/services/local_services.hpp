#pragma once

#include "markdownmay/core/result.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace markdownmay::services {

enum class DefaultViewMode : std::uint8_t { render, source, split };

struct Settings final {
    std::uint32_t schema_version{1};
    DefaultViewMode default_mode{DefaultViewMode::render};
    bool follow_system_theme{true};
    std::uint32_t recovery_interval_seconds{30};
    std::map<std::string, std::string> unknown;
};

class SettingsStore final {
public:
    explicit SettingsStore(std::filesystem::path file);
    [[nodiscard]] Result<Settings> load() const;
    [[nodiscard]] ErrorCode save(const Settings& settings) const;
private:
    std::filesystem::path file_;
};

struct RecoverySnapshot final {
    std::uint64_t document{};
    std::filesystem::path original_path;
    std::string source;
    std::uint64_t revision{};
};

class RecoveryStore final {
public:
    explicit RecoveryStore(std::filesystem::path directory);
    [[nodiscard]] ErrorCode write(const RecoverySnapshot& snapshot) const;
    [[nodiscard]] Result<std::vector<RecoverySnapshot>> discover() const;
    [[nodiscard]] ErrorCode discard(std::uint64_t document) const;
private:
    std::filesystem::path directory_;
};

class RecentFilesStore final {
public:
    RecentFilesStore(std::filesystem::path file, std::size_t maximum = 10);
    [[nodiscard]] Result<std::vector<std::filesystem::path>> load() const;
    [[nodiscard]] ErrorCode touch(const std::filesystem::path& path) const;
private:
    std::filesystem::path file_;
    std::size_t maximum_{};
};

struct DiagnosticRecord final {
    std::uint32_t error_code{};
    std::string module;
    std::uint32_t system_code{};
    std::uint64_t elapsed_microseconds{};
    std::filesystem::path related_path;
};

class PrivacyLogger final {
public:
    explicit PrivacyLogger(std::filesystem::path file);
    [[nodiscard]] ErrorCode append(const DiagnosticRecord& record) const;
private:
    std::filesystem::path file_;
};

}  // namespace markdownmay::services
