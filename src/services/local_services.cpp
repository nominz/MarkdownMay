#include "markdownmay/services/local_services.hpp"

#include "markdownmay/fileio/file_service.hpp"
#include "markdownmay/fileio/path_utils.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cwchar>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace markdownmay::services {
namespace {

std::map<std::string, std::string> ParseIni(std::string_view text) {
    std::map<std::string, std::string> values;
    std::istringstream lines{std::string(text)};
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos || separator == 0) continue;
        values[line.substr(0, separator)] = line.substr(separator + 1);
    }
    return values;
}

template <typename Integer>
bool ParseInteger(std::string_view text, Integer& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

std::string HexEncode(std::string_view input) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result; result.reserve(input.size() * 2);
    for (const unsigned char value : input) {
        result.push_back(digits[value >> 4U]); result.push_back(digits[value & 0xfU]);
    }
    return result;
}

std::string PathUtf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::filesystem::path Utf8Path(std::string_view text) {
    std::u8string value;
    value.reserve(text.size());
    for (const unsigned char byte : text) {
        value.push_back(static_cast<char8_t>(byte));
    }
    return std::filesystem::path(value);
}

bool HexDecode(std::string_view input, std::string& output) {
    if (input.size() % 2 != 0) return false;
    const auto nibble = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    };
    output.clear(); output.reserve(input.size() / 2);
    for (std::size_t index = 0; index < input.size(); index += 2) {
        const int high = nibble(input[index]); const int low = nibble(input[index + 1]);
        if (high < 0 || low < 0) return false;
        output.push_back(static_cast<char>((high << 4) | low));
    }
    return true;
}

ErrorCode SaveUtf8(const std::filesystem::path& path, std::string_view text,
                   ErrorCode failure) {
    std::error_code ignored;
    std::filesystem::create_directories(path.parent_path(), ignored);
    const auto result = fileio::SaveTextFileAtomic({
        path, text, fileio::TextEncoding::utf8, fileio::LineEnding::lf});
    return result == ErrorCode::ok ? ErrorCode::ok : failure;
}

std::uint64_t HashPath(std::wstring_view path) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (wchar_t value : path) {
        value = static_cast<wchar_t>(std::towlower(value));
        hash ^= static_cast<std::uint16_t>(value); hash *= 1099511628211ULL;
    }
    return hash;
}

}  // namespace

SettingsStore::SettingsStore(std::filesystem::path file) : file_(std::move(file)) {}
Result<Settings> SettingsStore::load() const {
    if (!std::filesystem::exists(file_)) return Result<Settings>::success({});
    auto loaded = fileio::LoadTextFile(file_, 1024 * 1024);
    if (!loaded.is_ok()) return Result<Settings>::failure(ErrorCode::settings_load_failed);
    const auto corrupt = [&]() {
        const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        std::error_code ignored;
        std::filesystem::rename(
            file_, std::filesystem::path(file_.wstring() + L".bad-" +
                                         std::to_wstring(stamp)), ignored);
        return Result<Settings>::success(Settings{});
    };
    auto values = ParseIni(loaded.value().source); Settings settings;
    settings.unknown = values;
    auto take = [&](const char* key) -> std::string {
        const auto found = settings.unknown.find(key);
        if (found == settings.unknown.end()) return {};
        auto result = found->second; settings.unknown.erase(found); return result;
    };
    const auto version = take("schema_version");
    const auto mode = take("default_mode");
    const auto theme = take("follow_system_theme");
    const auto interval = take("recovery_interval_seconds");
    if (!version.empty() && !ParseInteger(version, settings.schema_version))
        return corrupt();
    if (!mode.empty()) {
        if (mode == "render") settings.default_mode = DefaultViewMode::render;
        else if (mode == "source") settings.default_mode = DefaultViewMode::source;
        else if (mode == "split") settings.default_mode = DefaultViewMode::split;
        else return corrupt();
    }
    if (!theme.empty()) {
        if (theme == "true") settings.follow_system_theme = true;
        else if (theme == "false") settings.follow_system_theme = false;
        else return corrupt();
    }
    if (!interval.empty() && (!ParseInteger(interval, settings.recovery_interval_seconds) ||
        settings.recovery_interval_seconds < 10 || settings.recovery_interval_seconds > 3600))
        return corrupt();
    return Result<Settings>::success(std::move(settings));
}
ErrorCode SettingsStore::save(const Settings& settings) const {
    std::ostringstream output;
    output << "schema_version=" << settings.schema_version << "\n"
           << "default_mode=" << (settings.default_mode == DefaultViewMode::render ? "render" :
               settings.default_mode == DefaultViewMode::source ? "source" : "split") << "\n"
           << "follow_system_theme=" << (settings.follow_system_theme ? "true" : "false") << "\n"
           << "recovery_interval_seconds=" << settings.recovery_interval_seconds << "\n";
    for (const auto& [key, value] : settings.unknown) output << key << "=" << value << "\n";
    return SaveUtf8(file_, output.str(), ErrorCode::settings_save_failed);
}

RecoveryStore::RecoveryStore(std::filesystem::path directory) : directory_(std::move(directory)) {}
ErrorCode RecoveryStore::write(const RecoverySnapshot& snapshot) const {
    std::ostringstream output;
    output << "MMREC1\n" << snapshot.document << "\n" << snapshot.revision << "\n"
           << HexEncode(PathUtf8(snapshot.original_path)) << "\n" << HexEncode(snapshot.source) << "\n";
    return SaveUtf8(directory_ / (std::to_string(snapshot.document) + ".recovery"),
                    output.str(), ErrorCode::recovery_write_failed);
}
Result<std::vector<RecoverySnapshot>> RecoveryStore::discover() const {
    std::vector<RecoverySnapshot> result;
    if (!std::filesystem::exists(directory_)) return Result<std::vector<RecoverySnapshot>>::success({});
    for (const auto& entry : std::filesystem::directory_iterator(directory_)) {
        if (!entry.is_regular_file() || entry.path().extension() != L".recovery") continue;
        auto loaded = fileio::LoadTextFile(entry.path(), 110ULL * 1024ULL * 1024ULL);
        if (!loaded.is_ok()) return Result<std::vector<RecoverySnapshot>>::failure(ErrorCode::recovery_read_failed);
        std::istringstream lines(loaded.value().source); std::string magic, document, revision, path, source;
        std::getline(lines, magic); std::getline(lines, document); std::getline(lines, revision);
        std::getline(lines, path); std::getline(lines, source);
        RecoverySnapshot snapshot; std::string decoded_path;
        if (magic != "MMREC1" || !ParseInteger(document, snapshot.document) ||
            !ParseInteger(revision, snapshot.revision) || !HexDecode(path, decoded_path) ||
            !HexDecode(source, snapshot.source))
            return Result<std::vector<RecoverySnapshot>>::failure(ErrorCode::recovery_read_failed);
        snapshot.original_path = Utf8Path(decoded_path);
        result.push_back(std::move(snapshot));
    }
    return Result<std::vector<RecoverySnapshot>>::success(std::move(result));
}
ErrorCode RecoveryStore::discard(std::uint64_t document) const {
    std::error_code error;
    const bool removed = std::filesystem::remove(
        directory_ / (std::to_string(document) + ".recovery"), error);
    return (!error && (removed || !std::filesystem::exists(directory_ /
        (std::to_string(document) + ".recovery")))) ? ErrorCode::ok
                                                     : ErrorCode::recovery_discard_failed;
}

RecentFilesStore::RecentFilesStore(std::filesystem::path file, std::size_t maximum)
    : file_(std::move(file)), maximum_(maximum) {}
Result<std::vector<std::filesystem::path>> RecentFilesStore::load() const {
    std::vector<std::filesystem::path> result;
    if (!std::filesystem::exists(file_)) return Result<std::vector<std::filesystem::path>>::success({});
    auto loaded = fileio::LoadTextFile(file_, 1024 * 1024);
    if (!loaded.is_ok()) return Result<std::vector<std::filesystem::path>>::failure(ErrorCode::settings_load_failed);
    std::istringstream lines(loaded.value().source); std::string line;
    while (std::getline(lines, line) && result.size() < maximum_) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string decoded;
        if (!line.empty() && HexDecode(line, decoded)) result.push_back(Utf8Path(decoded));
    }
    return Result<std::vector<std::filesystem::path>>::success(std::move(result));
}
ErrorCode RecentFilesStore::touch(const std::filesystem::path& path) const {
    auto current = load(); if (!current.is_ok()) return ErrorCode::settings_load_failed;
    auto normalized = fileio::NormalizeAbsolutePath(path);
    if (!normalized.is_ok()) return ErrorCode::settings_save_failed;
    auto values = current.value();
    values.erase(std::remove_if(values.begin(), values.end(), [&](const auto& existing) {
        return _wcsicmp(existing.c_str(), normalized.value().c_str()) == 0;
    }), values.end());
    values.insert(values.begin(), normalized.value());
    if (values.size() > maximum_) values.resize(maximum_);
    std::string output;
    for (const auto& value : values) output += HexEncode(PathUtf8(value)) + "\n";
    return SaveUtf8(file_, output, ErrorCode::settings_save_failed);
}
ErrorCode RecentFilesStore::clear() const {
    std::error_code error;
    std::filesystem::remove(file_, error);
    return error ? ErrorCode::settings_save_failed : ErrorCode::ok;
}

PrivacyLogger::PrivacyLogger(std::filesystem::path file) : file_(std::move(file)) {}
ErrorCode PrivacyLogger::append(const DiagnosticRecord& record) const {
    std::error_code error; std::filesystem::create_directories(file_.parent_path(), error);
    std::ofstream output(file_, std::ios::binary | std::ios::app);
    if (!output) return ErrorCode::log_write_failed;
    std::string safe_module;
    for (const unsigned char value : record.module) {
        safe_module.push_back((std::isalnum(value) != 0 || value == '_' || value == '-')
            ? static_cast<char>(value) : '_');
    }
    output << "error=" << record.error_code << " module=" << safe_module
           << " system=" << record.system_code << " elapsed_us=" << record.elapsed_microseconds;
    if (!record.related_path.empty()) {
        output << " path_hash=" << std::hex << std::setw(16) << std::setfill('0')
               << HashPath(record.related_path.wstring()) << std::dec
               << " extension=" << PathUtf8(record.related_path.extension());
    }
    output << "\n"; output.flush();
    return output.good() ? ErrorCode::ok : ErrorCode::log_write_failed;
}

}  // namespace markdownmay::services
