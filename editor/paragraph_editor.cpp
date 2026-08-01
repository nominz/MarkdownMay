#include "markdownmay/editor/paragraph_editor.hpp"

#include "markdownmay/fileio/text_encoding.hpp"
#include "markdownmay/markdown/markdown_parser.hpp"

#include <algorithm>
#include <utility>

namespace markdownmay::editor {
namespace {

bool IsUtf8Boundary(std::string_view text, std::uint64_t offset) {
    if (offset > text.size()) return false;
    if (offset == text.size()) return true;
    return (static_cast<unsigned char>(text[static_cast<std::size_t>(offset)]) & 0xC0U) != 0x80U;
}

bool IsSupportedInline(const document::Node& node) {
    if (node.kind == document::NodeKind::text) return node.children.empty();
    if (node.kind == document::NodeKind::inline_code) return node.children.empty();
    if (node.kind != document::NodeKind::emphasis &&
        node.kind != document::NodeKind::strong &&
        node.kind != document::NodeKind::strike &&
        node.kind != document::NodeKind::link) return false;
    return std::all_of(node.children.begin(), node.children.end(),
        [](const auto& child) { return IsSupportedInline(*child); });
}

bool IsSupportedBlock(const document::Node& block) {
    if (IsSupportedInline(block)) return true;
    if (block.kind == document::NodeKind::thematic_break) return block.children.empty();
    if (block.kind == document::NodeKind::code_block) return block.children.empty();
    if (block.kind == document::NodeKind::quote) {
        return std::all_of(block.children.begin(), block.children.end(),
            [](const auto& child) { return IsSupportedBlock(*child); });
    }
    if (block.kind == document::NodeKind::list ||
        block.kind == document::NodeKind::list_item) {
        return std::all_of(block.children.begin(), block.children.end(),
            [](const auto& child) { return IsSupportedBlock(*child); });
    }
    if (block.kind != document::NodeKind::paragraph &&
        block.kind != document::NodeKind::heading) return false;
    for (const auto& child : block.children) {
        if (!IsSupportedInline(*child)) return false;
    }
    return true;
}

bool IsSupportedEditorDocument(const document::Document& document) {
    if (document.root()->kind != document::NodeKind::document) return false;
    for (const auto& block : document.root()->children) {
        if (!IsSupportedBlock(*block)) return false;
    }
    return true;
}

std::uint64_t PreviousCodePoint(std::string_view text, std::uint64_t offset) {
    if (offset == 0) return 0;
    auto value = offset - 1;
    while (value > 0 && !IsUtf8Boundary(text, value)) --value;
    return value;
}

std::uint64_t NextCodePoint(std::string_view text, std::uint64_t offset) {
    if (offset >= text.size()) return static_cast<std::uint64_t>(text.size());
    auto value = offset + 1;
    while (value < text.size() && !IsUtf8Boundary(text, value)) ++value;
    return value;
}

}  // namespace

ParagraphEditor::ParagraphEditor(document::DocumentSession& session) : session_(session) {
    const auto size = static_cast<std::uint64_t>(session_.snapshot().source.size());
    selection_ = {size, size};
}

TextSelection ParagraphEditor::selection() const noexcept { return selection_; }

ErrorCode ParagraphEditor::set_selection(TextSelection selection) noexcept {
    if (!IsValidSelection(selection)) return ErrorCode::editor_selection_mapping_failed;
    selection_ = selection;
    return ErrorCode::ok;
}

ErrorCode ParagraphEditor::insert_text(std::string_view utf8_text) {
    if (!fileio::IsValidUtf8(utf8_text)) return ErrorCode::document_invalid_state;
    return ReplaceSelection(std::string(utf8_text));
}

ErrorCode ParagraphEditor::replace_source_range(
    std::uint64_t begin, std::uint64_t end, std::string replacement,
    TextSelection next_selection) {
    const auto original = selection_;
    if (set_selection({begin, end}) != ErrorCode::ok) return ErrorCode::editor_selection_mapping_failed;
    const auto source = session_.snapshot().source;
    HistoryEntry history{begin,
        source.substr(static_cast<std::size_t>(begin), static_cast<std::size_t>(end - begin)),
        replacement, original, next_selection};
    const auto result = Apply(begin, end, std::move(replacement),
                              document::EditOrigin::render_view, next_selection);
    if (result != ErrorCode::ok) {
        selection_ = original;
        return result;
    }
    undo_.push_back(std::move(history));
    redo_.clear();
    return ErrorCode::ok;
}

ErrorCode ParagraphEditor::delete_backward() {
    if (selection_.anchor != selection_.caret) return ReplaceSelection({});
    const auto source = session_.snapshot().source;
    if (selection_.caret == 0) return ErrorCode::ok;
    const auto original_selection = selection_;
    auto begin = PreviousCodePoint(source, selection_.caret);
    if (selection_.caret >= 2 && source[static_cast<std::size_t>(selection_.caret - 2)] == '\r' &&
        source[static_cast<std::size_t>(selection_.caret - 1)] == '\n') begin = selection_.caret - 2;
    selection_.anchor = begin;
    const auto result = ReplaceSelection({});
    if (result != ErrorCode::ok) selection_ = original_selection;
    return result;
}

ErrorCode ParagraphEditor::delete_forward() {
    if (selection_.anchor != selection_.caret) return ReplaceSelection({});
    const auto source = session_.snapshot().source;
    if (selection_.caret >= source.size()) return ErrorCode::ok;
    const auto original_selection = selection_;
    auto end = NextCodePoint(source, selection_.caret);
    if (selection_.caret + 1 < source.size() &&
        source[static_cast<std::size_t>(selection_.caret)] == '\r' &&
        source[static_cast<std::size_t>(selection_.caret + 1)] == '\n') end = selection_.caret + 2;
    selection_.anchor = end;
    const auto result = ReplaceSelection({});
    if (result != ErrorCode::ok) selection_ = original_selection;
    return result;
}

ErrorCode ParagraphEditor::ReplaceSelection(std::string replacement) {
    if (!IsValidSelection(selection_)) return ErrorCode::editor_selection_mapping_failed;
    const auto begin = (std::min)(selection_.anchor, selection_.caret);
    const auto end = (std::max)(selection_.anchor, selection_.caret);
    const auto before = selection_;
    const auto source = session_.snapshot().source;
    HistoryEntry history{begin,
        source.substr(static_cast<std::size_t>(begin), static_cast<std::size_t>(end - begin)),
        replacement, before, {begin + replacement.size(), begin + replacement.size()}};
    const auto result = Apply(begin, end, std::move(replacement),
                              document::EditOrigin::render_view, history.after);
    if (result != ErrorCode::ok) return result;
    undo_.push_back(std::move(history));
    redo_.clear();
    return ErrorCode::ok;
}

ErrorCode ParagraphEditor::Apply(
    std::uint64_t begin, std::uint64_t end, std::string replacement,
    document::EditOrigin origin, TextSelection next_selection) {
    auto snapshot = session_.snapshot();
    if (begin > end || end > snapshot.source.size()) return ErrorCode::editor_selection_mapping_failed;
    auto candidate = snapshot.source;
    candidate.replace(static_cast<std::size_t>(begin), static_cast<std::size_t>(end - begin), replacement);
    auto parsed = markdown::ParseMarkdown(candidate, snapshot.source_revision + 1);
    if (!parsed) return ErrorCode::document_invalid_state;
    if (!IsSupportedEditorDocument(*parsed)) return ErrorCode::editor_unmapped_rich_edit_change;
    document::EditTransaction transaction{next_transaction_++, snapshot.source_revision, origin,
        {{{begin, end}, std::move(replacement)}}};
    const auto result = session_.commit(transaction);
    if (result == ErrorCode::document_revision_mismatch) return ErrorCode::editor_transaction_conflict;
    if (result != ErrorCode::ok) return result;
    selection_ = next_selection;
    return ErrorCode::ok;
}

ErrorCode ParagraphEditor::undo() {
    if (undo_.empty()) return ErrorCode::ok;
    const auto entry = undo_.back();
    const auto source = session_.snapshot().source;
    if (entry.begin + entry.inserted.size() > source.size() ||
        source.compare(static_cast<std::size_t>(entry.begin), entry.inserted.size(),
                       entry.inserted) != 0) return ErrorCode::editor_undo_failed;
    const auto result = Apply(entry.begin, entry.begin + entry.inserted.size(), entry.removed,
                              document::EditOrigin::undo, entry.before);
    if (result != ErrorCode::ok) return ErrorCode::editor_undo_failed;
    undo_.pop_back();
    redo_.push_back(entry);
    return ErrorCode::ok;
}

ErrorCode ParagraphEditor::redo() {
    if (redo_.empty()) return ErrorCode::ok;
    const auto entry = redo_.back();
    const auto source = session_.snapshot().source;
    if (entry.begin + entry.removed.size() > source.size() ||
        source.compare(static_cast<std::size_t>(entry.begin), entry.removed.size(),
                       entry.removed) != 0) return ErrorCode::editor_undo_failed;
    const auto result = Apply(entry.begin, entry.begin + entry.removed.size(), entry.inserted,
                              document::EditOrigin::redo, entry.after);
    if (result != ErrorCode::ok) return ErrorCode::editor_undo_failed;
    redo_.pop_back();
    undo_.push_back(entry);
    return ErrorCode::ok;
}

bool ParagraphEditor::can_undo() const noexcept { return !undo_.empty(); }
bool ParagraphEditor::can_redo() const noexcept { return !redo_.empty(); }

bool ParagraphEditor::IsValidSelection(TextSelection value) const noexcept {
    const auto source = session_.snapshot().source;
    return IsUtf8Boundary(source, value.anchor) && IsUtf8Boundary(source, value.caret);
}

}  // namespace markdownmay::editor
