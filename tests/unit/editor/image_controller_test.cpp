#include "markdownmay/editor/image_controller.hpp"
#include "markdownmay/editor/image_object.hpp"
#include "markdownmay/editor/rich_projection.hpp"

#include <windows.h>
#include <objbase.h>

#include <filesystem>
#include <fstream>

namespace {
const markdownmay::document::Node* FirstImage(const markdownmay::document::Node& node) {
    if (node.kind == markdownmay::document::NodeKind::image) return &node;
    for (const auto& child : node.children) {
        if (const auto* found = FirstImage(*child)) return found;
    }
    return nullptr;
}
void Images(const markdownmay::document::Node& node,
            std::vector<const markdownmay::document::Node*>& output) {
    if (node.kind == markdownmay::document::NodeKind::image) output.push_back(&node);
    for (const auto& child : node.children) Images(*child, output);
}

void WriteBmp(const std::filesystem::path& path) {
    const unsigned char bytes[] = {
        0x42,0x4d,58,0,0,0,0,0,0,0,54,0,0,0,40,0,0,0,
        1,0,0,0,1,0,0,0,1,0,24,0,0,0,0,0,4,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0x20,0x80,0xff,0
    };
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}
}

int main() {
    using namespace markdownmay;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    document::DocumentSession session("before after");
    editor::ParagraphEditor editor(session);
    editor::ImageController images(session, editor);
    if (editor.set_selection({7, 7}) != ErrorCode::ok ||
        images.insert_reference("HTTPS://example.invalid/a.png", "远程") != ErrorCode::ok)
        return 1;
    auto* image = FirstImage(*session.snapshot().semantic->root());
    if (!image || session.snapshot().source != "before ![远程](HTTPS://example.invalid/a.png)after") return 2;
    auto projection = editor::BuildInlineProjection(*session.snapshot().semantic,
        session.snapshot().source, L"C:\\work\\note.md");
    if (projection.text.find("远程图片未加载") == std::string::npos) return 3;
    if (images.replace(image->id, "local.png", "本地图") != ErrorCode::ok) return 4;
    image = FirstImage(*session.snapshot().semantic->root());
    if (!image || images.set_display_percent(image->id, 75) != ErrorCode::ok ||
        session.snapshot().source.find("markdownmay-width=75%") == std::string::npos) return 5;
    projection = editor::BuildInlineProjection(*session.snapshot().semantic,
        session.snapshot().source, L"C:\\work\\note.md");
    bool found_percent = false;
    for (const auto& span : projection.spans)
        if (span.kind == document::NodeKind::image && span.image_display_percent == 75)
            found_percent = true;
    if (!found_percent) return 10;
    image = FirstImage(*session.snapshot().semantic->root());
    if (!image || images.remove(image->id) != ErrorCode::ok ||
        session.snapshot().source != "before after") return 6;
    if (editor.undo() != ErrorCode::ok || !FirstImage(*session.snapshot().semantic->root())) return 7;

    const auto directory = std::filesystem::temp_directory_path() /
        (L"markdownmay-image-editor-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto document_path = directory / L"note.md";
    const auto bitmap = directory / L"one.bmp";
    { std::ofstream document_file(document_path); document_file << "note"; }
    WriteBmp(bitmap);
    const auto object = editor::LoadImageObject(document_path, "one.bmp", "one", 400);
    if (object.state != editor::ImageDisplayState::ready || object.pixel_width != 1 ||
        object.pixel_height != 1 || object.display_percent != 300) return 8;
    document::DocumentSession imported("x");
    editor::ParagraphEditor imported_editor(imported);
    editor::ImageController imported_images(imported, imported_editor);
    if (imported_editor.set_selection({1, 1}) != ErrorCode::ok ||
        imported_images.insert_file(document_path, bitmap, true, "粘贴图") != ErrorCode::ok ||
        imported.snapshot().source.find("note.assets/one.bmp") == std::string::npos) return 9;

    const auto managed_path = directory / L"note.assets" / L"one.bmp";
    document::DocumentSession managed(
        "![第一](note.assets/one.bmp) ![第二](note.assets/one.bmp)");
    editor::ParagraphEditor managed_editor(managed);
    editor::ImageController managed_images(managed, managed_editor);
    std::vector<const document::Node*> found;
    Images(*managed.snapshot().semantic->root(), found);
    if (found.size() != 2 ||
        managed_images.remove_managed(document_path, found[0]->id) != ErrorCode::ok ||
        !std::filesystem::exists(managed_path)) return 11;
    found.clear(); Images(*managed.snapshot().semantic->root(), found);
    if (found.size() != 1 ||
        managed_images.remove_managed(document_path, found[0]->id) != ErrorCode::ok ||
        std::filesystem::exists(managed_path) ||
        !std::filesystem::exists(directory / L"note.assets" / L"del_one.bmp")) return 12;
    if (managed_images.undo() != ErrorCode::ok || !std::filesystem::exists(managed_path) ||
        !FirstImage(*managed.snapshot().semantic->root())) return 13;
    if (managed_images.redo() != ErrorCode::ok || std::filesystem::exists(managed_path)) return 14;
    if (managed_images.undo() != ErrorCode::ok || !std::filesystem::exists(managed_path)) return 15;
    if (managed_images.undo() != ErrorCode::ok) return 18;
    found.clear(); Images(*managed.snapshot().semantic->root(), found);
    if (found.size() != 2 || managed_images.redo() != ErrorCode::ok) return 19;
    found.clear(); Images(*managed.snapshot().semantic->root(), found);
    if (found.size() != 1 || managed_images.redo() != ErrorCode::ok ||
        std::filesystem::exists(managed_path)) return 20;
    if (managed_images.undo() != ErrorCode::ok || !std::filesystem::exists(managed_path)) return 21;
    found.clear(); Images(*managed.snapshot().semantic->root(), found);
    if (found.size() != 1 ||
        managed_images.remove_managed(document_path, found[0]->id) != ErrorCode::ok) return 16;
    { std::ofstream occupied(managed_path); occupied << "new owner"; }
    if (managed_images.undo() != ErrorCode::image_restore_name_conflict ||
        managed.snapshot().source.find("one_restored_2.bmp") == std::string::npos ||
        !std::filesystem::exists(directory / L"note.assets" / L"one_restored_2.bmp") ||
        !std::filesystem::exists(managed_path)) return 17;
    std::filesystem::remove_all(directory);
    CoUninitialize();
    return 0;
}
