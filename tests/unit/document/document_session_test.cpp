#include "markdownmay/document/document_session.hpp"

#include "markdownmay/markdown/markdown_parser.hpp"

#include <stdexcept>
#include <vector>

namespace {
markdownmay::document::NodeId FirstParagraphId(
    const std::shared_ptr<const markdownmay::document::Node>& node) {
    if (node->kind == markdownmay::document::NodeKind::paragraph) return node->id;
    for (const auto& child : node->children) {
        const auto found = FirstParagraphId(child);
        if (found != 0) return found;
    }
    return 0;
}
}  // namespace

int RunDocumentSessionTests() {
    using namespace markdownmay;
    using namespace markdownmay::document;

    DocumentSession session("# 标题\n\n正文\n");
    auto initial = session.snapshot();
    if (initial.source_revision != 1 || initial.parsed_revision != 1 ||
        initial.saved_revision != 1 || session.is_dirty() ||
        !session.can_export()) return 20;
    const auto unchanged_paragraph_id = FirstParagraphId(initial.semantic->root());
    if (unchanged_paragraph_id == 0) return 30;
    auto background_parse = markdownmay::markdown::ParseMarkdown(initial.source, 1);
    if (session.accept_parse_result(1, background_parse) != ErrorCode::ok ||
        FirstParagraphId(session.snapshot().semantic->root()) != unchanged_paragraph_id) return 32;

    std::vector<DocumentEvent> events;
    session.subscribe([](const DocumentEvent&) { throw std::runtime_error("observer"); });
    session.subscribe([&](const DocumentEvent& event) { events.push_back(event); });

    EditTransaction edit{42, 1, EditOrigin::source_view,
        {{{0, 8}, "## 新标题"},
         {{static_cast<std::uint64_t>(std::string("## 新标题\n\n正文\n").size()),
           static_cast<std::uint64_t>(std::string("## 新标题\n\n正文\n").size())},
          "\n追加\n"}}};
    if (session.commit(edit) != ErrorCode::ok) return 21;
    auto changed = session.snapshot();
    if (changed.source_revision != 2 || changed.parsed_revision != 2 ||
        changed.saved_revision != 1 || !session.is_dirty() ||
        !session.can_export() || events.size() != 1 ||
        events[0].transaction != 42 || events[0].source_revision != 2) return 22;
    if (FirstParagraphId(changed.semantic->root()) != unchanged_paragraph_id ||
        changed.semantic->find(unchanged_paragraph_id) == nullptr) return 31;

    EditTransaction stale{43, 1, EditOrigin::undo, {{{0, 0}, "错误"}}};
    const auto before_stale = session.snapshot().source;
    if (session.commit(stale) != ErrorCode::document_revision_mismatch ||
        session.snapshot().source != before_stale || events.size() != 1) return 23;

    EditTransaction invalid{44, 2, EditOrigin::source_view,
        {{{9999, 10000}, "越界"}}};
    if (session.commit(invalid) != ErrorCode::document_invalid_state ||
        session.snapshot().source_revision != 2) return 24;

    auto stale_parse = markdownmay::markdown::ParseMarkdown("# 旧", 1);
    if (session.accept_parse_result(1, stale_parse) !=
        ErrorCode::document_revision_mismatch) return 25;

    const std::string rendered_source = "# 渲染编辑\n\n内容\n";
    auto rendered = markdownmay::markdown::ParseMarkdown(rendered_source, 3);
    if (session.commit_semantic(2, rendered, rendered_source,
                                EditOrigin::render_view) != ErrorCode::ok) return 26;
    if (session.snapshot().source_revision != 3 ||
        session.snapshot().parsed_revision != 3 || !session.can_export()) return 27;

    if (session.mark_saved(4) != ErrorCode::document_revision_mismatch ||
        !session.is_dirty()) return 28;
    if (session.mark_saved(3) != ErrorCode::ok || session.is_dirty()) return 29;
    const auto before_reload = session.snapshot();
    if (session.reload("# 新文件\n") != ErrorCode::ok || session.is_dirty() ||
        session.snapshot().source != "# 新文件\n" ||
        session.snapshot().source_revision != before_reload.source_revision + 1 ||
        events.back().origin != EditOrigin::file_reload) return 33;
    const auto valid_reload = session.snapshot();
    if (session.reload(std::string("bad\xff", 4)) !=
            ErrorCode::file_encoding_invalid ||
        session.snapshot().source != valid_reload.source ||
        session.snapshot().source_revision != valid_reload.source_revision) return 34;
    return 0;
}
