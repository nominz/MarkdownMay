#pragma once

#include "markdownmay/document/document_session.hpp"

#include <functional>
#include <unordered_set>
#include <vector>

namespace markdownmay::editor {

struct HeadingFoldItem final {
    document::NodeId node_id{};
    std::uint8_t level{1};
    document::SourceRange heading_range;
    document::SourceRange body_range;
    bool collapsed{};
};

class HeadingFoldController final {
public:
    explicit HeadingFoldController(document::DocumentSession& session);
    ~HeadingFoldController();
    HeadingFoldController(const HeadingFoldController&) = delete;
    HeadingFoldController& operator=(const HeadingFoldController&) = delete;
    void refresh();
    [[nodiscard]] bool toggle(document::NodeId node_id);
    [[nodiscard]] bool toggle_at(std::uint64_t heading_source_offset);
    [[nodiscard]] bool expand_to_reveal(std::uint64_t source_offset);
    void reset();
    void set_changed_callback(std::function<void()> callback);
    [[nodiscard]] const std::vector<HeadingFoldItem>& items() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;

private:
    document::DocumentSession& session_;
    std::vector<HeadingFoldItem> items_;
    std::unordered_set<document::NodeId> collapsed_;
    std::function<void()> changed_callback_;
    std::shared_ptr<int> lifetime_{std::make_shared<int>(0)};
    std::uint64_t revision_{};
};

}  // namespace markdownmay::editor
