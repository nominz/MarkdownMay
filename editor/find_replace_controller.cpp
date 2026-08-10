#include "markdownmay/editor/find_replace_controller.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <vector>

namespace markdownmay::editor {
namespace {
enum class TokenKind { literal, any_one, any_many, paragraph, tab };
struct Token { TokenKind kind{}; std::string text; };

std::size_t NextCodePoint(std::string_view value, std::size_t offset) {
    if (offset >= value.size()) return value.size();
    ++offset;
    while (offset < value.size() &&
           (static_cast<unsigned char>(value[offset]) & 0xC0U) == 0x80U) ++offset;
    return offset;
}
bool IsBoundary(std::string_view value, std::size_t offset) {
    return offset == value.size() ||
        (static_cast<unsigned char>(value[offset]) & 0xC0U) != 0x80U;
}
bool ByteEqual(char left, char right, bool case_sensitive) {
    const auto l = static_cast<unsigned char>(left);
    const auto r = static_cast<unsigned char>(right);
    return case_sensitive || l >= 0x80U || r >= 0x80U
        ? l == r : std::tolower(l) == std::tolower(r);
}
bool LiteralAt(std::string_view source, std::size_t offset, std::string_view literal,
               bool case_sensitive) {
    if (offset + literal.size() > source.size()) return false;
    for (std::size_t index = 0; index < literal.size(); ++index)
        if (!ByteEqual(source[offset + index], literal[index], case_sensitive)) return false;
    return true;
}
std::vector<Token> ParsePattern(std::string_view query, bool wildcards) {
    std::vector<Token> tokens;
    for (std::size_t index = 0; index < query.size();) {
        if (index + 1 < query.size() && query[index] == '^' &&
            (query[index + 1] == 'p' || query[index + 1] == 'P')) {
            tokens.push_back({TokenKind::paragraph, {}}); index += 2; continue;
        }
        if (index + 1 < query.size() && query[index] == '^' &&
            (query[index + 1] == 't' || query[index + 1] == 'T')) {
            tokens.push_back({TokenKind::tab, {}}); index += 2; continue;
        }
        if (wildcards && query[index] == '?') {
            tokens.push_back({TokenKind::any_one, {}}); ++index; continue;
        }
        if (wildcards && query[index] == '*') {
            if (tokens.empty() || tokens.back().kind != TokenKind::any_many)
                tokens.push_back({TokenKind::any_many, {}});
            ++index; continue;
        }
        const auto next = NextCodePoint(query, index);
        tokens.push_back({TokenKind::literal, std::string(query.substr(index, next - index))});
        index = next;
    }
    return tokens;
}
std::optional<std::size_t> MatchTokens(std::string_view source,
    const std::vector<Token>& tokens, std::size_t token, std::size_t offset,
    bool case_sensitive) {
    if (token == tokens.size()) return offset;
    const auto& current = tokens[token];
    if (current.kind == TokenKind::any_many) {
        auto end = offset;
        while (end < source.size() && source[end] != '\r' && source[end] != '\n')
            end = NextCodePoint(source, end);
        for (;;) {
            if (auto matched = MatchTokens(source, tokens, token + 1, end, case_sensitive))
                return matched;
            if (end == offset) break;
            auto previous = end - 1;
            while (previous > offset && !IsBoundary(source, previous)) --previous;
            end = previous;
        }
        return std::nullopt;
    }
    if (offset >= source.size()) return std::nullopt;
    std::size_t next = offset;
    if (current.kind == TokenKind::any_one) {
        if (source[offset] == '\r' || source[offset] == '\n') return std::nullopt;
        next = NextCodePoint(source, offset);
    } else if (current.kind == TokenKind::paragraph) {
        if (source[offset] == '\r') next = offset + (offset + 1 < source.size() && source[offset + 1] == '\n' ? 2 : 1);
        else if (source[offset] == '\n') next = offset + 1;
        else return std::nullopt;
    } else if (current.kind == TokenKind::tab) {
        if (source[offset] != '\t') return std::nullopt;
        next = offset + 1;
    } else {
        if (!LiteralAt(source, offset, current.text, case_sensitive)) return std::nullopt;
        next = offset + current.text.size();
    }
    return MatchTokens(source, tokens, token + 1, next, case_sensitive);
}
std::optional<TextSelection> FindMatch(std::string_view source,
    const std::vector<Token>& tokens, std::size_t start, bool forward,
    bool case_sensitive, bool allow_empty) {
    if (forward) {
        for (auto offset = (std::min)(start, source.size()); offset <= source.size();
             offset = offset == source.size() ? source.size() + 1 : NextCodePoint(source, offset)) {
            if (!IsBoundary(source, offset)) continue;
            if (auto end = MatchTokens(source, tokens, 0, offset, case_sensitive);
                end && (allow_empty || *end > offset)) return TextSelection{offset, *end};
        }
    } else {
        auto offset = (std::min)(start, source.size());
        for (;;) {
            if (IsBoundary(source, offset)) {
                if (auto end = MatchTokens(source, tokens, 0, offset, case_sensitive);
                    end && *end <= start && (allow_empty || *end > offset))
                    return TextSelection{offset, *end};
            }
            if (offset == 0) break;
            --offset;
        }
    }
    return std::nullopt;
}
}

FindReplaceController::FindReplaceController(document::DocumentSession& session,
                                             ParagraphEditor& editor)
    : session_(session), editor_(editor) {}

Result<TextSelection> FindReplaceController::find(std::string_view query, bool forward,
    bool case_sensitive, bool wrap, bool wildcards) {
    if (query.empty()) return Result<TextSelection>::failure(ErrorCode::editor_selection_mapping_failed);
    const auto source = session_.snapshot().source;
    const auto selected = editor_.selection();
    const auto tokens = ParsePattern(query, wildcards);
    std::optional<TextSelection> found;
    if (forward) {
        found = FindMatch(source, tokens, static_cast<std::size_t>((std::max)(selected.anchor, selected.caret)),
                          true, case_sensitive, false);
        if (!found && wrap) found = FindMatch(source, tokens, 0, true, case_sensitive, false);
    } else {
        const auto start = (std::min)(selected.anchor, selected.caret);
        if (start > 0) found = FindMatch(source, tokens, static_cast<std::size_t>(start), false,
                                        case_sensitive, false);
        if (!found && wrap) found = FindMatch(source, tokens, source.size(), false,
                                             case_sensitive, false);
    }
    if (!found)
        return Result<TextSelection>::failure(ErrorCode::editor_selection_mapping_failed);
    const auto selected_result = editor_.set_selection(*found);
    return selected_result == ErrorCode::ok ? Result<TextSelection>::success(*found)
                                             : Result<TextSelection>::failure(selected_result);
}

ErrorCode FindReplaceController::replace_current(std::string_view query,
    std::string_view replacement, bool case_sensitive, bool wildcards) {
    const auto source = session_.snapshot().source;
    const auto selected = editor_.selection();
    const auto begin = (std::min)(selected.anchor, selected.caret);
    const auto end = (std::max)(selected.anchor, selected.caret);
    const auto tokens = ParsePattern(query, wildcards);
    const auto matched = MatchTokens(source, tokens, 0, static_cast<std::size_t>(begin), case_sensitive);
    if (!matched || *matched != end || end == begin) return ErrorCode::editor_selection_mapping_failed;
    return editor_.insert_text(expand_special_format(replacement));
}

Result<std::size_t> FindReplaceController::replace_all(std::string_view query,
    std::string_view replacement, bool case_sensitive, bool wildcards) {
    if (query.empty()) return Result<std::size_t>::failure(ErrorCode::editor_selection_mapping_failed);
    const auto source = session_.snapshot().source;
    const auto tokens = ParsePattern(query, wildcards);
    const auto expanded = expand_special_format(replacement);
    std::string result;
    std::size_t cursor{}, count{};
    while (cursor < source.size()) {
        const auto found = FindMatch(source, tokens, cursor, true, case_sensitive, false);
        if (!found) break;
        result.append(source, cursor, static_cast<std::size_t>(found->anchor) - cursor);
        result += expanded;
        cursor = static_cast<std::size_t>(found->caret);
        ++count;
    }
    if (count == 0) return Result<std::size_t>::success(0);
    result.append(source, cursor, source.size() - cursor);
    const auto changed = editor_.replace_source_range(0, source.size(), std::move(result), {0, 0});
    return changed == ErrorCode::ok ? Result<std::size_t>::success(count)
                                    : Result<std::size_t>::failure(changed);
}

std::string FindReplaceController::expand_special_format(std::string_view value) {
    std::string result;
    for (std::size_t index = 0; index < value.size();) {
        if (index + 1 < value.size() && value[index] == '^' &&
            (value[index + 1] == 'p' || value[index + 1] == 'P')) {
            result.push_back('\n'); index += 2;
        } else if (index + 1 < value.size() && value[index] == '^' &&
                   (value[index + 1] == 't' || value[index + 1] == 'T')) {
            result.push_back('\t'); index += 2;
        } else {
            result.push_back(value[index++]);
        }
    }
    return result;
}

}  // namespace markdownmay::editor
