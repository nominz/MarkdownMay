#pragma once
#include "markdownmay/export/export_task.hpp"
namespace markdownmay::exporting {
[[nodiscard]] ErrorCode WriteDocx(const ExportDocument&,const std::filesystem::path&,const CancellationToken&,const ExportProgressSink&);
[[nodiscard]] ErrorCode ValidateDocx(const std::filesystem::path&);
[[nodiscard]] ErrorCode ExportDocx(const ExportDocument&,const std::filesystem::path&,
    const CancellationToken& cancellation = {},const ExportProgressSink& progress = {});
}
