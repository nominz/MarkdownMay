#pragma once

#include "markdownmay/export/export_task.hpp"

namespace markdownmay::exporting {

struct PdfOptions final {
    bool landscape{};
    double margin_points{56.7};
};

[[nodiscard]] ErrorCode WritePdf(const ExportDocument&, const std::filesystem::path&,
    const CancellationToken&, const ExportProgressSink&, const PdfOptions& = {});
[[nodiscard]] ErrorCode ValidatePdf(const std::filesystem::path&);
[[nodiscard]] ErrorCode ExportPdf(const ExportDocument&, const std::filesystem::path&,
    const CancellationToken& = {}, const ExportProgressSink& = {}, const PdfOptions& = {});

}  // namespace markdownmay::exporting
