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

    document::DocumentSession wildcard_session("cat cot c中文t c？t\tend\nnext");
    editor::ParagraphEditor wildcard_editor(wildcard_session);
    editor::FindReplaceController wildcard_find(wildcard_session, wildcard_editor);
    if (wildcard_editor.set_selection({0, 0}) != ErrorCode::ok) return 12;
    auto wildcard = wildcard_find.find("c?t", true, true, true, true);
    if (!wildcard.is_ok() || wildcard.value().anchor != 0 || wildcard.value().caret != 3)
        return 13;
    if (wildcard_editor.set_selection({3, 3}) != ErrorCode::ok) return 14;
    wildcard = wildcard_find.find("c*t", true, true, false, true);
    if (!wildcard.is_ok() || wildcard.value().anchor != 4 ||
        wildcard.value().caret != wildcard_session.snapshot().source.find('\t'))
        return 15;
    if (wildcard_editor.set_selection({0, 0}) != ErrorCode::ok) return 16;
    auto full_width = wildcard_find.find("c？t", true, true, false, true);
    if (!full_width.is_ok() || full_width.value().caret - full_width.value().anchor != 5)
        return 21;
    if (wildcard_editor.set_selection({0, 0}) != ErrorCode::ok) return 17;
    auto special = wildcard_find.find("^tend^pnext", true, true, false, true);
    if (!special.is_ok()) return 18;
    if (wildcard_find.replace_current("^tend^pnext", "^t完成", true, true) != ErrorCode::ok ||
        wildcard_session.snapshot().source.find("\t完成") == std::string::npos) return 19;
    document::DocumentSession all_session("one AxxZ\ntwo AZ\nAline\nZ");
    editor::ParagraphEditor all_editor(all_session);
    editor::FindReplaceController all_find(all_session, all_editor);
    auto all_wildcards = all_find.replace_all("A*Z", "X", true, true);
    if (!all_wildcards.is_ok() || all_wildcards.value() != 2 ||
        all_session.snapshot().source != "one X\ntwo X\nAline\nZ") return 20;
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
