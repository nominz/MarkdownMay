#pragma once

#include "markdownmay/document/document.hpp"

#include <cstdint>
#include <memory>

namespace markdownmay::document {

// Reuses node IDs for structurally unchanged subtrees after a full reparse.
// Source ranges and the document revision always come from `current`.
[[nodiscard]] std::shared_ptr<const Document> ReconcileNodeIds(
    const Document& previous,
    const Document& current,
    std::uint64_t revision);

}  // namespace markdownmay::document
