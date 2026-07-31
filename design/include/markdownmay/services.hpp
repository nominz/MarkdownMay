#pragma once

#include "markdownmay/document_session.hpp"

#include <chrono>

namespace markdownmay {

struct Settings final {
    ViewMode default_mode{ViewMode::render};
    bool follow_system_theme{true};
    std::uint32_t recovery_interval_seconds{30};
};

struct RecoverySnapshot final {
    DocumentId document{};
    Path original_path;
    Utf8Text source;
    Revision revision{};
};

class ISettingsService {
public:
    virtual ~ISettingsService() = default;
    [[nodiscard]] virtual Result<Settings> load() const = 0;
    [[nodiscard]] virtual Status save(const Settings& settings) const = 0;
};

class IRecoveryService {
public:
    virtual ~IRecoveryService() = default;
    [[nodiscard]] virtual Status write(
        const RecoverySnapshot& snapshot) const = 0;
    [[nodiscard]] virtual Result<std::vector<RecoverySnapshot>> discover() const = 0;
    [[nodiscard]] virtual Status discard(DocumentId document) const = 0;
};

}  // namespace markdownmay
