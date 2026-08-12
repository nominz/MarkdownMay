#pragma once

#include "markdownmay/export/export_task.hpp"

namespace markdownmay::exporting {

[[nodiscard]] ErrorCode WriteHtml(const ExportDocument&, const std::filesystem::path&,
                                  const CancellationToken&, const ExportProgressSink&);
[[nodiscard]] ErrorCode ValidateHtml(const std::filesystem::path&);
[[nodiscard]] ErrorCode ExportHtml(const ExportDocument&, const std::filesystem::path&,
                                   const CancellationToken& = {},
                                   const ExportProgressSink& = {});

}  // namespace markdownmay::exporting
