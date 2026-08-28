#include "markdownmay/editor/richedit_host.hpp"
#include "markdownmay/editor/table_layout.hpp"

#include <windows.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <wrl/client.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
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
LONG Utf16Offset(std::string_view text, std::uint64_t bytes) {
    return MultiByteToWideChar(CP_UTF8, 0, text.data(),
        static_cast<int>((std::min)(bytes, static_cast<std::uint64_t>(text.size()))),
        nullptr, 0);
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
    if (visible.find(L'|') != std::wstring::npos) return 80;
    if (visible.find(L'\t') == std::wstring::npos) return 85;
    const auto physical_length = GetWindowTextLengthW(host.handle());
    std::wstring physical(static_cast<std::size_t>(physical_length) + 1U, L'\0');
    TEXTRANGEW physical_range{{0, physical_length}, physical.data()};
    const auto physical_copied = static_cast<LONG>(SendMessageW(host.handle(),
        EM_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&physical_range)));
    physical.resize(static_cast<std::size_t>((std::max)(0L, physical_copied)));
    if (visible.find(L'┌') != std::wstring::npos || visible.find(L'│') != std::wstring::npos)
        return 88;
    if (visible.find(L"数据") == std::wstring::npos) return 89;
    const auto native_projection = editor::BuildInlineProjection(
        *session.snapshot().semantic, session.snapshot().source);
    auto native_layouts = editor::BuildTableLayouts(host.handle(), native_projection,
        session.snapshot().source_revision, 96);
    if (native_layouts.size() != 1 || native_layouts[0].cells.empty()) return 82;
    CHARFORMAT2W header_format{};
    header_format.cbSize = sizeof(header_format);
    const auto header_cp = Utf16Offset(native_projection.text,
        native_projection.tables.front().rows.front().cells.front().begin);
    CHARRANGE header_range{header_cp, header_cp + 1};
    SendMessageW(host.handle(), EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&header_range));
    SendMessageW(host.handle(), EM_GETCHARFORMAT, SCF_SELECTION,
        reinterpret_cast<LPARAM>(&header_format));
    if ((header_format.dwEffects & CFE_BOLD) == 0) return 83;
    const auto* data_cell = &native_projection.tables.front().rows[1].cells.front();
    const auto data_at = Utf16Offset(native_projection.text, data_cell->begin);
    CHARRANGE data_range{data_at, data_at + 2};
    SendMessageW(host.handle(), EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&data_range));
    SendMessageW(host.handle(), EM_REPLACESEL, TRUE,
        reinterpret_cast<LPARAM>(L"中文 English"));
    if (host.synchronize_change() != ErrorCode::ok ||
        session.snapshot().source.find("中文 English") == std::string::npos) return 99;
    if (host.undo() != ErrorCode::ok ||
        session.snapshot().source.find("数据") == std::string::npos) return 100;
    table = Table(*session.snapshot().semantic->root());
    if (!table || host.remove_table(table->id) != ErrorCode::ok ||
        !session.snapshot().source.empty()) return 9;
    if (host.undo() != ErrorCode::ok || !Table(*session.snapshot().semantic->root())) return 10;

    const std::string sample =
        "| 项目 | 值 |\n"
        "| --- | --- |\n"
        "| 服务器 | 示例云 ECS |\n"
        "| 公网 IP | 192.0.2.18 |\n"
        "| SSH 登录 | demo-user |\n"
        "| SSH 口令 | example-only |\n"
        "| 管理面板 | 示例面板 |\n"
        "| 面板地址 | https://example.com |\n"
        "| 说明 | 这是一段非常非常长的中文内容，用来检查 RichEdit 原生单元格中的实际排版几何；"
        "折行后的文字必须继续留在第二列内部，不能回到第一列；这里继续追加足够多的中文字符以确保发生自动折行 |\n"
        "| 面板账号 | demo |\n"
        "| 面板口令 | example-only |";
    document::DocumentSession sample_session(sample);
    editor::RichEditHost sample_host(sample_session);
    if (sample_host.create(parent, {0, 0, 760, 560}) != ErrorCode::ok) return 90;
    auto sample_projection = editor::BuildInlineProjection(
        *sample_session.snapshot().semantic, sample_session.snapshot().source);
    auto layouts = editor::BuildTableLayouts(sample_host.handle(), sample_projection,
        sample_session.snapshot().source_revision, 96);
    if (layouts.size() != 1 || layouts[0].row_rects.size() != 10 ||
        layouts[0].column_boundaries.size() != 3 || layouts[0].cells.size() != 20)
        return 91;
    const auto& layout = layouts[0];
    if (layout.table_rect.left != layout.column_boundaries.front() ||
        layout.table_rect.right != layout.column_boundaries.back() ||
        layout.table_rect.top != layout.row_rects.front().top ||
        layout.table_rect.bottom != layout.row_rects.back().bottom) return 92;
    const auto padding = editor::TableHorizontalPadding(96);
    for (const auto& cell : layout.cells) {
        if (cell.row >= layout.row_rects.size() ||
            cell.column + 1 >= layout.column_boundaries.size() ||
            cell.rect.left != layout.column_boundaries[cell.column] ||
            cell.rect.right != layout.column_boundaries[cell.column + 1] ||
            cell.rect.top != layout.row_rects[cell.row].top ||
            cell.rect.bottom != layout.row_rects[cell.row].bottom ||
            cell.content_rect.left - cell.rect.left != padding ||
            cell.rect.right - cell.content_rect.right != padding) return 93;
    }
    for (const auto& row : layout.row_rects)
        if (row.bottom <= row.top) return 94;
    const auto& wrapped_cell = sample_projection.tables.front().rows[7].cells[1];
    const auto wrapped_begin = Utf16Offset(sample_projection.text, wrapped_cell.begin);
    const auto wrapped_end = Utf16Offset(sample_projection.text, wrapped_cell.end);
    POINT wrapped_first{}, wrapped_last{};
    SendMessageW(sample_host.handle(), EM_POSFROMCHAR,
        reinterpret_cast<WPARAM>(&wrapped_first), wrapped_begin);
    SendMessageW(sample_host.handle(), EM_POSFROMCHAR,
        reinterpret_cast<WPARAM>(&wrapped_last), wrapped_end - 1);
    if (wrapped_last.y <= wrapped_first.y ||
        wrapped_last.x < layout.column_boundaries[1] ||
        wrapped_last.x >= layout.column_boundaries[2]) return 97;

    const auto old_right = layout.table_rect.right;
    const auto old_middle = layout.column_boundaries[1];
    MoveWindow(sample_host.handle(), 0, 0, 1800, 900, TRUE);
    auto maximized_layouts = editor::BuildTableLayouts(sample_host.handle(), sample_projection,
        sample_session.snapshot().source_revision, 96);
    std::wstring after_maximize(
        static_cast<std::size_t>(GetWindowTextLengthW(sample_host.handle())) + 1U, L'\0');
    GetWindowTextW(sample_host.handle(), after_maximize.data(),
        static_cast<int>(after_maximize.size()));
    if (maximized_layouts.size() != 1 || maximized_layouts[0].cells.size() != 20 ||
        after_maximize.find(L"示例云 ECS") == std::wstring::npos ||
        after_maximize.find(L"非常非常长的中文内容") == std::wstring::npos) return 111;
    const auto boundary_x = maximized_layouts[0].column_boundaries[1];
    const auto boundary_y = (maximized_layouts[0].table_rect.top +
        maximized_layouts[0].table_rect.bottom) / 2;
    if (!sample_host.is_native_table_column_boundary({boundary_x, boundary_y})) return 112;
    SendMessageW(sample_host.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
        MAKELPARAM(boundary_x, boundary_y));
    SendMessageW(sample_host.handle(), WM_MOUSEMOVE, MK_LBUTTON,
        MAKELPARAM(boundary_x + 120, boundary_y));
    SendMessageW(sample_host.handle(), WM_LBUTTONUP, 0,
        MAKELPARAM(boundary_x + 120, boundary_y));
    auto after_drag_layouts = editor::BuildTableLayouts(sample_host.handle(), sample_projection,
        sample_session.snapshot().source_revision, 96);
    if (after_drag_layouts.size() != 1 || after_drag_layouts[0].cells.size() != 20 ||
        after_drag_layouts[0].column_boundaries[1] != boundary_x ||
        sample_session.snapshot().source != sample) return 113;
    const auto outer_right = after_drag_layouts[0].column_boundaries.back();
    if (!sample_host.is_native_table_column_boundary({outer_right, boundary_y})) return 114;
    SendMessageW(sample_host.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
        MAKELPARAM(outer_right, boundary_y));
    SendMessageW(sample_host.handle(), WM_MOUSEMOVE, MK_LBUTTON,
        MAKELPARAM(outer_right + 180, boundary_y));
    SendMessageW(sample_host.handle(), WM_LBUTTONUP, 0,
        MAKELPARAM(outer_right + 180, boundary_y));
    const auto after_outer_drag = editor::BuildTableLayouts(sample_host.handle(), sample_projection,
        sample_session.snapshot().source_revision, 96);
    if (after_outer_drag.size() != 1 || after_outer_drag[0].cells.size() != 20 ||
        after_outer_drag[0].column_boundaries.back() != outer_right ||
        sample_session.snapshot().source != sample) return 115;
    MoveWindow(sample_host.handle(), 0, 0, 600, 560, TRUE);
    layouts = editor::BuildTableLayouts(sample_host.handle(), sample_projection,
        sample_session.snapshot().source_revision, 96);
    if (layouts.size() != 1 || layouts[0].table_rect.right >= old_right ||
        layouts[0].column_boundaries[1] >= old_middle) return 95;

    sample_host.apply_appearance(RGB(32, 32, 32), RGB(255, 255, 255), 144);
    layouts = editor::BuildTableLayouts(sample_host.handle(), sample_projection,
        sample_session.snapshot().source_revision, 144);
    if (layouts.size() != 1 || layouts[0].cells.empty() ||
        layouts[0].cells.front().content_rect.left -
            layouts[0].cells.front().rect.left != editor::TableHorizontalPadding(144)) return 96;

    // Bypass the mouse guard and directly reproduce the RTF-only width mutation
    // performed by RichEdit's native column tracker.  Synchronization must keep
    // Markdown and every native cell alive even though geometry changed.
    document::DocumentSession format_session(sample);
    editor::RichEditHost format_host(format_session);
    if (format_host.create(parent, {0, 0, 760, 560}) != ErrorCode::ok) return 116;
    MoveWindow(format_host.handle(), 0, 0, 1800, 900, TRUE);
    auto format_projection = editor::BuildInlineProjection(
        *format_session.snapshot().semantic, format_session.snapshot().source);
    auto format_layouts = editor::BuildTableLayouts(format_host.handle(), format_projection,
        format_session.snapshot().source_revision, 96);
    if (format_layouts.size() != 1 || format_layouts[0].cells.size() != 20) return 117;
    POINT format_point{
        (format_layouts[0].cells.front().content_rect.left +
            format_layouts[0].cells.front().content_rect.right) / 2,
        (format_layouts[0].cells.front().content_rect.top +
            format_layouts[0].cells.front().content_rect.bottom) / 2};
    if (!format_host.begin_native_table_pointer_gesture(format_point) ||
        (GetWindowLongPtrW(format_host.handle(), GWL_STYLE) & ES_READONLY) == 0)
        return 129;
    format_host.end_native_table_pointer_gesture();
    if ((GetWindowLongPtrW(format_host.handle(), GWL_STYLE) & ES_READONLY) != 0)
        return 130;
    SendMessageW(format_host.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
        MAKELPARAM(format_point.x, format_point.y));
    SendMessageW(format_host.handle(), WM_LBUTTONUP, 0,
        MAKELPARAM(format_point.x, format_point.y));
    CHARRANGE format_selection{};
    SendMessageW(format_host.handle(), EM_EXGETSEL, 0,
        reinterpret_cast<LPARAM>(&format_selection));
    Microsoft::WRL::ComPtr<IRichEditOle> rich_ole;
    Microsoft::WRL::ComPtr<ITextDocument2> text_document;
    Microsoft::WRL::ComPtr<ITextRange2> native_range;
    Microsoft::WRL::ComPtr<ITextRow> native_row;
    long native_width{};
    const auto format_event_mask = SendMessageW(format_host.handle(), EM_GETEVENTMASK, 0, 0);
    SendMessageW(format_host.handle(), EM_SETEVENTMASK, 0, 0);
    if (!SendMessageW(format_host.handle(), EM_GETOLEINTERFACE, 0,
            reinterpret_cast<LPARAM>(rich_ole.GetAddressOf())) ||
        FAILED(rich_ole.As(&text_document)) || !text_document ||
        FAILED(text_document->Range2(format_selection.cpMin, format_selection.cpMin,
            &native_range)) || !native_range ||
        FAILED(native_range->GetRow(&native_row)) || !native_row ||
        FAILED(native_row->SetCellIndex(0)) ||
        FAILED(native_row->GetCellWidth(&native_width)) ||
        FAILED(native_row->SetCellWidth(native_width + 720)) ||
        FAILED(native_row->Apply(1, tomRowUpdate))) return 121;
    SendMessageW(format_host.handle(), EM_SETEVENTMASK, 0, format_event_mask);
    // The native resize tracker can leave the caret outside the table after a
    // maximized-window drag.  Reproduce that notification shape explicitly.
    CHARRANGE outside_table_selection{0, 0};
    SendMessageW(format_host.handle(), EM_EXSETSEL, 0,
        reinterpret_cast<LPARAM>(&outside_table_selection));
    const auto previous_cursor = SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
    SetCapture(format_host.handle());
    format_host.note_change_notification();
    ReleaseCapture();
    SetCursor(previous_cursor);
    if (format_host.synchronize_change() != ErrorCode::ok) return 119;
    MSG pending{};
    while (PeekMessageW(&pending, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&pending);
        DispatchMessageW(&pending);
    }
    if (format_session.snapshot().source != sample) return 127;
    if (!Table(*format_session.snapshot().semantic->root())) return 128;
    format_layouts = editor::BuildTableLayouts(format_host.handle(), format_projection,
        format_session.snapshot().source_revision, 96);
    if (format_layouts.size() != 1 || format_layouts[0].cells.size() != 20 ||
        format_layouts[0].column_boundaries.size() != 3) return 120;

    const auto v04_path = std::filesystem::path(__FILE__).parent_path().parent_path()
        .parent_path() / "docs" / L"需求规格说明书_V04.md";
    std::ifstream v04_file(v04_path, std::ios::binary);
    std::ostringstream v04_buffer;
    v04_buffer << v04_file.rdbuf();
    const auto v04_source = v04_buffer.str();
    if (v04_source.empty()) return 124;
    document::DocumentSession v04_session(v04_source);
    editor::RichEditHost v04_host(v04_session);
    if (v04_host.create(parent, {0, 0, 760, 560}) != ErrorCode::ok) return 125;
    auto v04_projection = editor::BuildInlineProjection(
        *v04_session.snapshot().semantic, v04_session.snapshot().source);
    auto v04_layouts = editor::BuildTableLayouts(v04_host.handle(), v04_projection,
        v04_session.snapshot().source_revision, 96);
    if (v04_layouts.size() != 9) return 126;

    const std::string wrap_source =
        "表格前普通段落\n\n"
        "| 第一列 | 第二列 | 第三列 |\n"
        "| --- | --- | --- |\n"
        "| 短 | 短 | 非常非常长的第三列中文文字，必须在第三列内部连续自动折行，不能回到第一列区域；"
        "继续追加中文 English mixed text 以确保发生多次折行 |\n"
        "| 非常非常长的第一列中文文字，必须在第一列内部自动折行；继续追加足够多的字符确认边界；"
        "第一列继续追加中文 English mixed text，确保发生多次自动折行 | 短 | 短 |\n"
        "| 短 | 非常非常长的第二列 **粗体中文** 和 `inline code`，必须只在第二列内部自动折行；"
        "第二列继续追加中文 English mixed text，确保发生多次自动折行 | 短 |\n"
        "| 三列都是很长的第一列文本，继续继续继续追加中文确保折行，再追加 English mixed text 和更多中文内容 | 三列都是很长的第二列文本，继续继续继续追加中文确保折行，再追加 English mixed text 和更多中文内容 | 三列都是很长的第三列文本，继续继续继续追加中文确保折行，再追加 English mixed text 和更多中文内容 |\n"
        "\n表格后普通段落\n\n"
        "| A | B |\n| --- | --- |\n| second table | value |";
    document::DocumentSession wrap_session(wrap_source);
    editor::RichEditHost wrap_host(wrap_session);
    if (wrap_host.create(parent, {0, 0, 600, 560}) != ErrorCode::ok) return 101;
    const auto wrap_projection = editor::BuildInlineProjection(
        *wrap_session.snapshot().semantic, wrap_session.snapshot().source);
    const auto wrap_layouts = editor::BuildTableLayouts(wrap_host.handle(), wrap_projection,
        wrap_session.snapshot().source_revision, 96);
    if (wrap_projection.tables.size() != 2 || wrap_layouts.size() != 2 ||
        wrap_layouts.front().column_boundaries.size() != 4) return 102;
    const auto verify_wrap = [&](std::size_t row, std::size_t column) {
        const auto& cell = wrap_projection.tables.front().rows[row].cells[column];
        const auto begin = Utf16Offset(wrap_projection.text, cell.begin);
        const auto end = Utf16Offset(wrap_projection.text, cell.end);
        POINT first{}, last{};
        SendMessageW(wrap_host.handle(), EM_POSFROMCHAR,
            reinterpret_cast<WPARAM>(&first), begin);
        SendMessageW(wrap_host.handle(), EM_POSFROMCHAR,
            reinterpret_cast<WPARAM>(&last), end - 1);
        return last.y > first.y;
    };
    if (!verify_wrap(1, 2)) return 105;
    if (!verify_wrap(2, 0)) return 106;
    if (!verify_wrap(3, 1)) return 107;
    if (!verify_wrap(4, 0)) return 108;
    if (!verify_wrap(4, 1)) return 109;
    if (!verify_wrap(4, 2)) return 110;
    if (wrap_projection.tables.front().rows[3].cells[1].inline_spans.size() < 2) return 104;
    DestroyWindow(parent);
    return 0;
}
