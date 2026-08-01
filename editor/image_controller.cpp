#include "markdownmay/editor/image_controller.hpp"

#include <algorithm>
#include <windows.h>

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
    const auto near_position = (std::min)(static_cast<std::size_t>(node.source.begin), source.size());
    const auto open = source.rfind("![", near_position);
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

void CollectReferences(const document::Node& node,
                       const std::filesystem::path& document_path,
                       document::NodeId excluded,
                       std::vector<fileio::ImageReference>& output) {
    if (node.kind == document::NodeKind::image && node.id != excluded) {
        if (const auto* value = std::get_if<document::LinkAttributes>(&node.attributes))
            output.push_back(fileio::ResolveImageReference(document_path, value->target));
    }
    for (const auto& child : node.children)
        CollectReferences(*child, document_path, excluded, output);
}

}  // namespace

ImageController::ImageController(document::DocumentSession& session, ParagraphEditor& editor)
    : session_(session), editor_(editor) {}

ErrorCode ImageController::insert_reference(std::string_view target,
                                             std::string_view alternative,
                                             std::string_view title) {
    if (target.empty()) return ErrorCode::image_import_failed;
    const auto result = editor_.insert_text(Syntax(target, alternative, title));
    if (result == ErrorCode::ok) managed_redo_.clear();
    return result;
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

ErrorCode ImageController::remove_managed(const std::filesystem::path& document_path,
                                          document::NodeId image) {
    const auto snapshot = session_.snapshot();
    const auto* node = snapshot.semantic ? snapshot.semantic->find(image) : nullptr;
    const auto* attributes = node ? std::get_if<document::LinkAttributes>(&node->attributes) : nullptr;
    if (!node || node->kind != document::NodeKind::image || !attributes)
        return ErrorCode::editor_selection_mapping_failed;
    const auto reference = fileio::ResolveImageReference(document_path, attributes->target);
    std::vector<fileio::ImageReference> remaining;
    CollectReferences(*snapshot.semantic->root(), document_path, image, remaining);
    const auto removed = ReplaceNode(image, {});
    if (removed != ErrorCode::ok) return removed;
    if (reference.kind != fileio::ImageLocationKind::managed) return ErrorCode::ok;
    auto renamed = fileio::MarkManagedImageDeleted(document_path, reference, remaining);
    if (!renamed.is_ok()) return renamed.error();
    if (renamed.value().completed) {
        managed_undo_.push_back({editor_.undo_depth(), document_path,
            reference, renamed.value()});
        managed_redo_.clear();
    }
    return ErrorCode::ok;
}

ErrorCode ImageController::undo() {
    if (managed_undo_.empty() ||
        managed_undo_.back().history_depth != editor_.undo_depth())
        return editor_.undo();
    auto operation = managed_undo_.back();
    auto restored = fileio::RestoreManagedImageDeletedSafely(operation.rename);
    if (!restored.is_ok()) return restored.error();
    const auto conflict = restored.value() != operation.rename.original_path;
    auto result = editor_.undo();
    if (result != ErrorCode::ok) {
        MoveFileExW(restored.value().c_str(), operation.rename.marked_path.c_str(),
                    MOVEFILE_WRITE_THROUGH);
        return result;
    }
    if (conflict) {
        const auto snapshot = session_.snapshot();
        const document::Node* restored_node{};
        std::vector<const document::Node*> stack{snapshot.semantic->root().get()};
        while (!stack.empty()) {
            const auto* candidate = stack.back(); stack.pop_back();
            if (candidate->kind == document::NodeKind::image) {
                const auto* value = std::get_if<document::LinkAttributes>(&candidate->attributes);
                if (value && value->target == operation.reference.markdown_target) {
                    restored_node = candidate; break;
                }
            }
            for (const auto& child : candidate->children) stack.push_back(child.get());
        }
        if (!restored_node) return ErrorCode::editor_undo_failed;
        std::string alternative;
        for (const auto& child : restored_node->children) alternative += child->text;
        const auto target = fileio::ImageMarkdownTarget(operation.document_path, restored.value());
        result = replace(restored_node->id, target, alternative);
        managed_undo_.pop_back();
        return result == ErrorCode::ok ? ErrorCode::image_restore_name_conflict : result;
    }
    managed_undo_.pop_back();
    operation.history_depth = editor_.redo_depth();
    managed_redo_.push_back(std::move(operation));
    return ErrorCode::ok;
}

ErrorCode ImageController::redo() {
    if (managed_redo_.empty() ||
        managed_redo_.back().history_depth != editor_.redo_depth())
        return editor_.redo();
    auto operation = managed_redo_.back();
    auto result = editor_.redo();
    if (result != ErrorCode::ok) return result;
    auto renamed = fileio::MarkManagedImageDeleted(
        operation.document_path, operation.reference, {});
    if (!renamed.is_ok()) return renamed.error();
    operation.rename = renamed.value();
    operation.history_depth = editor_.undo_depth();
    managed_redo_.pop_back();
    managed_undo_.push_back(std::move(operation));
    return ErrorCode::ok;
}

ErrorCode ImageController::ReplaceNode(document::NodeId image, std::string replacement) {
    const auto snapshot = session_.snapshot();
    const auto* node = snapshot.semantic ? snapshot.semantic->find(image) : nullptr;
    std::uint64_t begin{}, end{};
    if (!node || !FullRange(*node, snapshot.source, begin, end))
        return ErrorCode::editor_selection_mapping_failed;
    const auto result = editor_.replace_source_range(
        begin, end, std::move(replacement), {begin, begin});
    if (result == ErrorCode::ok) managed_redo_.clear();
    return result;
}

}  // namespace markdownmay::editor
