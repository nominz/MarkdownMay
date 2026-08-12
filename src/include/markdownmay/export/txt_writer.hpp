#pragma once

#include "markdownmay/export/export_task.hpp"

namespace markdownmay::exporting {

[[nodiscard]] ErrorCode WriteTxt(
    const ExportDocument& document,
    const std::filesystem::path& temporary,
    const CancellationToken& cancellation,
    const ExportProgressSink& progress);

[[nodiscard]] ErrorCode ValidateTxt(
    const std::filesystem::path& temporary);

[[nodiscard]] ErrorCode ExportTxt(
    const ExportDocument& document,
    const std::filesystem::path& target,
    const CancellationToken& cancellation = {},
    const ExportProgressSink& progress = {});

}  // namespace markdownmay::exporting
