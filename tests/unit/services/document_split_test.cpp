#include "markdownmay/services/document_split.hpp"
#include "markdownmay/fileio/file_service.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>

int main() {
    using namespace markdownmay;
    using namespace markdownmay::document;
    using namespace markdownmay::fileio;
    using namespace markdownmay::services;
    const auto root = std::filesystem::temp_directory_path() /
        (L"markdownmay-split-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ignored; std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const auto cleanup = [&] { std::filesystem::remove_all(root, ignored); };

    DocumentSession markdown("# 前段\r\n\r\n后段 😀\r\n");
    const auto original = markdown.snapshot();
    const auto split_at = original.source.find("后段");
    SplitRequest request{original, root / L"原文.md", root / L"前段.md",
                         root / L"后段.md", split_at,
                         TextEncoding::utf16_le, LineEnding::crlf};
    auto plan = DocumentSplitService::Prepare(request);
    if (!plan.is_ok() || plan.value().first_source + plan.value().second_source !=
            original.source) { cleanup(); return 1; }
    if (DocumentSplitService::Commit(plan.value()) != ErrorCode::ok) {
        cleanup(); return 2;
    }
    auto first = LoadTextFile(root / L"前段.md");
    auto second = LoadTextFile(root / L"后段.md");
    if (!first.is_ok() || !second.is_ok() ||
        first.value().encoding != TextEncoding::utf16_le ||
        second.value().encoding != TextEncoding::utf16_le ||
        first.value().source + second.value().source != original.source ||
        markdown.snapshot().source != original.source || markdown.is_dirty()) {
        cleanup(); return 3;
    }

    DocumentSession plain("# 不是标题\n第二行\n", DocumentKind::plain_text);
    SplitRequest plain_request{plain.snapshot(), root / L"原文.txt",
        root / L"一.txt", root / L"二.txt",
        plain.snapshot().source.find("第二行"),
        TextEncoding::utf8, LineEnding::lf};
    auto plain_plan = DocumentSplitService::Prepare(plain_request);
    if (!plain_plan.is_ok() || plain_plan.value().requires_confirmation() ||
        DocumentSplitService::Commit(plain_plan.value()) != ErrorCode::ok) {
        cleanup(); return 4;
    }
    auto wrong_extension = plain_request;
    wrong_extension.first_target = root / L"错误.md";
    if (DocumentSplitService::Prepare(wrong_extension).error() !=
        ErrorCode::split_target_conflict) { cleanup(); return 5; }

    const auto utf8_middle = plain.snapshot().source.find("不") + 1;
    plain_request.utf8_offset = utf8_middle;
    if (DocumentSplitService::Prepare(plain_request).error() !=
        ErrorCode::split_invalid_position) { cleanup(); return 6; }

    DocumentSession rollback("前\n后\n");
    SplitRequest rollback_request{rollback.snapshot(), root / L"原.md",
        root / L"回滚一.md", root / L"竞争.md", 4,
        TextEncoding::utf8, LineEnding::lf};
    auto rollback_plan = DocumentSplitService::Prepare(rollback_request);
    if (!rollback_plan.is_ok()) { cleanup(); return 7; }
    const auto failed = DocumentSplitService::Commit(
        rollback_plan.value(), false,
        [](const std::filesystem::path&, const std::filesystem::path& second_target) {
            std::ofstream competing(second_target, std::ios::binary);
            competing << "other";
            return ErrorCode::ok;
        });
    if (failed != ErrorCode::split_commit_failed ||
        std::filesystem::exists(root / L"回滚一.md") ||
        !std::filesystem::exists(root / L"竞争.md") ||
        rollback.snapshot().source != "前\n后\n" || rollback.is_dirty()) {
        cleanup(); return 8;
    }

    SplitRequest empty_request{plain.snapshot(), root / L"空原文.txt",
        root / L"空前.txt", root / L"全后.txt", 0,
        TextEncoding::utf8, LineEnding::lf};
    auto empty_plan = DocumentSplitService::Prepare(empty_request);
    if (!empty_plan.is_ok() ||
        DocumentSplitService::Commit(empty_plan.value()) != ErrorCode::ok ||
        std::filesystem::file_size(root / L"空前.txt") != 0) {
        cleanup(); return 9;
    }

    cleanup();
    return 0;
}
