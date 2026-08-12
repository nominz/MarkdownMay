#include "markdownmay/export/txt_writer.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {
struct TemporaryDirectory final {
    std::filesystem::path path;
    ~TemporaryDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
}  // namespace

int RunTxtWriterTests() {
    using namespace markdownmay;
    using namespace markdownmay::exporting;
    const std::string source =
        "# 一级\n\n### 三级\n\n正文有 **粗体**、[链接](notes.md) 和 ![图](a.png)。\n\n"
        "3. 三\n4. 四\n   - 子项\n\n- [x] 完成\n- [ ] 待办\n\n"
        "```cpp\nint main() {}\n```\n\n```mermaid\nflowchart TD\nA-->B\n```\n\n"
        "| 姓名 | 状态 |\n| --- | --- |\n| 马冬梅 | 正常 |\n";
    document::DocumentSession session(source);
    const auto snapshot = session.snapshot();
    auto outline = BuildExportDocument(
        snapshot, snapshot.source_revision, ExportScope::outline, ExportFormat::txt);
    auto full = BuildExportDocument(
        snapshot, snapshot.source_revision, ExportScope::full, ExportFormat::txt);
    if (!outline.is_ok() || !full.is_ok()) return 30;

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory{std::filesystem::temp_directory_path() /
        ("markdownmay-txt-writer-" + std::to_string(nonce))};
    std::filesystem::create_directories(directory.path);
    const auto outline_path = directory.path / "outline.txt";
    const auto full_path = directory.path / "full.txt";
    if (ExportTxt(outline.value(), outline_path) != ErrorCode::ok ||
        ExportTxt(full.value(), full_path) != ErrorCode::ok) return 31;

    const auto outline_text = ReadAll(outline_path);
    if (outline_text != "一级\r\n\r\n    三级\r\n") return 32;
    const auto full_text = ReadAll(full_path);
    if (full_text.find("正文有 粗体、链接（notes.md） 和 图片：图（a.png）。") ==
            std::string::npos) return 40;
    if (full_text.find("3. 三\r\n4. 四\r\n  - 子项") == std::string::npos) return 41;
    if (full_text.find("- [x] 完成\r\n- [ ] 待办") == std::string::npos) return 42;
    if (full_text.find("int main() {}") == std::string::npos) return 43;
    if (full_text.find("[Mermaid 源码]\r\nflowchart TD\r\nA-->B") ==
            std::string::npos) return 44;
    if (full_text.find("姓名\t状态\r\n马冬梅\t正常") == std::string::npos) return 45;
    if (full_text.find("**") != std::string::npos || full_text.find("```") != std::string::npos ||
        full_text.find("\n") != std::string::npos && full_text.find("\r\n") == std::string::npos)
        return 34;
    if (ValidateTxt(full_path) != ErrorCode::ok) return 35;

    const auto invalid_path = directory.path / "invalid.txt";
    { std::ofstream invalid(invalid_path, std::ios::binary); invalid << "LF only\n"; }
    if (ValidateTxt(invalid_path) != ErrorCode::export_validation_failed) return 36;

    CancellationSource cancelled;
    cancelled.cancel();
    const auto cancelled_path = directory.path / "cancelled.txt";
    if (ExportTxt(full.value(), cancelled_path, cancelled.token()) !=
            ErrorCode::export_cancelled || std::filesystem::exists(cancelled_path)) return 37;
    return 0;
}
