#include "markdownmay/document/document.hpp"

#include <unordered_set>

namespace markdownmay::document {

Document::Document(std::shared_ptr<const Node> root, std::uint64_t revision)
    : root_(std::move(root)), revision_(revision) { Index(root_); }

const std::shared_ptr<const Node>& Document::root() const noexcept { return root_; }
std::uint64_t Document::revision() const noexcept { return revision_; }
const Node* Document::find(NodeId id) const noexcept {
    const auto found = index_.find(id);
    return found == index_.end() ? nullptr : found->second;
}
void Document::Index(const std::shared_ptr<const Node>& node) {
    if (!node) return;
    if (!index_.emplace(node->id, node.get()).second) duplicate_id_ = true;
    for (const auto& child : node->children) Index(child);
}
bool Document::validate(std::uint64_t source_size) const noexcept {
    if (!root_ || root_->kind != NodeKind::document || root_->id == 0 || duplicate_id_) return false;
    std::vector<const Node*> pending{root_.get()};
    while (!pending.empty()) {
        const auto* node = pending.back(); pending.pop_back();
        if (node->id == 0 || node->source.begin > node->source.end ||
            node->source.end > source_size) return false;
        for (const auto& child : node->children) {
            if (!child) return false;
            pending.push_back(child.get());
        }
    }
    return true;
}

}  // namespace markdownmay::document
