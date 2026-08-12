#include "markdownmay/export/export_document.hpp"

#include <variant>

int RunExportTaskTests();

int main() {
    using namespace markdownmay;
    using namespace markdownmay::exporting;

    document::DocumentSession session("# 一级\n\n正文\n\n### 三级\n");
    const auto snapshot = session.snapshot();

    if (!IsSupportedCombination(ExportScope::outline, ExportFormat::pdf) ||
        !IsSupportedCombination(ExportScope::outline, ExportFormat::docx) ||
        !IsSupportedCombination(ExportScope::outline, ExportFormat::txt) ||
        IsSupportedCombination(ExportScope::outline, ExportFormat::html) ||
        !IsSupportedCombination(ExportScope::full, ExportFormat::html)) return 1;

    auto outline = BuildExportDocument(
        snapshot, snapshot.source_revision, ExportScope::outline, ExportFormat::txt);
    if (!outline.is_ok() || outline.value().revision != snapshot.source_revision ||
        outline.value().blocks.size() != 2) return 2;
    const auto* first = std::get_if<document::HeadingAttributes>(
        &outline.value().blocks[0].attributes);
    const auto* second = std::get_if<document::HeadingAttributes>(
        &outline.value().blocks[1].attributes);
    if (!first || first->level != 1 || !second || second->level != 3 ||
        outline.value().blocks[0].source_id == 0) return 3;

    auto full = BuildExportDocument(
        snapshot, snapshot.source_revision, ExportScope::full, ExportFormat::pdf);
    if (!full.is_ok() || full.value().blocks.size() != 3 ||
        full.value().blocks[1].kind != document::NodeKind::paragraph) return 4;

    auto invalid = BuildExportDocument(
        snapshot, snapshot.source_revision, ExportScope::outline, ExportFormat::html);
    if (invalid.is_ok() || invalid.error() != ErrorCode::export_invalid_options) return 5;

    auto stale_expected = BuildExportDocument(
        snapshot, snapshot.source_revision + 1, ExportScope::full, ExportFormat::txt);
    if (stale_expected.is_ok() ||
        stale_expected.error() != ErrorCode::export_revision_not_current) return 6;

    auto stale_parse = snapshot;
    stale_parse.parsed_revision = 0;
    auto rejected = BuildExportDocument(
        stale_parse, stale_parse.source_revision, ExportScope::full, ExportFormat::docx);
    if (rejected.is_ok() ||
        rejected.error() != ErrorCode::export_revision_not_current) return 7;

    auto no_semantic = snapshot;
    no_semantic.semantic.reset();
    rejected = BuildExportDocument(
        no_semantic, no_semantic.source_revision, ExportScope::full, ExportFormat::pdf);
    if (rejected.is_ok() ||
        rejected.error() != ErrorCode::export_revision_not_current) return 8;

    return RunExportTaskTests();
}
