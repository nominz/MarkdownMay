#pragma once

#include "markdownmay/editor/paragraph_editor.hpp"
#include "markdownmay/fileio/image_store.hpp"

#include <filesystem>
#include <string_view>

namespace markdownmay::editor {

class ImageController final {
public:
    ImageController(document::DocumentSession& session, ParagraphEditor& editor);
    [[nodiscard]] ErrorCode insert_reference(std::string_view target,
        std::string_view alternative, std::string_view title = {});
    [[nodiscard]] ErrorCode insert_file(const std::filesystem::path& document_path,
        const std::filesystem::path& image_path, bool copy_to_assets,
        std::string_view alternative);
    [[nodiscard]] ErrorCode replace(document::NodeId image, std::string_view target,
        std::string_view alternative, std::string_view title = {});
    [[nodiscard]] ErrorCode set_display_percent(document::NodeId image, std::uint16_t percent);
    [[nodiscard]] ErrorCode remove(document::NodeId image);

private:
    [[nodiscard]] ErrorCode ReplaceNode(document::NodeId image, std::string replacement);
    document::DocumentSession& session_;
    ParagraphEditor& editor_;
};

}  // namespace markdownmay::editor
