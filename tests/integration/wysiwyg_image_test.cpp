#include "markdownmay/editor/richedit_host.hpp"

#include <windows.h>
#include <ole2.h>
#include <richedit.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {
const markdownmay::document::Node* FirstImage(const markdownmay::document::Node& node) {
    if (node.kind == markdownmay::document::NodeKind::image) return &node;
    for (const auto& child : node.children) {
        if (const auto* found = FirstImage(*child)) return found;
    }
    return nullptr;
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

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    using namespace markdownmay;
    OleInitialize(nullptr);
    const wchar_t class_name[] = L"MarkdownMayImageTest";
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = instance;
    window_class.lpszClassName = class_name;
    RegisterClassW(&window_class);
    const auto parent = CreateWindowExW(0, class_name, L"", WS_OVERLAPPEDWINDOW,
        0, 0, 640, 480, nullptr, nullptr, instance, nullptr);
    if (!parent) return 1;
    document::DocumentSession session("图片：");
    editor::RichEditHost host(session);
    if (host.create(parent, {0, 0, 600, 400}) != ErrorCode::ok) return 2;
    const auto control = host.handle();
    SendMessageW(control, EM_SETSEL, 3, 3);
    if (host.insert_image_reference("https://example.invalid/a.png", "示例") != ErrorCode::ok)
        return 3;
    std::wstring visible(static_cast<std::size_t>(GetWindowTextLengthW(control)) + 1, L'\0');
    GetWindowTextW(control, visible.data(), static_cast<int>(visible.size()));
    if (visible.find(L"远程图片未加载") == std::wstring::npos) return 4;
    auto* image = FirstImage(*session.snapshot().semantic->root());
    if (!image || host.resize_image(image->id, 60) != ErrorCode::ok ||
        session.snapshot().source.find("markdownmay-width=60%") == std::string::npos) return 5;
    image = FirstImage(*session.snapshot().semantic->root());
    if (!image || host.replace_image(image->id, "missing.png", "替换图") != ErrorCode::ok)
        return 6;
    image = FirstImage(*session.snapshot().semantic->root());
    if (!image || host.remove_image(image->id) != ErrorCode::ok ||
        session.snapshot().source != "图片：") return 7;
    if (host.undo() != ErrorCode::ok || !FirstImage(*session.snapshot().semantic->root())) return 8;

    const auto directory = std::filesystem::temp_directory_path() /
        (L"markdownmay-wysiwyg-image-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto document_path = directory / L"note.md";
    const auto bitmap = directory / L"one.bmp";
    { std::ofstream file(document_path); file << "note"; }
    WriteBmp(bitmap);
    document::DocumentSession local_session("![本地](one.bmp)");
    editor::RichEditHost local_host(local_session);
    local_host.set_document_path(document_path);
    if (local_host.create(parent, {0, 0, 600, 400}) != ErrorCode::ok) return 9;
    if (!FirstImage(*local_session.snapshot().semantic->root())) return 10;
    std::filesystem::remove_all(directory);
    DestroyWindow(parent);
    OleUninitialize();
    return 0;
}
