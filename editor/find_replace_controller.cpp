#include "markdownmay/editor/find_replace_controller.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace markdownmay::editor {
namespace {
std::string Fold(std::string_view value) {
    std::string result(value);
    for (auto& character : result) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x80U) character = static_cast<char>(std::tolower(byte));
    }
    return result;
}
bool Equal(std::string_view left, std::string_view right, bool case_sensitive) {
    return case_sensitive ? left == right : Fold(left) == Fold(right);
}
std::size_t Find(std::string_view source, std::string_view query, std::size_t start,
                 bool case_sensitive) {
    if (case_sensitive) return source.find(query, start);
    return Fold(source).find(Fold(query), start);
}
std::size_t RFind(std::string_view source, std::string_view query, std::size_t start,
                  bool case_sensitive) {
    if (case_sensitive) return source.rfind(query, start);
    return Fold(source).rfind(Fold(query), start);
}
}

FindReplaceController::FindReplaceController(document::DocumentSession& session,
                                             ParagraphEditor& editor)
    : session_(session), editor_(editor) {}

Result<TextSelection> FindReplaceController::find(std::string_view query, bool forward,
    bool case_sensitive, bool wrap) {
    if (query.empty()) return Result<TextSelection>::failure(ErrorCode::editor_selection_mapping_failed);
    const auto source = session_.snapshot().source;
    const auto selected = editor_.selection();
    std::size_t found = std::string::npos;
    if (forward) {
        found = Find(source, query, static_cast<std::size_t>((std::max)(selected.anchor, selected.caret)),
                     case_sensitive);
        if (found == std::string::npos && wrap) found = Find(source, query, 0, case_sensitive);
    } else {
        const auto start = (std::min)(selected.anchor, selected.caret);
        found = start == 0 ? std::string::npos
                           : RFind(source, query, static_cast<std::size_t>(start - 1), case_sensitive);
        if (found == std::string::npos && wrap) found = RFind(source, query, source.size(), case_sensitive);
    }
    if (found == std::string::npos)
        return Result<TextSelection>::failure(ErrorCode::editor_selection_mapping_failed);
    TextSelection result{found, found + query.size()};
    const auto selected_result = editor_.set_selection(result);
    return selected_result == ErrorCode::ok ? Result<TextSelection>::success(result)
                                             : Result<TextSelection>::failure(selected_result);
}

ErrorCode FindReplaceController::replace_current(std::string_view query,
    std::string_view replacement, bool case_sensitive) {
    const auto source = session_.snapshot().source;
    const auto selected = editor_.selection();
    const auto begin = (std::min)(selected.anchor, selected.caret);
    const auto end = (std::max)(selected.anchor, selected.caret);
    if (end - begin != query.size() ||
        !Equal(std::string_view(source).substr(static_cast<std::size_t>(begin), query.size()),
               query, case_sensitive)) return ErrorCode::editor_selection_mapping_failed;
    return editor_.insert_text(replacement);
}

Result<std::size_t> FindReplaceController::replace_all(std::string_view query,
    std::string_view replacement, bool case_sensitive) {
    if (query.empty()) return Result<std::size_t>::failure(ErrorCode::editor_selection_mapping_failed);
    const auto source = session_.snapshot().source;
    std::string result;
    std::size_t cursor{}, count{};
    while (cursor < source.size()) {
        const auto found = Find(source, query, cursor, case_sensitive);
        if (found == std::string::npos) break;
        result.append(source, cursor, found - cursor);
        result += replacement;
        cursor = found + query.size();
        ++count;
    }
    if (count == 0) return Result<std::size_t>::success(0);
    result.append(source, cursor, source.size() - cursor);
    const auto changed = editor_.replace_source_range(0, source.size(), std::move(result), {0, 0});
    return changed == ErrorCode::ok ? Result<std::size_t>::success(count)
                                    : Result<std::size_t>::failure(changed);
}

}  // namespace markdownmay::editor
