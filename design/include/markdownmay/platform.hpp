#pragma once

#include "markdownmay/error.hpp"

#include <span>

namespace markdownmay {

enum class AssociationExtension : std::uint8_t {
    md,
    markdown,
};

struct OpenRequest final {
    std::vector<Path> paths;
};

class IFileAssociationService {
public:
    virtual ~IFileAssociationService() = default;
    [[nodiscard]] virtual Status register_current_executable(
        std::span<const AssociationExtension> extensions) = 0;
    [[nodiscard]] virtual Status unregister_current_executable() = 0;
    [[nodiscard]] virtual Result<bool> needs_repair() const = 0;
    [[nodiscard]] virtual Status open_default_apps_ui() const = 0;
};

class ISingleInstanceService {
public:
    virtual ~ISingleInstanceService() = default;
    [[nodiscard]] virtual Result<bool> become_primary() = 0;
    [[nodiscard]] virtual Status send_to_primary(const OpenRequest& request) = 0;
};

}  // namespace markdownmay
