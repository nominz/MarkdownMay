#include "markdownmay/editor/image_controller.hpp"

#include <algorithm>

namespace markdownmay::editor {
namespace {

std::string Escape(std::string_view value) {
    std::string result;
    for (const auto character : value) {
        if (character == '\\' || character == '[' || character == ']' ||
            character == '(' || character == ')' || character == '"') result.push_back('\\');
        result.push_back(character);
    }
    return result;
}

std::string Syntax(std::string_view target, std::string_view alternative,
                   std::string_view title) {
    auto result = "![" + Escape(alternative) + "](" + Escape(target);
    if (!title.empty()) result += " \"" + Escape(title) + "\"";
    return result + ")";
}

bool FullRange(const document::Node& node, std::string_view source,
               std::uint64_t& begin, std::uint64_t& end) {
    if (node.kind != document::NodeKind::image) return false;
    const auto near = (std::min)(static_cast<std::size_t>(node.source.begin), source.size());
    const auto open = source.rfind("![", near);
    if (open == std::string_view::npos) return false;
    bool escaped = false;
    int parentheses{};
    for (auto cursor = open + 2; cursor < source.size(); ++cursor) {
        const auto value = source[cursor];
        if (escaped) { escaped = false; continue; }
        if (value == '\\') { escaped = true; continue; }
        if (value == '(') ++parentheses;
        else if (value == ')' && parentheses > 0 && --parentheses == 0) {
            begin = open; end = cursor + 1; return true;
        }
    }
    return false;
}

}  // namespace

ImageController::ImageController(document::DocumentSession& session, ParagraphEditor& editor)
    : session_(session), editor_(editor) {}

ErrorCode ImageController::insert_reference(std::string_view target,
                                             std::string_view alternative,
                                             std::string_view title) {
    if (target.empty()) return ErrorCode::image_import_failed;
    return editor_.insert_text(Syntax(target, alternative, title));
}

ErrorCode ImageController::insert_file(const std::filesystem::path& document_path,
                                       const std::filesystem::path& image_path,
                                       bool copy_to_assets,
                                       std::string_view alternative) {
    auto imported = fileio::ImportImageFile(document_path, image_path, copy_to_assets);
    if (!imported.is_ok()) return imported.error();
    return insert_reference(imported.value().markdown_target, alternative);
}

ErrorCode ImageController::replace(document::NodeId image, std::string_view target,
                                   std::string_view alternative, std::string_view title) {
    if (target.empty()) return ErrorCode::image_import_failed;
    return ReplaceNode(image, Syntax(target, alternative, title));
}

ErrorCode ImageController::set_display_percent(document::NodeId image, std::uint16_t percent) {
    const auto snapshot = session_.snapshot();
    const auto* node = snapshot.semantic ? snapshot.semantic->find(image) : nullptr;
    const auto* attributes = node ? std::get_if<document::LinkAttributes>(&node->attributes) : nullptr;
    if (!node || node->kind != document::NodeKind::image || !attributes)
        return ErrorCode::editor_selection_mapping_failed;
    percent = (std::clamp)(percent, std::uint16_t{10}, std::uint16_t{300});
    std::string alternative;
    for (const auto& child : node->children) alternative += child->text;
    return ReplaceNode(image, Syntax(attributes->target, alternative,
        "markdownmay-width=" + std::to_string(percent) + "%"));
}

ErrorCode ImageController::remove(document::NodeId image) { return ReplaceNode(image, {}); }

ErrorCode ImageController::ReplaceNode(document::NodeId image, std::string replacement) {
    const auto snapshot = session_.snapshot();
    const auto* node = snapshot.semantic ? snapshot.semantic->find(image) : nullptr;
    std::uint64_t begin{}, end{};
    if (!node || !FullRange(*node, snapshot.source, begin, end))
        return ErrorCode::editor_selection_mapping_failed;
    return editor_.replace_source_range(begin, end, std::move(replacement), {begin, begin});
}

}  // namespace markdownmay::editor
