#include "markdownmay/document/document_reconciler.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace markdownmay::document {
namespace {

void AppendBytes(std::string& target, const void* value, std::size_t size) {
    target.append(static_cast<const char*>(value), size);
}

template <typename T>
void AppendValue(std::string& target, const T& value) {
    AppendBytes(target, &value, sizeof(value));
}

void AppendString(std::string& target, const std::string& value) {
    const auto size = static_cast<std::uint64_t>(value.size());
    AppendValue(target, size);
    target.append(value);
}

using SignatureCache = std::unordered_map<const Node*, std::string>;

const std::string& Signature(const Node& node, SignatureCache& cache) {
    if (const auto found = cache.find(&node); found != cache.end()) return found->second;
    std::string result;
    result.reserve(node.text.size() + node.children.size() * 16U + 32U);
    AppendValue(result, node.kind);
    AppendValue(result, node.attributes.index());
    std::visit([&](const auto& attributes) {
        using T = std::decay_t<decltype(attributes)>;
        if constexpr (std::is_same_v<T, HeadingAttributes>) {
            AppendValue(result, attributes.level);
        } else if constexpr (std::is_same_v<T, ListAttributes>) {
            AppendValue(result, attributes.ordered);
            AppendValue(result, attributes.start);
            AppendValue(result, attributes.tight);
        } else if constexpr (std::is_same_v<T, ListItemAttributes>) {
            AppendValue(result, attributes.task);
            AppendValue(result, attributes.checked);
        } else if constexpr (std::is_same_v<T, LinkAttributes>) {
            AppendString(result, attributes.target);
            AppendString(result, attributes.title);
        } else if constexpr (std::is_same_v<T, CodeAttributes>) {
            AppendString(result, attributes.language);
        }
    }, node.attributes);
    AppendString(result, node.text);
    const auto child_count = static_cast<std::uint64_t>(node.children.size());
    AppendValue(result, child_count);
    for (const auto& child : node.children) AppendString(result, Signature(*child, cache));
    return cache.emplace(&node, std::move(result)).first->second;
}

void Collect(const std::shared_ptr<const Node>& node,
             std::unordered_map<std::string, std::vector<NodeId>>& ids,
             SignatureCache& signatures,
             NodeId& maximum) {
    maximum = (std::max)(maximum, node->id);
    ids[Signature(*node, signatures)].push_back(node->id);
    for (const auto& child : node->children) Collect(child, ids, signatures, maximum);
}

void FindMaximum(const std::shared_ptr<const Node>& node, NodeId& maximum) {
    maximum = (std::max)(maximum, node->id);
    for (const auto& child : node->children) FindMaximum(child, maximum);
}

std::shared_ptr<const Node> Clone(
    const std::shared_ptr<const Node>& source,
    std::unordered_map<std::string, std::vector<NodeId>>& reusable,
    std::unordered_map<std::string, std::size_t>& cursors,
    SignatureCache& signatures,
    std::unordered_set<NodeId>& used,
    NodeId& next_id) {
    NodeId id{};
    const auto& signature = Signature(*source, signatures);
    auto found = reusable.find(signature);
    if (found != reusable.end()) {
        auto& cursor = cursors[signature];
        while (cursor < found->second.size() && used.contains(found->second[cursor])) ++cursor;
        if (cursor < found->second.size()) id = found->second[cursor++];
    }
    if (id == 0 || used.contains(id)) {
        do {
            if (next_id == (std::numeric_limits<NodeId>::max)()) return {};
            ++next_id;
        } while (used.contains(next_id));
        id = next_id;
    }
    used.insert(id);

    auto node = std::make_shared<Node>();
    node->id = id;
    node->kind = source->kind;
    node->source = source->source;
    node->attributes = source->attributes;
    node->text = source->text;
    node->children.reserve(source->children.size());
    for (const auto& child : source->children) {
        auto cloned = Clone(child, reusable, cursors, signatures, used, next_id);
        if (!cloned) return {};
        node->children.push_back(std::move(cloned));
    }
    return node;
}

}  // namespace

std::shared_ptr<const Document> ReconcileNodeIds(
    const Document& previous,
    const Document& current,
    std::uint64_t revision) {
    std::unordered_map<std::string, std::vector<NodeId>> reusable;
    SignatureCache previous_signatures;
    SignatureCache current_signatures;
    NodeId maximum{};
    Collect(previous.root(), reusable, previous_signatures, maximum);
    FindMaximum(current.root(), maximum);
    std::unordered_map<std::string, std::size_t> cursors;
    std::unordered_set<NodeId> used;
    auto root = Clone(current.root(), reusable, cursors, current_signatures, used, maximum);
    if (!root) return {};
    auto reconciled = std::make_shared<Document>(std::move(root), revision);
    return reconciled->validate(current.root()->source.end) ? reconciled : nullptr;
}

}  // namespace markdownmay::document
