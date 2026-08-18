#include "markdownmay/editor/richedit_host.hpp"

#include <windows.h>
#include <richedit.h>

#include <algorithm>
#include <string>

namespace {
const markdownmay::document::Node* Table(const markdownmay::document::Node& node) {
    if (node.kind == markdownmay::document::NodeKind::table) return &node;
    for (const auto& child : node.children) if (const auto* found = Table(*child)) return found;
    return nullptr;
}
markdownmay::editor::TablePosition LastCell(const markdownmay::document::Node& table) {
    std::size_t rows{}, columns{};
    for (const auto& section : table.children) for (const auto& row : section->children) {
        if (row->kind != markdownmay::document::NodeKind::table_row) continue;
        ++rows; columns = (std::max)(columns, row->children.size());
    }
    return {rows - 1, columns - 1};
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    using namespace markdownmay;
    const wchar_t class_name[] = L"MarkdownMayTableTest";
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = instance;
    window_class.lpszClassName = class_name;
    RegisterClassW(&window_class);
    const auto parent = CreateWindowExW(0, class_name, L"", WS_OVERLAPPEDWINDOW,
        0, 0, 800, 600, nullptr, nullptr, instance, nullptr);
    if (!parent) return 1;
    document::DocumentSession session("");
    editor::RichEditHost host(session);
    if (host.create(parent, {0, 0, 760, 560}) != ErrorCode::ok ||
        host.insert_table(2, 2) != ErrorCode::ok) return 2;
    auto* table = Table(*session.snapshot().semantic->root());
    if (!table) return 30;
    const auto set_result = host.set_table_cell(table->id, {1, 0}, "数据");
    if (set_result != ErrorCode::ok)
        return set_result == ErrorCode::editor_unmapped_rich_edit_change ? 32 :
               set_result == ErrorCode::document_invalid_state ? 33 : 34;
    table = Table(*session.snapshot().semantic->root());
    if (!table || host.paste_table(table->id, {1, 1}, "10\t20\n30\t40") != ErrorCode::ok)
        return 4;
    table = Table(*session.snapshot().semantic->root());
    if (!table || host.insert_table_column(table->id, 1) != ErrorCode::ok) return 5;
    table = Table(*session.snapshot().semantic->root());
    if (!table || host.delete_table_column(table->id, 1) != ErrorCode::ok) return 6;
    table = Table(*session.snapshot().semantic->root());
    const auto last = table ? LastCell(*table) : editor::TablePosition{};
    auto next = table ? host.navigate_table(table->id, last, true)
                      : Result<editor::TablePosition>::failure(ErrorCode::document_invalid_state);
    if (!next.is_ok()) return 70;
    if (next.value().row != last.row + 1 || next.value().column != 0) return 71;
    std::wstring visible(static_cast<std::size_t>(GetWindowTextLengthW(host.handle())) + 1, L'\0');
    GetWindowTextW(host.handle(), visible.data(), static_cast<int>(visible.size()));
    if (visible.find(L'|') != std::wstring::npos || visible.find(L'\t') == std::wstring::npos ||
        visible.find(L'┌') != std::wstring::npos || visible.find(L'│') != std::wstring::npos ||
        visible.find(L"数据") == std::wstring::npos) return 8;
    const auto first_tab = visible.find(L'\t');
    POINT first_cell{}, second_cell{}, first_row{}, second_row{};
    SendMessageW(host.handle(), EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&first_cell), 0);
    SendMessageW(host.handle(), EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&second_cell),
        static_cast<LPARAM>(first_tab + 1));
    const auto first_newline = visible.find(L'\n');
    SendMessageW(host.handle(), EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&first_row), 0);
    SendMessageW(host.handle(), EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&second_row),
        static_cast<LPARAM>(first_newline + 1));
    if (second_cell.x - first_cell.x < 200 || second_row.y - first_row.y < 24) return 82;
    CHARFORMAT2W header_format{};
    header_format.cbSize = sizeof(header_format);
    CHARRANGE header_range{0, 1};
    SendMessageW(host.handle(), EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&header_range));
    SendMessageW(host.handle(), EM_GETCHARFORMAT, SCF_SELECTION,
        reinterpret_cast<LPARAM>(&header_format));
    if ((header_format.dwEffects & CFE_BOLD) == 0) return 83;
    const auto second_newline = visible.find(L'\n', first_newline + 1);
    if (second_newline == std::wstring::npos) return 84;
    CHARFORMAT2W odd_format{}, even_format{};
    odd_format.cbSize = sizeof(odd_format);
    even_format.cbSize = sizeof(even_format);
    CHARRANGE odd_range{static_cast<LONG>(first_newline + 1),
        static_cast<LONG>(first_newline + 2)};
    CHARRANGE even_range{static_cast<LONG>(second_newline + 1),
        static_cast<LONG>(second_newline + 2)};
    SendMessageW(host.handle(), EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&odd_range));
    SendMessageW(host.handle(), EM_GETCHARFORMAT, SCF_SELECTION,
        reinterpret_cast<LPARAM>(&odd_format));
    SendMessageW(host.handle(), EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&even_range));
    SendMessageW(host.handle(), EM_GETCHARFORMAT, SCF_SELECTION,
        reinterpret_cast<LPARAM>(&even_format));
    if (odd_format.crBackColor == even_format.crBackColor) return 84;
    const auto screen = GetDC(host.handle());
    const auto memory = CreateCompatibleDC(screen);
    const auto bitmap = CreateCompatibleBitmap(screen, 760, 560);
    const auto old_bitmap = SelectObject(memory, bitmap);
    RECT canvas{0, 0, 760, 560};
    FillRect(memory, &canvas, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    host.draw_table_grid(memory);
    int longest{};
    for (int y = 0; y < 560; ++y) {
        int run{};
        for (int x = 0; x < 760; ++x) {
            if (GetPixel(memory, x, y) != RGB(255, 255, 255)) {
                run++;
                longest = (std::max)(longest, run);
            } else run = 0;
        }
    }
    SelectObject(memory, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(host.handle(), screen);
    if (longest < 100) return 81;
    table = Table(*session.snapshot().semantic->root());
    if (!table || host.remove_table(table->id) != ErrorCode::ok ||
        !session.snapshot().source.empty()) return 9;
    if (host.undo() != ErrorCode::ok || !Table(*session.snapshot().semantic->root())) return 10;
    DestroyWindow(parent);
    return 0;
}
