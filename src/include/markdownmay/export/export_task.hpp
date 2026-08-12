#pragma once

#include "markdownmay/export/export_document.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>

namespace markdownmay::exporting {

class CancellationToken final {
public:
    CancellationToken() = default;
    [[nodiscard]] bool is_cancelled() const noexcept;

private:
    explicit CancellationToken(std::shared_ptr<std::atomic_bool> state) noexcept;
    std::shared_ptr<std::atomic_bool> state_;
    friend class CancellationSource;
};

class CancellationSource final {
public:
    CancellationSource();
    [[nodiscard]] CancellationToken token() const noexcept;
    void cancel() noexcept;

private:
    std::shared_ptr<std::atomic_bool> state_;
};

enum class ExportStage : std::uint8_t {
    preparing, writing, validating, committing, completed
};

struct ExportProgress final {
    ExportStage stage{ExportStage::preparing};
    std::uint32_t completed{};
    std::uint32_t total{100};
};

using ExportProgressSink = std::function<void(const ExportProgress&)>;
using ExportWriter = std::function<ErrorCode(
    const ExportDocument&,
    const std::filesystem::path& temporary,
    const CancellationToken&,
    const ExportProgressSink&)>;
using ExportValidator = std::function<ErrorCode(
    const std::filesystem::path& temporary)>;

[[nodiscard]] ErrorCode RunExportTask(
    const ExportDocument& document,
    const std::filesystem::path& target,
    const ExportWriter& writer,
    const ExportValidator& validator,
    const CancellationToken& cancellation = {},
    const ExportProgressSink& progress = {});

}  // namespace markdownmay::exporting
