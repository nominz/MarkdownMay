#pragma once

#include "markdownmay/document_types.hpp"
#include "markdownmay/error.hpp"

#include <functional>
#include <optional>

namespace markdownmay {

struct FileMetadata final {
    Path path;
    TextEncoding encoding{TextEncoding::utf8};
    LineEnding line_ending{LineEnding::crlf};
    bool read_only{};
};

struct SessionSnapshot final {
    DocumentId id{};
    Revision source_revision{};
    Revision parsed_revision{};
    Revision saved_revision{};
    Utf8Text source;
    std::shared_ptr<const SemanticDocument> semantic;
    FileMetadata file;
    ViewMode mode{ViewMode::render};
};

enum class EditOrigin : std::uint8_t {
    render_view,
    source_view,
    file_reload,
    undo,
    redo,
};

struct SourceChange final {
    SourceRange replaced_range;
    Utf8Text replacement;
};

struct DocumentEvent final {
    DocumentId document{};
    Revision source_revision{};
    EditOrigin origin{EditOrigin::source_view};
};

using DocumentObserver = std::function<void(const DocumentEvent&)>;

class IDocumentSession {
public:
    virtual ~IDocumentSession() = default;

    [[nodiscard]] virtual SessionSnapshot snapshot() const = 0;
    [[nodiscard]] virtual Status apply_source_change(
        const SourceChange& change,
        EditOrigin origin) = 0;
    [[nodiscard]] virtual Status commit_semantic_edit(
        std::shared_ptr<const SemanticDocument> document,
        Utf8Text serialized_source,
        SelectionSnapshot selection) = 0;
    [[nodiscard]] virtual Status accept_parse_result(
        const ParseSnapshot& result) = 0;
    [[nodiscard]] virtual Status mark_saved(Revision revision) = 0;
    [[nodiscard]] virtual bool can_export() const noexcept = 0;
    [[nodiscard]] virtual bool is_dirty() const noexcept = 0;
    virtual void set_mode(ViewMode mode) = 0;
    virtual void subscribe(DocumentObserver observer) = 0;
};

}  // namespace markdownmay
