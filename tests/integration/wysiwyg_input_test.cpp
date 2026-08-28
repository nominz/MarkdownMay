#include "markdownmay/editor/richedit_host.hpp"

#include <windows.h>
#include <richedit.h>
#include <commdlg.h>

#include <string>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    using namespace markdownmay;
    const wchar_t class_name[] = L"MarkdownMayInputTest";
    WNDCLASSW window_class{}; window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = instance; window_class.lpszClassName = class_name;
    RegisterClassW(&window_class);
    const auto parent = CreateWindowExW(0, class_name, L"", WS_OVERLAPPEDWINDOW,
        0, 0, 800, 600, nullptr, nullptr, instance, nullptr);
    if (!parent) return 1;
    document::DocumentSession session("Alpha 中文 alpha");
    editor::RichEditHost host(session);
    if (host.create(parent, {0, 0, 760, 560}) != ErrorCode::ok) return 2;
    auto found = host.find_text("alpha", true, false);
    if (!found.is_ok() || found.value().anchor != 0 ||
        host.replace_text("alpha", "Beta", false) != ErrorCode::ok) return 3;
    auto all = host.replace_all_text("alpha", "Gamma", false);
    if (!all.is_ok() || all.value() != 1 || session.snapshot().source != "Beta 中文 Gamma") return 4;
    SendMessageW(host.handle(), EM_SETSEL, GetWindowTextLengthW(host.handle()),
                 GetWindowTextLengthW(host.handle()));
    if (host.paste_html("<p><strong>加粗</strong><script>bad</script></p>") != ErrorCode::ok ||
        session.snapshot().source.find("**加粗**") == std::string::npos ||
        session.snapshot().source.find("bad") != std::string::npos) return 5;
    const auto visible_length = GetWindowTextLengthW(host.handle());
    SendMessageW(host.handle(), EM_SETSEL, visible_length - 2, visible_length);
    if (host.execute(editor::EditorCommand::italic) != ErrorCode::ok) return 6;

    if (!OpenClipboard(parent)) return 7;
    EmptyClipboard();
    const std::wstring clipboard_text = L" 剪贴板";
    const auto memory = GlobalAlloc(GMEM_MOVEABLE,
        (clipboard_text.size() + 1) * sizeof(wchar_t));
    auto* target = static_cast<wchar_t*>(GlobalLock(memory));
    memcpy(target, clipboard_text.c_str(), (clipboard_text.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(memory); SetClipboardData(CF_UNICODETEXT, memory); CloseClipboard();
    SendMessageW(host.handle(), EM_SETSEL, GetWindowTextLengthW(host.handle()),
                 GetWindowTextLengthW(host.handle()));
    if (host.paste_from_clipboard() != ErrorCode::ok ||
        session.snapshot().source.find("剪贴板") == std::string::npos) return 8;
    if (host.select_all() != ErrorCode::ok || host.copy() != ErrorCode::ok) return 9;
    const auto cut_result = host.cut();
    if (cut_result != ErrorCode::ok)
        return cut_result == ErrorCode::editor_selection_mapping_failed ? 13 :
               cut_result == ErrorCode::editor_unmapped_rich_edit_change ? 14 :
               cut_result == ErrorCode::document_invalid_state ? 15 : 16;
    if (!session.snapshot().source.empty()) return 12;
    CHARRANGE after_cut{};
    SendMessageW(host.handle(), EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&after_cut));
    if (after_cut.cpMin != 0 || after_cut.cpMax != 0) return 17;
    if (host.undo() != ErrorCode::ok || session.snapshot().source.empty()) return 11;

    document::DocumentSession table_cut(
        "| A | B |\n| --- | --- |\n| x | y |\n\n待剪切段落。\n\n后段。");
    editor::RichEditHost table_cut_host(table_cut);
    if (table_cut_host.create(parent, {0, 0, 760, 560}) != ErrorCode::ok) return 18;
    FINDTEXTEXW find_cut{{0, -1}, const_cast<wchar_t*>(L"待剪切段落。"), {}};
    if (SendMessageW(table_cut_host.handle(), EM_FINDTEXTEXW, FR_DOWN,
            reinterpret_cast<LPARAM>(&find_cut)) < 0) return 19;
    SendMessageW(table_cut_host.handle(), EM_SETSEL,
        find_cut.chrgText.cpMin, find_cut.chrgText.cpMax);
    if (table_cut_host.cut() != ErrorCode::ok ||
        table_cut.snapshot().source !=
            "| A | B |\n| --- | --- |\n| x | y |\n\n\n\n后段。") return 20;
    CHARRANGE table_cut_selection{};
    SendMessageW(table_cut_host.handle(), EM_EXGETSEL, 0,
        reinterpret_cast<LPARAM>(&table_cut_selection));
    if (table_cut_selection.cpMin != table_cut_selection.cpMax) return 21;
    DestroyWindow(parent);
    return 0;
}
