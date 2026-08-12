#include "markdownmay/document/document_session.hpp"

#include "markdownmay/document/document_reconciler.hpp"
#include "markdownmay/fileio/text_encoding.hpp"
#include "markdownmay/markdown/markdown_parser.hpp"

#include <utility>

namespace markdownmay::document {

DocumentSession::DocumentSession(std::string source, DocumentKind kind)
    : source_(std::move(source)), kind_(kind) {
    if (kind_ == DocumentKind::markdown && fileio::IsValidUtf8(source_)) {
        semantic_ = markdown::ParseMarkdown(source_, source_revision_);
        if (semantic_) parsed_revision_ = source_revision_;
    }
}

SessionSnapshot DocumentSession::snapshot() const {
    return {source_, kind_, source_revision_, parsed_revision_, saved_revision_, semantic_};
}

ErrorCode DocumentSession::commit(const EditTransaction& transaction) {
    if (transaction.base_revision != source_revision_) {
        return ErrorCode::document_revision_mismatch;
    }
    std::string candidate = source_;
    for (const auto& change : transaction.changes) {
        if (change.replaced_range.begin > change.replaced_range.end ||
            change.replaced_range.end > candidate.size()) {
            return ErrorCode::document_invalid_state;
        }
        candidate.replace(
            static_cast<std::size_t>(change.replaced_range.begin),
            static_cast<std::size_t>(change.replaced_range.end -
                                     change.replaced_range.begin),
            change.replacement);
    }
    if (!fileio::IsValidUtf8(candidate)) {
        return ErrorCode::document_invalid_state;
    }
    const auto next_revision = source_revision_ + 1;
    auto parsed = kind_ == DocumentKind::markdown
        ? markdown::ParseMarkdown(candidate, next_revision) : nullptr;
    if (parsed && semantic_) {
        parsed = ReconcileNodeIds(*semantic_, *parsed, next_revision);
    }
    source_ = std::move(candidate);
    source_revision_ = next_revision;
    if (kind_ == DocumentKind::markdown && parsed) {
        semantic_ = std::move(parsed);
        parsed_revision_ = source_revision_;
    }
    Notify({transaction.id, source_revision_, parsed_revision_, transaction.origin});
    return ErrorCode::ok;
}

ErrorCode DocumentSession::commit_semantic(
    std::uint64_t expected_revision,
    std::shared_ptr<const Document> semantic,
    std::string serialized_source,
    EditOrigin origin) {
    if (expected_revision != source_revision_) {
        return ErrorCode::document_revision_mismatch;
    }
    if (kind_ != DocumentKind::markdown) {
        return ErrorCode::document_invalid_state;
    }
    const auto next_revision = source_revision_ + 1;
    if (!semantic || semantic->revision() != next_revision ||
        !fileio::IsValidUtf8(serialized_source) ||
        !semantic->validate(serialized_source.size())) {
        return ErrorCode::document_invariant_failed;
    }
    source_ = std::move(serialized_source);
    source_revision_ = next_revision;
    parsed_revision_ = next_revision;
    semantic_ = std::move(semantic);
    Notify({0, source_revision_, parsed_revision_, origin});
    return ErrorCode::ok;
}

ErrorCode DocumentSession::accept_parse_result(
    std::uint64_t source_revision,
    std::shared_ptr<const Document> semantic) {
    if (kind_ != DocumentKind::markdown) {
        return ErrorCode::document_invalid_state;
    }
    if (source_revision != source_revision_) {
        return ErrorCode::document_revision_mismatch;
    }
    if (!semantic || semantic->revision() != source_revision ||
        !semantic->validate(source_.size())) {
        return ErrorCode::document_invariant_failed;
    }
    if (semantic_) {
        semantic = ReconcileNodeIds(*semantic_, *semantic, source_revision);
        if (!semantic) return ErrorCode::document_invariant_failed;
    }
    semantic_ = std::move(semantic);
    parsed_revision_ = source_revision_;
    return ErrorCode::ok;
}

ErrorCode DocumentSession::reload(std::string source) {
    if (!fileio::IsValidUtf8(source)) return ErrorCode::file_encoding_invalid;
    const auto next_revision = source_revision_ + 1;
    auto semantic = kind_ == DocumentKind::markdown
        ? markdown::ParseMarkdown(source, next_revision) : nullptr;
    source_ = std::move(source);
    source_revision_ = next_revision;
    semantic_ = std::move(semantic);
    parsed_revision_ = semantic_ ? source_revision_ : 0;
    saved_revision_ = source_revision_;
    Notify({0, source_revision_, parsed_revision_, EditOrigin::file_reload});
    return ErrorCode::ok;
}

ErrorCode DocumentSession::mark_saved(std::uint64_t revision) noexcept {
    if (revision > source_revision_) return ErrorCode::document_revision_mismatch;
    saved_revision_ = revision;
    return ErrorCode::ok;
}

bool DocumentSession::can_export() const noexcept {
    return kind_ == DocumentKind::markdown && semantic_ &&
           parsed_revision_ == source_revision_ &&
           semantic_->revision() == source_revision_;
}
bool DocumentSession::is_dirty() const noexcept {
    return saved_revision_ != source_revision_;
}
DocumentKind DocumentSession::kind() const noexcept { return kind_; }
bool DocumentSession::has_markdown_semantics() const noexcept {
    return kind_ == DocumentKind::markdown;
}
void DocumentSession::subscribe(DocumentObserver observer) {
    if (observer) observers_.push_back(std::move(observer));
}
void DocumentSession::Notify(const DocumentEvent& event) noexcept {
    for (const auto& observer : observers_) {
        try { observer(event); } catch (...) { }
    }
}

}  // namespace markdownmay::document
