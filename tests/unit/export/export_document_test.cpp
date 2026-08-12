#include "markdownmay/export/export_document.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <variant>

int RunExportTaskTests();
int RunDocxWriterTests();
int RunPdfWriterTests();
int RunTxtWriterTests();

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

    const auto directory = std::filesystem::temp_directory_path() /
        ("markdownmay-export-resources-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto document_path = directory / "note.md";
    { std::ofstream image(directory / "image.png", std::ios::binary); image << "PNGDATA"; }
    document::DocumentSession resources_session(
        "![本地](image.png) ![远程](https://example.invalid/image.png) ![缺失](missing.png)\n");
    const auto resource_snapshot = resources_session.snapshot();
    auto resources = BuildExportDocument(resource_snapshot, resource_snapshot.source_revision,
        ExportScope::full, ExportFormat::pdf, ExportContext{document_path, 1024, 1024});
    std::error_code cleanup_error;
    std::filesystem::remove_all(directory, cleanup_error);
    if (!resources.is_ok()) return 9;
    if (resources.value().resources.size() != 3) return 10;
    if (resources.value().resources[0].state != ExportResourceState::embedded) return 11;
    if (std::string(resources.value().resources[0].bytes.begin(),
            resources.value().resources[0].bytes.end()) != "PNGDATA") return 14;
    if (resources.value().resources[1].state != ExportResourceState::remote_blocked) return 12;
    if (resources.value().resources[2].state != ExportResourceState::missing) return 13;

    const auto task = RunExportTaskTests();
    if(task!=0)return task;const auto txt=RunTxtWriterTests();if(txt!=0)return txt;const auto pdf=RunPdfWriterTests();return pdf==0?RunDocxWriterTests():pdf;
}
