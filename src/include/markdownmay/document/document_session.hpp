#pragma once

#include "markdownmay/core/result.hpp"
#include "markdownmay/document/document.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace markdownmay::document {

enum class EditOrigin : std::uint8_t {
    render_view, source_view, file_reload, undo, redo
};

struct SourceChange final {
    SourceRange replaced_range;
    std::string replacement;
};

struct EditTransaction final {
    std::uint64_t id{};
    std::uint64_t base_revision{};
    EditOrigin origin{EditOrigin::source_view};
    std::vector<SourceChange> changes;
};

struct DocumentEvent final {
    std::uint64_t transaction{};
    std::uint64_t source_revision{};
    std::uint64_t parsed_revision{};
    EditOrigin origin{EditOrigin::source_view};
};

struct SessionSnapshot final {
    std::string source;
    std::uint64_t source_revision{};
    std::uint64_t parsed_revision{};
    std::uint64_t saved_revision{};
    std::shared_ptr<const Document> semantic;
};

using DocumentObserver = std::function<void(const DocumentEvent&)>;

class DocumentSession final {
public:
    explicit DocumentSession(std::string source);
    [[nodiscard]] SessionSnapshot snapshot() const;
    [[nodiscard]] ErrorCode commit(const EditTransaction& transaction);
    [[nodiscard]] ErrorCode commit_semantic(
        std::uint64_t expected_revision,
        std::shared_ptr<const Document> semantic,
        std::string serialized_source,
        EditOrigin origin);
    [[nodiscard]] ErrorCode accept_parse_result(
        std::uint64_t source_revision,
        std::shared_ptr<const Document> semantic);
    [[nodiscard]] ErrorCode reload(std::string source);
    [[nodiscard]] ErrorCode mark_saved(std::uint64_t revision) noexcept;
    [[nodiscard]] bool can_export() const noexcept;
    [[nodiscard]] bool is_dirty() const noexcept;
    void subscribe(DocumentObserver observer);

private:
    void Notify(const DocumentEvent& event) noexcept;
    std::string source_;
    std::uint64_t source_revision_{1};
    std::uint64_t parsed_revision_{};
    std::uint64_t saved_revision_{1};
    std::shared_ptr<const Document> semantic_;
    std::vector<DocumentObserver> observers_;
};

}  // namespace markdownmay::document
