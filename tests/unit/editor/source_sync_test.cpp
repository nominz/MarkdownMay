#include "markdownmay/editor/source_sync.hpp"

#include "markdownmay/fileio/file_service.hpp"

#include <filesystem>
#include <fstream>

int main() {
    using namespace markdownmay;
    document::DocumentSession session("# 原文\n");
    editor::SourceSync sync(session);
    if (sync.synchronize("# 新标题\n\n正文\n") != ErrorCode::ok) return 1;
    const auto changed = session.snapshot();
    if (changed.source != "# 新标题\n\n正文\n" ||
        changed.parsed_revision != changed.source_revision || !session.is_dirty()) return 2;

    const auto directory = std::filesystem::temp_directory_path() /
        L"markdownmay-source-sync-test";
    std::filesystem::create_directories(directory);
    const auto target = directory / L"source.md";
    std::error_code ignored;
    std::filesystem::remove(target, ignored);
    if (sync.save(target, fileio::TextEncoding::utf8,
                  fileio::LineEnding::crlf) != ErrorCode::ok || session.is_dirty()) return 3;
    const auto loaded = fileio::LoadTextFile(target);
    if (!loaded.is_ok() || loaded.value().source.find("\r\n") == std::string::npos) return 4;

    std::string invalid = "line\n";
    invalid.push_back(static_cast<char>(0xff));
    if (sync.synchronize(invalid) != ErrorCode::file_encoding_invalid ||
        sync.diagnostics().size() != 1 || sync.diagnostics()[0].line != 2 ||
        sync.diagnostics()[0].column != 1 ||
        session.snapshot().source != changed.source) return 5;
    std::filesystem::remove_all(directory, ignored);
    return 0;
}
