#include "markdownmay/editor/block_formatter.hpp"

#include "markdownmay/fileio/line_endings.hpp"

#include <algorithm>
#include <optional>
#include <string>

namespace markdownmay::editor {
namespace {

std::uint64_t LineStart(std::string_view source, std::uint64_t position) {
    position = (std::min)(position, static_cast<std::uint64_t>(source.size()));
    if (position == 0) return 0;
    const auto found = source.rfind('\n', static_cast<std::size_t>(position - 1));
    return found == std::string_view::npos ? 0 : static_cast<std::uint64_t>(found + 1);
}

std::uint64_t LineEnd(std::string_view source, std::uint64_t position) {
    const auto found = source.find('\n', static_cast<std::size_t>(position));
    auto end = found == std::string_view::npos ? source.size() : found;
    if (end > 0 && source[end - 1] == '\r') --end;
    return static_cast<std::uint64_t>(end);
}

std::string_view Ending(std::string_view source) {
    return fileio::DetectLineEnding(source) == fileio::LineEnding::lf ? "\n" : "\r\n";
}

struct SelectedBlockRange final {
    std::uint64_t begin{};
    std::uint64_t end{};
    document::NodeKind kind{document::NodeKind::paragraph};
};

std::optional<SelectedBlockRange> SelectedTopLevelBlocks(
        const document::Document& document, std::string_view source,
        const TextSelection& selection) {
    auto selected_begin = (std::min)(selection.anchor, selection.caret);
    const auto selected_end = (std::max)(selection.anchor, selection.caret);
    while (selected_begin < selected_end && selected_begin < source.size() &&
           (source[static_cast<std::size_t>(selected_begin)] == '\r' ||
            source[static_cast<std::size_t>(selected_begin)] == '\n')) ++selected_begin;
    const bool caret_only = selected_begin == selected_end;
    const document::Node* first{};
    const document::Node* last{};
    for (const auto& block : document.root()->children) {
        const auto begin = (std::min)(block->source.begin,
            static_cast<std::uint64_t>(source.size()));
        const auto end = (std::min)(block->source.end,
            static_cast<std::uint64_t>(source.size()));
        const bool covered = caret_only
            ? selected_begin >= begin && selected_begin <= end
            : selected_begin < end && selected_end > begin;
        if (!covered) continue;
        if (!first) first = block.get();
        last = block.get();
    }
    if (!first || !last) return std::nullopt;
    auto first_content = (std::min)(first->source.begin,
        static_cast<std::uint64_t>(source.size()));
    while (first_content < first->source.end &&
           (source[static_cast<std::size_t>(first_content)] == '\r' ||
            source[static_cast<std::size_t>(first_content)] == '\n')) ++first_content;
    auto last_content_end = (std::min)(last->source.end,
        static_cast<std::uint64_t>(source.size()));
    while (last_content_end > last->source.begin &&
           (source[static_cast<std::size_t>(last_content_end - 1)] == '\r' ||
            source[static_cast<std::size_t>(last_content_end - 1)] == '\n')) --last_content_end;
    return SelectedBlockRange{LineStart(source, first_content),
        LineEnd(source, last_content_end),
        first == last ? first->kind : document::NodeKind::document};
}

std::uint64_t PreviousLineStart(std::string_view source, std::uint64_t line_begin) {
    if (line_begin == 0) return 0;
    auto position = line_begin - 1;
    if (source[static_cast<std::size_t>(position)] == '\n' && position > 0) --position;
    return LineStart(source, position);
}

}  // namespace

BlockFormatter::BlockFormatter(document::DocumentSession& session, ParagraphEditor& editor)
    : session_(session), editor_(editor) {}

ErrorCode BlockFormatter::set_heading(std::uint8_t level) {
    if (level > 6) return ErrorCode::editor_selection_mapping_failed;
    const auto source = session_.snapshot().source;
    const auto selection = editor_.selection();
    const auto line_begin = LineStart(source, (std::min)(selection.anchor, selection.caret));
    const auto line_end = LineEnd(source, (std::max)(selection.anchor, selection.caret));
    if (source.find('\n', static_cast<std::size_t>(line_begin)) < line_end)
        return ErrorCode::editor_selection_mapping_failed;
    std::uint64_t content_begin = line_begin;
    while (content_begin < line_end && source[static_cast<std::size_t>(content_begin)] == '#')
        ++content_begin;
    if (content_begin > line_begin && content_begin < line_end &&
        source[static_cast<std::size_t>(content_begin)] == ' ') ++content_begin;
    const auto content = source.substr(static_cast<std::size_t>(content_begin),
                                       static_cast<std::size_t>(line_end - content_begin));
    const auto marker = level == 0 ? std::string{} : std::string(level, '#') + " ";
    const auto begin = line_begin + marker.size();
    return editor_.replace_source_range(line_begin, line_end, marker + content,
                                         {begin, begin + content.size()});
}

ErrorCode BlockFormatter::toggle_quote() {
    const auto source = session_.snapshot().source;
    const auto selected = editor_.selection();
    const auto begin = LineStart(source, (std::min)(selected.anchor, selected.caret));
    const auto end = LineEnd(source, (std::max)(selected.anchor, selected.caret));
    const auto block = source.substr(static_cast<std::size_t>(begin),
                                     static_cast<std::size_t>(end - begin));
    bool all_quoted = true;
    std::size_t cursor{};
    while (cursor <= block.size()) {
        const auto next = block.find('\n', cursor);
        const auto length = (next == std::string::npos ? block.size() : next) - cursor;
        if (length > 0 && block[cursor] != '>') all_quoted = false;
        if (next == std::string::npos) break;
        cursor = next + 1;
    }
    std::string replacement;
    cursor = 0;
    while (cursor <= block.size()) {
        const auto next = block.find('\n', cursor);
        const auto finish = next == std::string::npos ? block.size() : next;
        auto line = block.substr(cursor, finish - cursor);
        if (all_quoted) {
            if (!line.empty() && line.front() == '>') line.erase(0, 1);
            if (!line.empty() && line.front() == ' ') line.erase(0, 1);
        } else if (!line.empty()) {
            line.insert(0, "> ");
        }
        replacement += line;
        if (next == std::string::npos) break;
        replacement.push_back('\n');
        cursor = next + 1;
    }
    return editor_.replace_source_range(begin, end, replacement,
                                         {begin, begin + replacement.size()});
}

ErrorCode BlockFormatter::toggle_code_block(std::string_view language) {
    if (language.find_first_of("\r\n`") != std::string_view::npos)
        return ErrorCode::editor_selection_mapping_failed;
    const auto source = session_.snapshot().source;
    const auto selected = editor_.selection();
    const auto semantic = session_.snapshot().semantic;
    const auto selected_blocks = semantic
        ? SelectedTopLevelBlocks(*semantic, source, selected) : std::nullopt;
    const auto begin = selected_blocks ? selected_blocks->begin :
        LineStart(source, (std::min)(selected.anchor, selected.caret));
    const auto end = selected_blocks ? selected_blocks->end :
        LineEnd(source, (std::max)(selected.anchor, selected.caret));
    const auto eol = Ending(source);
    if (selected_blocks && selected_blocks->kind == document::NodeKind::code_block) {
        const auto opening_begin = PreviousLineStart(source, begin);
        const auto opening_end = LineEnd(source, opening_begin);
        auto content_begin = opening_end + eol.size();
        auto closing_begin = LineStart(source, end);
        if (closing_begin < content_begin ||
            source.compare(static_cast<std::size_t>(closing_begin), 3, "```") != 0) {
            const auto next = source.find('\n', static_cast<std::size_t>(end));
            closing_begin = next == std::string_view::npos
                ? static_cast<std::uint64_t>(source.size())
                : static_cast<std::uint64_t>(next + 1);
        }
        if (opening_end >= opening_begin + 3 && closing_begin + 3 <= source.size() &&
            source.compare(static_cast<std::size_t>(opening_begin), 3, "```") == 0 &&
            source.compare(static_cast<std::size_t>(closing_begin), 3, "```") == 0) {
            const auto closing_end = LineEnd(source, closing_begin);
            auto content_end = closing_begin;
            if (content_end >= eol.size() &&
                source.substr(static_cast<std::size_t>(content_end - eol.size()), eol.size()) == eol)
                content_end -= eol.size();
            const auto content = source.substr(static_cast<std::size_t>(content_begin),
                static_cast<std::size_t>(content_end - content_begin));
            return editor_.replace_source_range(opening_begin, closing_end, content,
                {opening_begin, opening_begin + content.size()});
        }
    }
    const auto content = source.substr(static_cast<std::size_t>(begin),
                                       static_cast<std::size_t>(end - begin));
    const std::string open = "```" + std::string(language) + std::string(eol);
    const std::string close = std::string(eol) + "```";
    return editor_.replace_source_range(begin, end, open + content + close,
        {begin + open.size(), begin + open.size() + content.size()});
}

ErrorCode BlockFormatter::insert_thematic_break() {
    const auto source = session_.snapshot().source;
    const auto position = editor_.selection().caret;
    if (position > source.size()) return ErrorCode::editor_selection_mapping_failed;
    const auto eol = std::string(Ending(source));
    std::string value;
    if (position > 0 && source[static_cast<std::size_t>(position - 1)] != '\n') value += eol;
    value += "---" + eol;
    if (position < source.size() && source[static_cast<std::size_t>(position)] != '\r' &&
        source[static_cast<std::size_t>(position)] != '\n') value += eol;
    const auto next = position + value.size();
    return editor_.replace_source_range(position, position, std::move(value), {next, next});
}

}  // namespace markdownmay::editor
