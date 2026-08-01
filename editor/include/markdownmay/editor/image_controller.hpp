#pragma once

#include "markdownmay/editor/paragraph_editor.hpp"
#include "markdownmay/fileio/image_store.hpp"

#include <filesystem>
#include <string_view>
#include <vector>

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
    [[nodiscard]] ErrorCode remove_managed(
        const std::filesystem::path& document_path, document::NodeId image);
    [[nodiscard]] ErrorCode undo();
    [[nodiscard]] ErrorCode redo();

private:
    struct ManagedDelete final {
        std::size_t history_depth{};
        std::filesystem::path document_path;
        fileio::ImageReference reference;
        fileio::ManagedImageRename rename;
    };
    [[nodiscard]] ErrorCode ReplaceNode(document::NodeId image, std::string replacement);
    document::DocumentSession& session_;
    ParagraphEditor& editor_;
    std::vector<ManagedDelete> managed_undo_;
    std::vector<ManagedDelete> managed_redo_;
};

}  // namespace markdownmay::editor
