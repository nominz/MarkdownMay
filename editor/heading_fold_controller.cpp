#include "markdownmay/editor/heading_fold_controller.hpp"

#include <algorithm>

namespace markdownmay::editor {
namespace {
void CollectHeadings(const document::Node& node, std::vector<HeadingFoldItem>& output) {
    if (node.kind == document::NodeKind::heading) {
        const auto* attributes = std::get_if<document::HeadingAttributes>(&node.attributes);
        output.push_back({node.id,
            attributes ? (std::clamp)(attributes->level, std::uint8_t{1}, std::uint8_t{6})
                       : std::uint8_t{1},
            node.source, {node.source.end, node.source.end}, false});
    }
    for (const auto& child : node.children) CollectHeadings(*child, output);
}
}

HeadingFoldController::HeadingFoldController(document::DocumentSession& session)
    : session_(session) {
    const std::weak_ptr<int> lifetime(lifetime_);
    session_.subscribe([this, lifetime](const document::DocumentEvent&) {
        if (!lifetime.expired()) refresh();
    });
    refresh();
}
HeadingFoldController::~HeadingFoldController() { lifetime_.reset(); }

void HeadingFoldController::refresh() {
    const auto snapshot = session_.snapshot();
    std::vector<HeadingFoldItem> next;
    if (snapshot.kind == document::DocumentKind::markdown && snapshot.semantic &&
        snapshot.parsed_revision == snapshot.source_revision) {
        CollectHeadings(*snapshot.semantic->root(), next);
        for (auto& item : next) {
            const auto raw_begin = (std::min)(item.heading_range.begin,
                static_cast<std::uint64_t>(snapshot.source.size()));
            const auto previous = raw_begin == 0 ? std::string::npos :
                snapshot.source.rfind('\n', static_cast<std::size_t>(raw_begin - 1));
            const auto line_begin = previous == std::string::npos ? 0 : previous + 1;
            auto line_end = snapshot.source.find('\n',
                static_cast<std::size_t>((std::min)(item.heading_range.end,
                    static_cast<std::uint64_t>(snapshot.source.size()))));
            if (line_end == std::string::npos) line_end = snapshot.source.size();
            item.heading_range = {static_cast<std::uint64_t>(line_begin),
                static_cast<std::uint64_t>(line_end)};
        }
        std::sort(next.begin(), next.end(), [](const auto& left, const auto& right) {
            return left.heading_range.begin < right.heading_range.begin;
        });
        for (std::size_t index = 0; index < next.size(); ++index) {
            auto end = static_cast<std::uint64_t>(snapshot.source.size());
            for (auto following = index + 1; following < next.size(); ++following) {
                if (next[following].level <= next[index].level) {
                    end = next[following].heading_range.begin;
                    break;
                }
            }
            auto body_begin = next[index].heading_range.end;
            if (body_begin < snapshot.source.size() &&
                snapshot.source[static_cast<std::size_t>(body_begin)] == '\n')
                ++body_begin;
            next[index].body_range = {body_begin, (std::max)(body_begin, end)};
            next[index].collapsed = collapsed_.contains(next[index].node_id);
        }
        std::unordered_set<document::NodeId> retained;
        for (const auto& item : next) if (item.collapsed) retained.insert(item.node_id);
        collapsed_ = std::move(retained);
        revision_ = snapshot.source_revision;
    } else {
        collapsed_.clear();
        revision_ = 0;
    }
    items_ = std::move(next);
    if (changed_callback_) changed_callback_();
}

bool HeadingFoldController::toggle(document::NodeId node_id) {
    const auto found = std::find_if(items_.begin(), items_.end(), [node_id](const auto& item) {
        return item.node_id == node_id;
    });
    if (found == items_.end()) return false;
    if (collapsed_.contains(node_id)) collapsed_.erase(node_id);
    else collapsed_.insert(node_id);
    found->collapsed = collapsed_.contains(node_id);
    if (changed_callback_) changed_callback_();
    return true;
}

bool HeadingFoldController::toggle_at(std::uint64_t heading_source_offset) {
    const auto found = std::find_if(items_.begin(), items_.end(),
        [heading_source_offset](const auto& item) {
            return heading_source_offset >= item.heading_range.begin &&
                heading_source_offset <= item.heading_range.end;
        });
    return found != items_.end() && toggle(found->node_id);
}

bool HeadingFoldController::expand_to_reveal(std::uint64_t source_offset) {
    bool changed{};
    for (auto& item : items_) {
        if (item.collapsed && source_offset >= item.body_range.begin &&
            source_offset < item.body_range.end) {
            collapsed_.erase(item.node_id);
            item.collapsed = false;
            changed = true;
        }
    }
    if (changed && changed_callback_) changed_callback_();
    return changed;
}

void HeadingFoldController::reset() {
    collapsed_.clear();
    for (auto& item : items_) item.collapsed = false;
    if (changed_callback_) changed_callback_();
}
void HeadingFoldController::set_changed_callback(std::function<void()> callback) {
    changed_callback_ = std::move(callback);
}
const std::vector<HeadingFoldItem>& HeadingFoldController::items() const noexcept {
    return items_;
}
std::uint64_t HeadingFoldController::revision() const noexcept { return revision_; }
}  // namespace markdownmay::editor
