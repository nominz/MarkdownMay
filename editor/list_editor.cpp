#include "markdownmay/editor/list_editor.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace markdownmay::editor {
namespace {

struct Marker final {
    std::size_t indent{};
    std::size_t content{};
    ListEditor::Kind kind{ListEditor::Kind::none};
};

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

Marker ParseMarker(std::string_view line) {
    Marker value;
    while (value.indent < line.size() && line[value.indent] == ' ') ++value.indent;
    auto cursor = value.indent;
    if (cursor + 6 <= line.size() && line[cursor] == '-' && line[cursor + 1] == ' ' &&
        line[cursor + 2] == '[' && (line[cursor + 3] == ' ' ||
        line[cursor + 3] == 'x' || line[cursor + 3] == 'X') &&
        line[cursor + 4] == ']' && line[cursor + 5] == ' ') {
        value.kind = ListEditor::Kind::task; value.content = cursor + 6; return value;
    }
    if (cursor + 2 <= line.size() &&
        (line[cursor] == '-' || line[cursor] == '*' || line[cursor] == '+') &&
        line[cursor + 1] == ' ') {
        value.kind = ListEditor::Kind::unordered; value.content = cursor + 2; return value;
    }
    const auto digits = cursor;
    while (cursor < line.size() && std::isdigit(static_cast<unsigned char>(line[cursor]))) ++cursor;
    if (cursor > digits && cursor + 2 <= line.size() && line[cursor] == '.' &&
        line[cursor + 1] == ' ') {
        value.kind = ListEditor::Kind::ordered; value.content = cursor + 2; return value;
    }
    value.content = value.indent;
    return value;
}

template <typename Transform>
std::string TransformLines(std::string_view block, Transform transform) {
    std::string output;
    std::size_t cursor{};
    std::uint32_t index{};
    while (cursor <= block.size()) {
        const auto next = block.find('\n', cursor);
        const auto finish = next == std::string_view::npos ? block.size() : next;
        auto line = std::string(block.substr(cursor, finish - cursor));
        bool carriage = !line.empty() && line.back() == '\r';
        if (carriage) line.pop_back();
        const bool has_content = !line.empty();
        output += transform(std::move(line), index);
        if (has_content) ++index;
        if (carriage) output.push_back('\r');
        if (next == std::string_view::npos) break;
        output.push_back('\n');
        cursor = next + 1;
    }
    return output;
}

}  // namespace

ListEditor::ListEditor(document::DocumentSession& session, ParagraphEditor& editor)
    : session_(session), editor_(editor) {}

ErrorCode ListEditor::toggle_unordered() { return Toggle(Kind::unordered, 1); }
ErrorCode ListEditor::toggle_ordered(std::uint32_t start) {
    if (start == 0) return ErrorCode::editor_selection_mapping_failed;
    return Toggle(Kind::ordered, start);
}
ErrorCode ListEditor::toggle_task() { return Toggle(Kind::task, 1); }

ErrorCode ListEditor::Toggle(Kind target, std::uint32_t start) {
    const auto source = session_.snapshot().source;
    const auto selected = editor_.selection();
    const auto begin = LineStart(source, (std::min)(selected.anchor, selected.caret));
    const auto end = LineEnd(source, (std::max)(selected.anchor, selected.caret));
    const auto block = std::string_view(source).substr(static_cast<std::size_t>(begin),
        static_cast<std::size_t>(end - begin));
    bool all_target = true;
    TransformLines(block, [&](std::string line, std::uint32_t) {
        if (!line.empty() && ParseMarker(line).kind != target) all_target = false;
        return line;
    });
    const auto replacement = TransformLines(block,
        [&](std::string line, std::uint32_t index) {
            if (line.empty()) return line;
            const auto marker = ParseMarker(line);
            const auto content = line.substr(marker.content);
            const auto indent = line.substr(0, marker.indent);
            if (all_target) return indent + content;
            if (target == Kind::ordered)
                return indent + std::to_string(start + index) + ". " + content;
            if (target == Kind::task) return indent + "- [ ] " + content;
            return indent + "- " + content;
        });
    return editor_.replace_source_range(begin, end, replacement,
        {begin, begin + replacement.size()});
}

ErrorCode ListEditor::toggle_checked() {
    const auto source = session_.snapshot().source;
    const auto position = editor_.selection().caret;
    const auto begin = LineStart(source, position);
    const auto end = LineEnd(source, position);
    auto line = source.substr(static_cast<std::size_t>(begin),
                              static_cast<std::size_t>(end - begin));
    const auto marker = ParseMarker(line);
    if (marker.kind != Kind::task) return ErrorCode::editor_selection_mapping_failed;
    const auto checkbox = marker.indent + 3;
    line[checkbox] = line[checkbox] == ' ' ? 'x' : ' ';
    return editor_.replace_source_range(begin, end, line, editor_.selection());
}

ErrorCode ListEditor::indent() { return ShiftIndent(true); }
ErrorCode ListEditor::outdent() { return ShiftIndent(false); }

ErrorCode ListEditor::continue_item() {
    const auto source = session_.snapshot().source;
    const auto selection = editor_.selection();
    if (selection.anchor != selection.caret) return ErrorCode::editor_selection_mapping_failed;
    const auto begin = LineStart(source, selection.caret);
    const auto end = LineEnd(source, selection.caret);
    const auto line = source.substr(static_cast<std::size_t>(begin),
                                    static_cast<std::size_t>(end - begin));
    const auto marker = ParseMarker(line);
    if (marker.kind == Kind::none || selection.caret < begin + marker.content)
        return ErrorCode::editor_selection_mapping_failed;
    if (marker.content == line.size()) {
        return editor_.replace_source_range(begin, begin + marker.content, "", {begin, begin});
    }
    auto marker_text = line.substr(0, marker.content);
    if (marker.kind == Kind::task) marker_text[marker.indent + 3] = ' ';
    if (marker.kind == Kind::ordered) {
        std::size_t cursor = marker.indent;
        std::uint32_t number{};
        while (cursor < marker_text.size() && marker_text[cursor] >= '0' &&
               marker_text[cursor] <= '9') {
            number = number * 10 + static_cast<std::uint32_t>(marker_text[cursor] - '0');
            ++cursor;
        }
        marker_text = std::string(marker.indent, ' ') + std::to_string(number + 1) + ". ";
    }
    const auto eol = source.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const auto insertion = std::string(eol) + marker_text;
    const auto next = selection.caret + insertion.size();
    return editor_.replace_source_range(selection.caret, selection.caret, insertion, {next, next});
}

ErrorCode ListEditor::ShiftIndent(bool increase) {
    const auto source = session_.snapshot().source;
    const auto selected = editor_.selection();
    const auto begin = LineStart(source, (std::min)(selected.anchor, selected.caret));
    const auto end = LineEnd(source, (std::max)(selected.anchor, selected.caret));
    const auto block = std::string_view(source).substr(static_cast<std::size_t>(begin),
        static_cast<std::size_t>(end - begin));
    const auto replacement = TransformLines(block,
        [&](std::string line, std::uint32_t) {
            if (line.empty()) return line;
            if (increase) return std::string("    ") + line;
            std::size_t remove{};
            while (remove < 4 && remove < line.size() && line[remove] == ' ') ++remove;
            line.erase(0, remove);
            return line;
        });
    return editor_.replace_source_range(begin, end, replacement,
        {begin, begin + replacement.size()});
}

}  // namespace markdownmay::editor
