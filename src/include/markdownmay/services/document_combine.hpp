#pragma once

#include "markdownmay/document/document_session.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace markdownmay::services {

struct InsertResource final {
    std::filesystem::path source;
    std::filesystem::path target;
    bool create{};
};

struct InsertPlan final {
    std::uint64_t base_revision{};
    std::uint64_t utf8_offset{};
    std::string insertion;
    std::vector<InsertResource> resources;
};

struct InsertResult final {
    std::uint64_t revision{};
    std::vector<std::filesystem::path> created_resources;
};

struct InsertRequest final {
    document::SessionSnapshot snapshot;
    std::filesystem::path current_document_path;
    std::filesystem::path source_path;
    std::uint64_t utf8_offset{};
};

class DocumentCombineService final {
public:
    [[nodiscard]] static Result<InsertPlan> Prepare(const InsertRequest& request);
    [[nodiscard]] static Result<InsertResult> Commit(
        document::DocumentSession& session, const InsertPlan& plan,
        std::uint64_t transaction_id);
};

}  // namespace markdownmay::services
