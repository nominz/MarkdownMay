#include "markdownmay/editor/inline_formatter.hpp"

#include <algorithm>
#include <string>

namespace markdownmay::editor {

InlineFormatter::InlineFormatter(document::DocumentSession& session, ParagraphEditor& editor)
    : session_(session), editor_(editor) {}

ErrorCode InlineFormatter::toggle(InlineFormat format) {
    const auto selection = editor_.selection();
    const auto begin = (std::min)(selection.anchor, selection.caret);
    const auto end = (std::max)(selection.anchor, selection.caret);
    if (begin == end) return ErrorCode::editor_selection_mapping_failed;
    const auto source = session_.snapshot().source;
    if (end > source.size()) return ErrorCode::editor_selection_mapping_failed;
    std::string marker;
    switch (format) {
        case InlineFormat::bold: marker = "**"; break;
        case InlineFormat::italic: marker = "*"; break;
        case InlineFormat::strike: marker = "~~"; break;
        case InlineFormat::code: marker = "`"; break;
    }
    const auto marker_size = static_cast<std::uint64_t>(marker.size());
    if (begin >= marker_size && end + marker_size <= source.size() &&
        source.compare(static_cast<std::size_t>(begin - marker_size), marker.size(), marker) == 0 &&
        source.compare(static_cast<std::size_t>(end), marker.size(), marker) == 0) {
        const auto content = source.substr(static_cast<std::size_t>(begin),
                                           static_cast<std::size_t>(end - begin));
        return editor_.replace_source_range(begin - marker_size, end + marker_size, content,
                                             {begin - marker_size, end - marker_size});
    }
    if (begin >= marker_size && end >= marker_size && end <= source.size() &&
        source.compare(static_cast<std::size_t>(begin - marker_size), marker.size(), marker) == 0 &&
        source.compare(static_cast<std::size_t>(end - marker_size), marker.size(), marker) == 0) {
        const auto content_end = end - marker_size;
        const auto content = source.substr(static_cast<std::size_t>(begin),
            static_cast<std::size_t>(content_end - begin));
        return editor_.replace_source_range(begin - marker_size, end, content,
            {begin - marker_size, content_end - marker_size});
    }
    const auto content = source.substr(static_cast<std::size_t>(begin),
                                       static_cast<std::size_t>(end - begin));
    return editor_.replace_source_range(begin, end, marker + content + marker,
                                         {begin + marker_size, end + marker_size});
}

ErrorCode InlineFormatter::set_link(std::string_view target, std::string_view title) {
    const auto selection = editor_.selection();
    const auto begin = (std::min)(selection.anchor, selection.caret);
    const auto end = (std::max)(selection.anchor, selection.caret);
    const auto source = session_.snapshot().source;
    if (begin == end || end > source.size() || target.empty() ||
        target.find_first_of("\r\n)") != std::string_view::npos ||
        title.find_first_of("\r\n\"") != std::string_view::npos) {
        return ErrorCode::editor_selection_mapping_failed;
    }
    const auto label = source.substr(static_cast<std::size_t>(begin),
                                     static_cast<std::size_t>(end - begin));
    std::string replacement = "[" + label + "](" + std::string(target);
    if (!title.empty()) replacement += " \"" + std::string(title) + "\"";
    replacement += ")";
    return editor_.replace_source_range(begin, end, std::move(replacement),
                                         {begin + 1, end + 1});
}

}  // namespace markdownmay::editor
