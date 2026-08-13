#include "markdownmay/services/document_combine.hpp"
#include "markdownmay/fileio/file_service.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>

namespace {
void Write(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
}
}

int main() {
    using namespace markdownmay;
    using namespace markdownmay::document;
    using namespace markdownmay::services;
    const auto root = std::filesystem::absolute(std::filesystem::temp_directory_path()) /
        (L"markdownmay-combine-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ignored; std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const auto cleanup = [&] { std::filesystem::remove_all(root, ignored); };

    const auto current_path = root / L"current" / L"报告.md";
    const auto source_path = root / L"source" / L"片段.md";
    const auto image = root / L"source" / L"图.png";
    Write(image, "image-bytes");
    Write(source_path,
        "[资料](docs/readme.txt)\n\n![图](图.png)\n\n![远程](https://example.invalid/a.png)\n\n"
        "```text\n[代码](do-not-rewrite.txt)\n```\n");
    DocumentSession session("开头\n结尾\n");
    InsertRequest request{session.snapshot(), current_path, source_path, 7};
    auto plan = DocumentCombineService::Prepare(request);
    if (!plan.is_ok()) { cleanup(); return 101; }
    if (plan.value().resources.size() != 1) { cleanup(); return 102; }
    if (plan.value().insertion.find("../source/docs/readme.txt") == std::string::npos)
        { cleanup(); return 103; }
    if (plan.value().insertion.find("报告.assets/图.png") == std::string::npos)
        { cleanup(); return 104; }
    if (plan.value().insertion.find("https://example.invalid/a.png") == std::string::npos)
        { cleanup(); return 105; }
    if (plan.value().insertion.find("[代码](do-not-rewrite.txt)") == std::string::npos)
        { cleanup(); return 106; }
    auto inserted = DocumentCombineService::Commit(session, plan.value(), 80);
    if (!inserted.is_ok() || inserted.value().created_resources.size() != 1 ||
        !std::filesystem::is_regular_file(inserted.value().created_resources[0]) ||
        session.snapshot().source.find("[资料]") == std::string::npos ||
        !session.is_dirty()) { cleanup(); return 2; }
    std::ifstream copied(inserted.value().created_resources[0], std::ios::binary);
    std::string copied_bytes((std::istreambuf_iterator<char>(copied)), {});
    if (copied_bytes != "image-bytes" || std::filesystem::file_size(source_path) == 0) {
        cleanup(); return 3;
    }

    const auto utf16_txt = root / L"纯文本.txt";
    const std::string txt_utf8 = "# 仍是普通字符\r\n";
    if (fileio::SaveTextFileAtomic({utf16_txt, txt_utf8,
            fileio::TextEncoding::utf16_be, fileio::LineEnding::crlf}) != ErrorCode::ok) {
        cleanup(); return 4;
    }
    DocumentSession plain("甲\n乙\n", DocumentKind::plain_text);
    auto txt_plan = DocumentCombineService::Prepare(
        {plain.snapshot(), root / L"当前.txt", utf16_txt, 4});
    if (!txt_plan.is_ok() || !txt_plan.value().resources.empty() ||
        txt_plan.value().insertion.find("# 仍是普通字符") == std::string::npos ||
        DocumentCombineService::Commit(plain, txt_plan.value(), 81).error() != ErrorCode::ok) {
        cleanup(); return 5;
    }
    if (plain.snapshot().semantic || plain.snapshot().source.find('#') == std::string::npos)
        { cleanup(); return 6; }

    DocumentSession markdown_text("正文\n");
    auto escaped_txt = DocumentCombineService::Prepare(
        {markdown_text.snapshot(), current_path, utf16_txt, 0});
    if (!escaped_txt.is_ok() || escaped_txt.value().insertion.find("\\# 仍是普通字符") ==
            std::string::npos) { cleanup(); return 11; }

    DocumentSession unsaved("正文\n");
    if (DocumentCombineService::Prepare(
            {unsaved.snapshot(), {}, source_path, 0}).error() !=
        ErrorCode::insert_target_unsaved) { cleanup(); return 7; }

    const auto bad = root / L"bad.docx"; Write(bad, "not supported");
    if (DocumentCombineService::Prepare(
            {unsaved.snapshot(), current_path, bad, 0}).error() !=
        ErrorCode::insert_source_unsupported) { cleanup(); return 8; }

    DocumentSession rollback("原文\n");
    auto rollback_plan = DocumentCombineService::Prepare(
        {rollback.snapshot(), root / L"回滚.md", source_path, 0});
    if (!rollback_plan.is_ok()) { cleanup(); return 9; }
    auto invalid_plan = rollback_plan.value();
    invalid_plan.utf8_offset = 9999;
    const auto resource_target = invalid_plan.resources.front().target;
    if (DocumentCombineService::Commit(rollback, invalid_plan, 82).error() !=
            ErrorCode::insert_transaction_failed ||
        std::filesystem::exists(resource_target) || rollback.snapshot().source != "原文\n") {
        cleanup(); return 10;
    }

    cleanup(); return 0;
}
