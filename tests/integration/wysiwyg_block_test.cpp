#include "markdownmay/editor/richedit_host.hpp"

#include <richedit.h>
#include <commdlg.h>

#include <array>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED,
                                  0, 0, 500, 400, nullptr, nullptr, instance, nullptr);
    if (!parent) return 1;
    const std::string source =
        "# 标题\n\n> 引用\n\n```cpp\n代码\n```\n\n---\n";
    markdownmay::document::DocumentSession session(source);
    markdownmay::editor::RichEditHost host(session);
    RECT bounds{0, 0, 500, 400};
    if (host.create(parent, bounds) != markdownmay::ErrorCode::ok) return 2;
    std::array<wchar_t, 256> visible{};
    GetWindowTextW(host.handle(), visible.data(), static_cast<int>(visible.size()));
    const std::wstring text(visible.data());
    if (text.find(L"标题") == std::wstring::npos ||
        text.find(L"引用") == std::wstring::npos ||
        text.find(L"代码") == std::wstring::npos ||
        text.find(L"────────") == std::wstring::npos ||
        text.find(L"```") != std::wstring::npos ||
        text.find(L"> ") != std::wstring::npos || text.find(L"# ") != std::wstring::npos) return 3;
    FINDTEXTEXW find_quote{{0, -1}, const_cast<wchar_t*>(L"引用"), {}};
    if (SendMessageW(host.handle(), EM_FINDTEXTEXW, FR_DOWN,
        reinterpret_cast<LPARAM>(&find_quote)) < 0) return 15;
    SendMessageW(host.handle(), EM_SETSEL, find_quote.chrgText.cpMin,
        find_quote.chrgText.cpMin);
    if (!host.block_active(markdownmay::editor::BlockFormat::quote)) return 16;

    SendMessageW(host.handle(), EM_SETSEL, 0, 2);
    CHARFORMAT2W heading{};
    heading.cbSize = sizeof(heading);
    heading.dwMask = CFM_BOLD | CFM_SIZE;
    SendMessageW(host.handle(), EM_GETCHARFORMAT, SCF_SELECTION,
                 reinterpret_cast<LPARAM>(&heading));
    if ((heading.dwEffects & CFE_BOLD) == 0 || heading.yHeight != 420) return 4;
    host.set_render_style(markdownmay::editor::RenderStyle::song_ying);
    SendMessageW(host.handle(), EM_SETSEL, 0, 2);
    SendMessageW(host.handle(), EM_GETCHARFORMAT, SCF_SELECTION,
                 reinterpret_cast<LPARAM>(&heading));
    if (heading.yHeight != 400) return 40;
    CHARFORMAT2W body{};
    body.cbSize = sizeof(body);
    body.dwMask = CFM_FACE | CFM_SIZE;
    SendMessageW(host.handle(), EM_SETSEL, 3, 3);
    SendMessageW(host.handle(), EM_GETCHARFORMAT, SCF_SELECTION,
                 reinterpret_cast<LPARAM>(&body));
    const std::wstring body_face(body.szFaceName);
    if (body.yHeight != 230 || (body_face != L"SimSun" && body_face != L"宋体")) return 41;
    PARAFORMAT2 heading_paragraph{};
    heading_paragraph.cbSize = sizeof(heading_paragraph);
    heading_paragraph.dwMask = PFM_LINESPACING | PFM_SPACEBEFORE | PFM_SPACEAFTER;
    SendMessageW(host.handle(), EM_SETSEL, 0, 1);
    SendMessageW(host.handle(), EM_GETPARAFORMAT, 0,
        reinterpret_cast<LPARAM>(&heading_paragraph));
    if (heading_paragraph.bLineSpacingRule != 4 ||
        heading_paragraph.dyLineSpacing != 345 ||
        heading_paragraph.dySpaceBefore != 360 ||
        heading_paragraph.dySpaceAfter != 180) return 42;

    FINDTEXTEXW find_code{{0, -1}, const_cast<wchar_t*>(L"代码"), {}};
    if (SendMessageW(host.handle(), EM_FINDTEXTEXW, FR_DOWN,
                     reinterpret_cast<LPARAM>(&find_code)) < 0) return 50;
    const auto code_position = find_code.chrgText.cpMin + 1;
    SendMessageW(host.handle(), EM_SETSEL, code_position, code_position);
    CHARRANGE selected_code{};
    SendMessageW(host.handle(), EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selected_code));
    if (selected_code.cpMin != code_position || selected_code.cpMax != code_position) return 51;
    SendMessageW(host.handle(), EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"新"));
    if (host.synchronize_change() != markdownmay::ErrorCode::ok ||
        session.snapshot().source.find("代新码") == std::string::npos) return 5;
    if (host.undo() != markdownmay::ErrorCode::ok || session.snapshot().source != source) return 6;

    markdownmay::document::DocumentSession plain("标题");
    markdownmay::editor::RichEditHost command_host(plain);
    if (command_host.create(parent, bounds) != markdownmay::ErrorCode::ok) return 7;
    SendMessageW(command_host.handle(), EM_SETSEL, 0, 2);
    if (command_host.set_heading(3) != markdownmay::ErrorCode::ok ||
        plain.snapshot().source != "### 标题") return 8;
    if (command_host.toggle_quote() != markdownmay::ErrorCode::ok) return 9;
    if (command_host.undo() != markdownmay::ErrorCode::ok ||
        plain.snapshot().source != "### 标题") return 10;
    markdownmay::document::DocumentSession empty("");
    markdownmay::editor::RichEditHost empty_host(empty);
    if (empty_host.create(parent, bounds) != markdownmay::ErrorCode::ok) return 11;
    if (empty_host.set_heading(1) != markdownmay::ErrorCode::ok ||
        empty.snapshot().source != "# ") return 12;
    const auto empty_selection = empty_host.source_selection();
    if (!empty_selection.is_ok() || empty_selection.value().caret != 2 ||
        empty_selection.value().anchor != 2) return 13;
    SendMessageW(empty_host.handle(), EM_REPLACESEL, TRUE,
        reinterpret_cast<LPARAM>(L"在"));
    if (empty_host.synchronize_change() != markdownmay::ErrorCode::ok ||
        empty.snapshot().source != "# 在") return 14;

    markdownmay::document::DocumentSession boundary("## 标题\n\n正文\n\n后段");
    markdownmay::editor::RichEditHost boundary_host(boundary);
    if (boundary_host.create(parent, bounds) != markdownmay::ErrorCode::ok) return 52;
    FINDTEXTEXW find_body{{0, -1}, const_cast<wchar_t*>(L"正文"), {}};
    if (SendMessageW(boundary_host.handle(), EM_FINDTEXTEXW, FR_DOWN,
            reinterpret_cast<LPARAM>(&find_body)) < 0) return 53;
    SendMessageW(boundary_host.handle(), EM_SETSEL, find_body.chrgText.cpMin,
        find_body.chrgText.cpMax);
    if (boundary_host.toggle_code_block("python") != markdownmay::ErrorCode::ok) return 54;
    if (boundary.snapshot().source != "## 标题\n\n```python\n正文\n```\n\n后段") return 57;
    find_body.chrg = {0, -1};
    if (SendMessageW(boundary_host.handle(), EM_FINDTEXTEXW, FR_DOWN,
            reinterpret_cast<LPARAM>(&find_body)) < 0) return 55;
    SendMessageW(boundary_host.handle(), EM_SETSEL, find_body.chrgText.cpMin,
        find_body.chrgText.cpMax);
    if (!boundary_host.block_active(markdownmay::editor::BlockFormat::code_block)) return 55;

    // The standard Markdown blank separator is collapsed to one visual paragraph
    // break. Including that break must not select the preceding heading.
    for (LONG leading_separators = 1; leading_separators <= 1; ++leading_separators) {
        markdownmay::document::DocumentSession separator_boundary(
            "## Heading\n\nParagraph A\n\nParagraph B");
        markdownmay::editor::RichEditHost separator_host(separator_boundary);
        if (separator_host.create(parent, bounds) != markdownmay::ErrorCode::ok) return 58;
        FINDTEXTEXW find_a{{0, -1}, const_cast<wchar_t*>(L"Paragraph A"), {}};
        if (SendMessageW(separator_host.handle(), EM_FINDTEXTEXW, FR_DOWN,
                reinterpret_cast<LPARAM>(&find_a)) < 0) return 59;
        SendMessageW(separator_host.handle(), EM_SETSEL,
            find_a.chrgText.cpMin - leading_separators, find_a.chrgText.cpMax);
        if (separator_host.toggle_code_block() != markdownmay::ErrorCode::ok) return 60;
        if (separator_boundary.snapshot().source !=
                "## Heading\n\n```\nParagraph A\n```\n\nParagraph B")
            return static_cast<int>(60 + leading_separators);
    }

    markdownmay::document::DocumentSession collapsed_gap("first\n\nsecond");
    markdownmay::editor::RichEditHost collapsed_host(collapsed_gap);
    if (collapsed_host.create(parent, bounds) != markdownmay::ErrorCode::ok) return 68;
    FINDTEXTEXW find_second{{0, -1}, const_cast<wchar_t*>(L"second"), {}};
    if (SendMessageW(collapsed_host.handle(), EM_FINDTEXTEXW, FR_DOWN,
            reinterpret_cast<LPARAM>(&find_second)) < 0) return 69;
    SendMessageW(collapsed_host.handle(), EM_SETSEL,
        find_second.chrgText.cpMin - 1, find_second.chrgText.cpMin);
    SendMessageW(collapsed_host.handle(), EM_REPLACESEL, TRUE,
        reinterpret_cast<LPARAM>(L""));
    if (collapsed_host.synchronize_change() != markdownmay::ErrorCode::ok ||
        collapsed_gap.snapshot().source != "firstsecond") return 70;

    markdownmay::document::DocumentSession extra_gap("first\n\n\nsecond");
    markdownmay::editor::RichEditHost extra_host(extra_gap);
    if (extra_host.create(parent, bounds) != markdownmay::ErrorCode::ok) return 71;
    find_second.chrg = {0, -1};
    if (SendMessageW(extra_host.handle(), EM_FINDTEXTEXW, FR_DOWN,
            reinterpret_cast<LPARAM>(&find_second)) < 0) return 72;
    SendMessageW(extra_host.handle(), EM_SETSEL,
        find_second.chrgText.cpMin - 2, find_second.chrgText.cpMin - 1);
    SendMessageW(extra_host.handle(), EM_REPLACESEL, TRUE,
        reinterpret_cast<LPARAM>(L""));
    if (extra_host.synchronize_change() != markdownmay::ErrorCode::ok ||
        extra_gap.snapshot().source == "first\n\n\nsecond" ||
        extra_gap.snapshot().source.find("\n\n\n") != std::string::npos) return 73;

    // Regression fixture matching the reported document: inline emphasis in a
    // long CJK paragraph must not pull the preceding level-two heading into the
    // fenced block.
    markdownmay::document::DocumentSession reported(
        "# 企业侧写长什么样\n\n"
        "## ——从\"一张画儿\"到\"一个会长大的东西\"\n\n"
        "企业管理咨询的前期工作既然叫企业侧写，它就应该是一套过程受控、产出成档、结论有据、证据成链的取证作业。\n\n"
        "这一篇就来把这件事写死。先拿一个实物开刀。\n\n"
        "## 一、先审判一份现成的侧写\n\n"
        "《十维度企业侧写》**的骨架，先承认，是对的。** 十个维度、三个层次（身份治理 / 经营竞争 / 运营前瞻）、"
        "一百零三个字段，每个字段是\"标签 + 值\"。\n\n"
        "但接下来是三个死穴。");
    markdownmay::editor::RichEditHost reported_host(reported);
    if (reported_host.create(parent, bounds) != markdownmay::ErrorCode::ok) return 63;
    FINDTEXTEXW find_reported{{0, -1}, const_cast<wchar_t*>(L"《十维度企业侧写》"), {}};
    if (SendMessageW(reported_host.handle(), EM_FINDTEXTEXW, FR_DOWN,
            reinterpret_cast<LPARAM>(&find_reported)) < 0) return 64;
    FINDTEXTEXW find_reported_end{{find_reported.chrgText.cpMin, -1},
        const_cast<wchar_t*>(L"标签 + 值\"。"), {}};
    if (SendMessageW(reported_host.handle(), EM_FINDTEXTEXW, FR_DOWN,
            reinterpret_cast<LPARAM>(&find_reported_end)) < 0) return 65;
    SendMessageW(reported_host.handle(), EM_SETSEL, find_reported.chrgText.cpMin,
        find_reported_end.chrgText.cpMax);
    if (reported_host.toggle_code_block() != markdownmay::ErrorCode::ok) return 66;
    if (reported.snapshot().source !=
            "# 企业侧写长什么样\n\n"
            "## ——从\"一张画儿\"到\"一个会长大的东西\"\n\n"
            "企业管理咨询的前期工作既然叫企业侧写，它就应该是一套过程受控、产出成档、结论有据、证据成链的取证作业。\n\n"
            "这一篇就来把这件事写死。先拿一个实物开刀。\n\n"
            "## 一、先审判一份现成的侧写\n\n"
            "```\n"
            "《十维度企业侧写》**的骨架，先承认，是对的。** 十个维度、三个层次（身份治理 / 经营竞争 / 运营前瞻）、"
            "一百零三个字段，每个字段是\"标签 + 值\"。\n"
            "```\n\n"
            "但接下来是三个死穴。") return 67;

    // Regression for a CRLF document whose first rendered paragraph is
    // selected from the first visible character.
    markdownmay::document::DocumentSession first_crlf(
        "这篇稿子表面上在讲分析，但真正讲的是工作方法。\r\n\r\n"
        "## 一、第一节\r\n\r\n正文一。\r\n\r\n"
        "## 二、第二节\r\n\r\n正文二。\r\n");
    markdownmay::editor::RichEditHost first_crlf_host(first_crlf);
    if (first_crlf_host.create(parent, bounds) != markdownmay::ErrorCode::ok) return 74;
    FINDTEXTEXW find_first{{0, -1}, const_cast<wchar_t*>(L"这篇稿子表面上在讲分析，但真正讲的是工作方法。"), {}};
    if (SendMessageW(first_crlf_host.handle(), EM_FINDTEXTEXW, FR_DOWN,
            reinterpret_cast<LPARAM>(&find_first)) < 0) return 75;
    SendMessageW(first_crlf_host.handle(), EM_SETSEL,
        find_first.chrgText.cpMin, find_first.chrgText.cpMax);
    if (first_crlf_host.toggle_code_block() != markdownmay::ErrorCode::ok) return 76;
    if (first_crlf.snapshot().source !=
            "```\r\n这篇稿子表面上在讲分析，但真正讲的是工作方法。\r\n```\r\n\r\n"
            "## 一、第一节\r\n\r\n正文一。\r\n\r\n"
            "## 二、第二节\r\n\r\n正文二。\r\n") return 77;

    markdownmay::document::DocumentSession trailing_gap(
        "首段。\r\n\r\n## 标题\r\n\r\n后段。\r\n");
    markdownmay::editor::RichEditHost trailing_gap_host(trailing_gap);
    if (trailing_gap_host.create(parent, bounds) != markdownmay::ErrorCode::ok) return 78;
    FINDTEXTEXW find_trailing_heading{{0, -1}, const_cast<wchar_t*>(L"标题"), {}};
    if (SendMessageW(trailing_gap_host.handle(), EM_FINDTEXTEXW, FR_DOWN,
            reinterpret_cast<LPARAM>(&find_trailing_heading)) < 0) return 79;
    SendMessageW(trailing_gap_host.handle(), EM_SETSEL, 0, find_trailing_heading.chrgText.cpMin);
    if (trailing_gap_host.toggle_code_block() != markdownmay::ErrorCode::ok) return 80;
    if (trailing_gap.snapshot().source !=
            "```\r\n首段。\r\n```\r\n\r\n## 标题\r\n\r\n后段。\r\n") return 81;

    DestroyWindow(parent);
    return 0;
}
