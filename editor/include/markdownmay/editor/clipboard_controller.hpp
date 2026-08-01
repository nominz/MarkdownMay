#pragma once

#include "markdownmay/editor/image_controller.hpp"

#include <windows.h>

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace markdownmay::editor {

struct DropResult final {
    std::vector<std::filesystem::path> documents_to_open;
    std::size_t inserted_images{};
};

class ClipboardController final {
public:
    ClipboardController(document::DocumentSession& session, ParagraphEditor& editor,
                        ImageController& images);
    [[nodiscard]] ErrorCode paste_plain(std::string_view text);
    [[nodiscard]] ErrorCode paste_html(std::string_view html);
    [[nodiscard]] Result<DropResult> drop_files(const std::filesystem::path& document_path,
        std::span<const std::filesystem::path> files, bool copy_images_to_assets);
    [[nodiscard]] ErrorCode paste_bitmap(const std::filesystem::path& document_path,
                                         HBITMAP bitmap);
    [[nodiscard]] static std::string HtmlToMarkdown(std::string_view html);
private:
    document::DocumentSession& session_;
    ParagraphEditor& editor_;
    ImageController& images_;
};

}  // namespace markdownmay::editor
