#include "markdownmay/editor/paragraph_editor.hpp"

#include <string>

int main() {
    using namespace markdownmay;
    document::DocumentSession session("第一段\n\nsecond");
    editor::ParagraphEditor editor(session);

    if (editor.set_selection({0, 0}) != ErrorCode::ok ||
        editor.insert_text("新") != ErrorCode::ok ||
        session.snapshot().source != "新第一段\n\nsecond") return 1;
    if (!editor.can_undo() || editor.can_redo()) return 2;
    if (editor.undo() != ErrorCode::ok ||
        session.snapshot().source != "第一段\n\nsecond" || !editor.can_redo()) return 3;
    if (editor.redo() != ErrorCode::ok ||
        session.snapshot().source != "新第一段\n\nsecond") return 4;

    if (editor.set_selection({3, 6}) != ErrorCode::ok ||
        editor.insert_text("好") != ErrorCode::ok ||
        session.snapshot().source != "新好一段\n\nsecond") return 5;
    if (editor.delete_backward() != ErrorCode::ok ||
        session.snapshot().source != "新一段\n\nsecond") return 6;
    if (editor.undo() != ErrorCode::ok ||
        session.snapshot().source != "新好一段\n\nsecond") return 7;

    const auto before_raw = session.snapshot();
    const std::string raw = "<div>x</div>\n\n";
    if (editor.set_selection({0, 0}) != ErrorCode::ok ||
        editor.insert_text(raw) != ErrorCode::ok ||
        session.snapshot().source != raw + before_raw.source ||
        session.snapshot().source.find(raw) != 0) return 8;
    if (editor.set_selection({raw.size() + 1, raw.size() + 1}) !=
        ErrorCode::editor_selection_mapping_failed) return 9;

    document::EditTransaction external{99, session.snapshot().source_revision,
        document::EditOrigin::source_view, {{{0, 0}, "外"}}};
    if (session.commit(external) != ErrorCode::ok ||
        editor.undo() != ErrorCode::editor_undo_failed) return 10;
    return 0;
}
