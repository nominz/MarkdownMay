#include "markdownmay/editor/clipboard_controller.hpp"
#include "markdownmay/editor/find_replace_controller.hpp"

#include <windows.h>
#include <objbase.h>

#include <filesystem>
#include <fstream>

int main() {
    using namespace markdownmay;
    const auto markdown = editor::ClipboardController::HtmlToMarkdown(
        "<h2>标题</h2><p><strong>粗体</strong>、<a href=\"local.md\">链接</a>"
        "<script>bad()</script><a href=\"javascript:bad()\">危险</a><br>末尾</p>");
    if (markdown.find("## 标题") == std::string::npos ||
        markdown.find("**粗体**") == std::string::npos ||
        markdown.find("[链接](local.md)") == std::string::npos ||
        markdown.find("bad") != std::string::npos || markdown.find("javascript:") != std::string::npos ||
        markdown.find("[危险](#)") == std::string::npos) return 1;

    document::DocumentSession session("Alpha 中文 alpha");
    editor::ParagraphEditor paragraphs(session);
    editor::ImageController images(session, paragraphs);
    editor::ClipboardController clipboard(session, paragraphs, images);
    editor::FindReplaceController find(session, paragraphs);
    if (paragraphs.set_selection({0, 0}) != ErrorCode::ok) return 2;
    auto match = find.find("alpha", true, false);
    if (!match.is_ok() || match.value().anchor != 0) return 3;
    if (find.replace_current("alpha", "Beta", false) != ErrorCode::ok ||
        session.snapshot().source.find("Beta") != 0) return 4;
    auto replaced = find.replace_all("alpha", "Gamma", false);
    if (!replaced.is_ok() || replaced.value() != 1 ||
        session.snapshot().source != "Beta 中文 Gamma") return 5;
    if (paragraphs.undo() != ErrorCode::ok || session.snapshot().source != "Beta 中文 alpha") return 6;
    if (paragraphs.set_selection({session.snapshot().source.size(), session.snapshot().source.size()}) !=
            ErrorCode::ok) return 10;
    auto backward = find.find("alpha", false, true, false);
    if (!backward.is_ok() || backward.value().anchor != 12 ||
        find.find("ALPHA", false, true, false).is_ok()) return 11;
    if (paragraphs.set_selection({session.snapshot().source.size(), session.snapshot().source.size()}) !=
            ErrorCode::ok || clipboard.paste_html("<p><em>斜体</em></p>") != ErrorCode::ok ||
        session.snapshot().source.find("*斜体*") == std::string::npos) return 7;

    const auto directory = std::filesystem::temp_directory_path() /
        (L"markdownmay-input-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto document_path = directory / L"note.md";
    const auto image_path = directory / L"drop.png";
    const auto other_path = directory / L"other.md";
    { std::ofstream file(document_path); file << "note"; }
    { std::ofstream file(image_path); file << "image"; }
    { std::ofstream file(other_path); file << "other"; }
    document::DocumentSession dropped("");
    editor::ParagraphEditor dropped_paragraphs(dropped);
    editor::ImageController dropped_images(dropped, dropped_paragraphs);
    editor::ClipboardController dropped_clipboard(dropped, dropped_paragraphs, dropped_images);
    const std::filesystem::path files[]{image_path, other_path};
    auto drop = dropped_clipboard.drop_files(document_path, files, true);
    if (!drop.is_ok() || drop.value().inserted_images != 1 ||
        drop.value().documents_to_open.size() != 1 ||
        dropped.snapshot().source.find("note.assets/drop.png") == std::string::npos) return 8;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto bitmap = CreateBitmap(2, 2, 1, 32, nullptr);
    if (!bitmap || dropped_clipboard.paste_bitmap(document_path, bitmap) != ErrorCode::ok ||
        dropped.snapshot().source.find("image_") == std::string::npos) return 9;
    DeleteObject(bitmap);
    CoUninitialize();
    std::error_code ignored; std::filesystem::remove_all(directory, ignored);
    return 0;
}
