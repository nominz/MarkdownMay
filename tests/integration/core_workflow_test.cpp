#include "markdownmay/document/document_session.hpp"
#include "markdownmay/fileio/file_service.hpp"
#include "markdownmay/markdown/markdown_parser.hpp"
#include "markdownmay/markdown/markdown_writer.hpp"

#include <chrono>
#include <filesystem>
#include <string>

namespace {
using namespace markdownmay::document;

std::string Signature(const Node& node) {
    std::string result = std::to_string(static_cast<int>(node.kind)) + ":" + node.text;
    if (const auto* heading = std::get_if<HeadingAttributes>(&node.attributes)) {
        result += ":h" + std::to_string(heading->level);
    } else if (const auto* list = std::get_if<ListAttributes>(&node.attributes)) {
        result += list->ordered ? ":ol" : ":ul";
        result += ":" + std::to_string(list->start);
    } else if (const auto* item = std::get_if<ListItemAttributes>(&node.attributes)) {
        result += item->task ? (item->checked ? ":checked" : ":unchecked") : ":item";
    } else if (const auto* link = std::get_if<LinkAttributes>(&node.attributes)) {
        result += ":" + link->target + ":" + link->title;
    } else if (const auto* code = std::get_if<CodeAttributes>(&node.attributes)) {
        result += ":" + code->language;
    }
    result += "[";
    for (const auto& child : node.children) result += Signature(*child);
    return result + "]";
}

struct TemporaryDirectory final {
    std::filesystem::path path;
    ~TemporaryDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
}  // namespace

int main() {
    using namespace markdownmay;
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory temporary{std::filesystem::temp_directory_path() /
        ("markdownmay-core-workflow-" + std::to_string(nonce))};
    std::filesystem::create_directories(temporary.path);
    const auto file = temporary.path / "workflow.md";
    const std::string initial =
        "# Initial\n\nParagraph with **bold**.\n\n"
        "<section data-keep=\"yes\">unknown block</section>\n";

    if (fileio::SaveTextFileAtomic(
            {file, initial, fileio::TextEncoding::utf8, fileio::LineEnding::lf}) !=
        ErrorCode::ok) return 1;
    const auto opened = fileio::LoadTextFile(file);
    if (!opened.is_ok() || opened.value().source != initial ||
        opened.value().encoding != fileio::TextEncoding::utf8 ||
        opened.value().line_ending != fileio::LineEnding::lf) return 2;

    document::DocumentSession session(opened.value().source);
    const auto before = session.snapshot();
    document::EditTransaction edit{
        1, before.source_revision, document::EditOrigin::source_view,
        {{{static_cast<std::uint64_t>(before.source.size()),
           static_cast<std::uint64_t>(before.source.size())},
          "\nAdded locally.\n"}}};
    if (session.commit(edit) != ErrorCode::ok) return 3;
    const auto modified = session.snapshot();
    if (!modified.semantic || !session.can_export()) return 4;

    const auto serialized = markdown::WriteMarkdown(
        *modified.semantic, {fileio::LineEnding::lf, true});
    if (serialized.find("data-keep") == std::string::npos) return 5;
    if (fileio::SaveTextFileAtomic(
            {file, serialized, fileio::TextEncoding::utf8, fileio::LineEnding::lf}) !=
        ErrorCode::ok) return 6;
    if (session.mark_saved(modified.source_revision) != ErrorCode::ok ||
        session.is_dirty()) return 7;

    const auto reopened = fileio::LoadTextFile(file);
    if (!reopened.is_ok()) return 8;
    const auto reparsed = markdown::ParseMarkdown(reopened.value().source, 1);
    if (!reparsed) return 9;
    if (Signature(*modified.semantic->root()) != Signature(*reparsed->root())) return 10;
    return 0;
}
