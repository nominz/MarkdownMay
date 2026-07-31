#pragma once

#include "markdownmay/document_session.hpp"

#include <functional>

namespace markdownmay {

enum class ExportFormat : std::uint8_t {
    pdf,
    docx,
};

struct PageSettings final {
    std::uint32_t width_micrometers{210000};
    std::uint32_t height_micrometers{297000};
    std::uint32_t margin_top_micrometers{20000};
    std::uint32_t margin_right_micrometers{20000};
    std::uint32_t margin_bottom_micrometers{20000};
    std::uint32_t margin_left_micrometers{20000};
};

struct ExportRequest final {
    ExportFormat format{ExportFormat::pdf};
    Path target;
    std::shared_ptr<const SemanticDocument> document;
    Revision revision{};
    PageSettings page;
    CancellationToken cancellation;
};

struct ExportProgress final {
    std::uint32_t completed_units{};
    std::uint32_t total_units{};
    Utf8Text phase_key;
};

using ExportProgressCallback = std::function<void(const ExportProgress&)>;

class IExportService {
public:
    virtual ~IExportService() = default;
    [[nodiscard]] virtual Status export_document(
        const ExportRequest& request,
        ExportProgressCallback progress) const = 0;
};

}  // namespace markdownmay
