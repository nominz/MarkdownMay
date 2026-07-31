#include "markdownmay/fileio/image_store.hpp"
#include "markdownmay/fileio/path_utils.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <vector>

namespace {
void Write(const std::filesystem::path& path, std::string_view content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
}
}

int main() {
    using namespace markdownmay;
    using namespace markdownmay::fileio;
    const auto root = std::filesystem::temp_directory_path() /
        (L"markdownmay-images-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ignored; std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const auto cleanup = [&] { std::filesystem::remove_all(root, ignored); };
    const auto document = root / L"报告.md";
    const auto first_source = root / L"图.png";
    const auto second_source_dir = root / L"other";
    std::filesystem::create_directories(second_source_dir);
    const auto second_source = second_source_dir / L"图.png";
    Write(first_source, "first-image"); Write(second_source, "second-image");

    const auto remote = ResolveImageReference(document, "HTTPS://example.invalid/a.png");
    if (remote.kind != ImageLocationKind::remote || !remote.resolved_path.empty()) {
        cleanup(); return 1;
    }
    auto external = ImportImageFile(document, first_source, false);
    if (!external.is_ok() || external.value().kind != ImageLocationKind::external_local) {
        cleanup(); return 2;
    }
    auto imported = ImportImageFile(document, first_source, true);
    if (!imported.is_ok() || imported.value().kind != ImageLocationKind::managed ||
        imported.value().resolved_path.parent_path() != AssetsDirectoryFor(document)) {
        cleanup(); return 3;
    }
    auto reused = ImportImageFile(document, first_source, true);
    if (!reused.is_ok() || reused.value().resolved_path != imported.value().resolved_path) {
        cleanup(); return 4;
    }
    auto collision = ImportImageFile(document, second_source, true);
    if (!collision.is_ok() || collision.value().resolved_path == imported.value().resolved_path ||
        collision.value().resolved_path.filename() != L"图_2.png") {
        cleanup(); return 5;
    }

    std::vector<ImageReference> still_referenced{imported.value()};
    auto retained = MarkManagedImageDeleted(
        document, imported.value(), still_referenced);
    if (!retained.is_ok() || retained.value().completed ||
        !std::filesystem::exists(imported.value().resolved_path)) {
        cleanup(); return 6;
    }
    auto marked = MarkManagedImageDeleted(document, imported.value(), {});
    if (!marked.is_ok() || !marked.value().completed ||
        marked.value().marked_path.filename() != L"del_图.png" ||
        std::filesystem::exists(imported.value().resolved_path) ||
        !std::filesystem::exists(marked.value().marked_path)) {
        cleanup(); return 7;
    }
    if (UndoManagedImageDeleted(marked.value()) != ErrorCode::ok ||
        !std::filesystem::exists(imported.value().resolved_path)) {
        cleanup(); return 8;
    }
    Write(imported.value().resolved_path.parent_path() / L"del_图.png", "occupied");
    auto marked_twice = MarkManagedImageDeleted(document, imported.value(), {});
    if (!marked_twice.is_ok() || !marked_twice.value().completed ||
        marked_twice.value().marked_path.filename() != L"del_2_图.png") {
        cleanup(); return 10;
    }
    Write(imported.value().resolved_path, "new-owner");
    if (UndoManagedImageDeleted(marked_twice.value()) !=
            ErrorCode::image_restore_name_conflict ||
        !std::filesystem::exists(marked_twice.value().marked_path) ||
        !std::filesystem::exists(imported.value().resolved_path)) {
        cleanup(); return 11;
    }
    auto external_delete = MarkManagedImageDeleted(document, external.value(), {});
    if (external_delete.is_ok() || !std::filesystem::exists(first_source)) {
        cleanup(); return 9;
    }
    ImageReference forged{"图.png", first_source, ImageLocationKind::managed};
    if (MarkManagedImageDeleted(document, forged, {}).is_ok()) {
        cleanup(); return 12;
    }
    cleanup(); return 0;
}
