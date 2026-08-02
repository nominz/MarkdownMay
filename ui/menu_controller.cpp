#include "markdownmay/ui/menu_controller.hpp"

#include <array>
#include <utility>

namespace markdownmay::ui {
namespace {
constexpr UINT Native(app::CommandId command) noexcept {
    return static_cast<UINT>(command);
}

bool IsKnown(std::uint16_t value) noexcept {
    return (value >= Native(app::CommandId::file_new) &&
            value <= Native(app::CommandId::file_exit)) ||
        (value >= Native(app::CommandId::edit_undo) &&
            value <= Native(app::CommandId::edit_replace)) ||
        (value >= Native(app::CommandId::format_bold) &&
            value <= Native(app::CommandId::format_task_list)) ||
        (value >= Native(app::CommandId::view_render) &&
            value <= Native(app::CommandId::view_split)) ||
        value == Native(app::CommandId::help_about);
}
}

MenuController::MenuController(Query query, Execute execute)
    : query_(std::move(query)), execute_(std::move(execute)) {}

MenuController::~MenuController() {
    if (accelerator_) DestroyAcceleratorTable(accelerator_);
}

bool MenuController::create(HWND window) {
    window_ = window;
    menu_ = CreateMenu();
    const auto file = CreatePopupMenu();
    const auto edit = CreatePopupMenu();
    const auto format = CreatePopupMenu();
    const auto view = CreatePopupMenu();
    const auto help = CreatePopupMenu();
    recent_menu_ = CreatePopupMenu();
    if (!menu_ || !file || !edit || !format || !view || !help || !recent_menu_)
        return false;

    AddCommand(file, app::CommandId::file_new, L"新建(&N)\tCtrl+N");
    AddCommand(file, app::CommandId::file_open, L"打开(&O)...\tCtrl+O");
    AddCommand(file, app::CommandId::file_save, L"保存(&S)\tCtrl+S");
    AddCommand(file, app::CommandId::file_save_as, L"另存为(&A)...\tCtrl+Shift+S");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_POPUP, reinterpret_cast<UINT_PTR>(recent_menu_), L"最近文件(&R)");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AddCommand(file, app::CommandId::file_exit, L"退出(&X)\tAlt+F4");

    AddCommand(edit, app::CommandId::edit_undo, L"撤销(&U)\tCtrl+Z");
    AddCommand(edit, app::CommandId::edit_redo, L"重做(&R)\tCtrl+Y");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AddCommand(edit, app::CommandId::edit_cut, L"剪切(&T)\tCtrl+X");
    AddCommand(edit, app::CommandId::edit_copy, L"复制(&C)\tCtrl+C");
    AddCommand(edit, app::CommandId::edit_paste, L"粘贴(&P)\tCtrl+V");
    AddCommand(edit, app::CommandId::edit_select_all, L"全选(&A)\tCtrl+A");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AddCommand(edit, app::CommandId::edit_find, L"查找(&F)...\tCtrl+F");
    AddCommand(edit, app::CommandId::edit_replace, L"替换(&H)...\tCtrl+H");

    AddCommand(format, app::CommandId::format_bold, L"粗体(&B)\tCtrl+B");
    AddCommand(format, app::CommandId::format_italic, L"斜体(&I)\tCtrl+I");
    AddCommand(format, app::CommandId::format_strike, L"删除线(&S)");
    AddCommand(format, app::CommandId::format_inline_code, L"行内代码(&C)");
    AppendMenuW(format, MF_SEPARATOR, 0, nullptr);
    AddCommand(format, app::CommandId::format_quote, L"引用(&Q)");
    AddCommand(format, app::CommandId::format_unordered_list, L"无序列表(&U)");
    AddCommand(format, app::CommandId::format_ordered_list, L"有序列表(&O)");
    AddCommand(format, app::CommandId::format_task_list, L"任务列表(&T)");

    AddCommand(view, app::CommandId::view_render, L"渲染模式(&R)\tCtrl+1");
    AddCommand(view, app::CommandId::view_source, L"源码模式(&S)\tCtrl+2");
    AddCommand(view, app::CommandId::view_split, L"对照模式(&P)\tCtrl+3");
    AddCommand(help, app::CommandId::help_about, L"关于马冬梅(&A)\tF1");

    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"文件(&F)");
    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), L"编辑(&E)");
    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(format), L"格式(&O)");
    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"视图(&V)");
    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"帮助(&H)");
    if (!SetMenu(window_, menu_)) return false;

    const std::array<ACCEL, 16> keys{{
        {FVIRTKEY | FCONTROL, 'N', static_cast<WORD>(app::CommandId::file_new)},
        {FVIRTKEY | FCONTROL, 'O', static_cast<WORD>(app::CommandId::file_open)},
        {FVIRTKEY | FCONTROL, 'S', static_cast<WORD>(app::CommandId::file_save)},
        {FVIRTKEY | FCONTROL | FSHIFT, 'S', static_cast<WORD>(app::CommandId::file_save_as)},
        {FVIRTKEY | FCONTROL, 'Z', static_cast<WORD>(app::CommandId::edit_undo)},
        {FVIRTKEY | FCONTROL, 'Y', static_cast<WORD>(app::CommandId::edit_redo)},
        {FVIRTKEY | FCONTROL, 'X', static_cast<WORD>(app::CommandId::edit_cut)},
        {FVIRTKEY | FCONTROL, 'C', static_cast<WORD>(app::CommandId::edit_copy)},
        {FVIRTKEY | FCONTROL, 'V', static_cast<WORD>(app::CommandId::edit_paste)},
        {FVIRTKEY | FCONTROL, 'A', static_cast<WORD>(app::CommandId::edit_select_all)},
        {FVIRTKEY | FCONTROL, 'B', static_cast<WORD>(app::CommandId::format_bold)},
        {FVIRTKEY | FCONTROL, 'I', static_cast<WORD>(app::CommandId::format_italic)},
        {FVIRTKEY | FCONTROL, '1', static_cast<WORD>(app::CommandId::view_render)},
        {FVIRTKEY | FCONTROL, '2', static_cast<WORD>(app::CommandId::view_source)},
        {FVIRTKEY | FCONTROL, '3', static_cast<WORD>(app::CommandId::view_split)},
        {FVIRTKEY, VK_F1, static_cast<WORD>(app::CommandId::help_about)},
    }};
    accelerator_ = CreateAcceleratorTableW(
        const_cast<ACCEL*>(keys.data()), static_cast<int>(keys.size()));
    refresh();
    return accelerator_ != nullptr;
}

bool MenuController::dispatch(std::uint16_t native_id) {
    const bool recent = native_id >= Native(app::CommandId::recent_first) &&
        native_id <= Native(app::CommandId::recent_clear);
    if (!IsKnown(native_id) && !recent) return false;
    const auto command = static_cast<app::CommandId>(native_id);
    if (query_ && !query_(command).enabled) return true;
    if (execute_) execute_(command);
    refresh();
    return true;
}

void MenuController::set_recent_files(std::vector<std::filesystem::path> files) {
    if (!recent_menu_) return;
    while (GetMenuItemCount(recent_menu_) > 0)
        DeleteMenu(recent_menu_, 0, MF_BYPOSITION);
    if (files.empty()) {
        AppendMenuW(recent_menu_, MF_STRING | MF_GRAYED, 0, L"（没有最近文件）");
    } else {
        for (std::size_t index = 0; index < files.size() && index < 20; ++index) {
            auto label = std::to_wstring(index + 1) + L"  " + files[index].wstring();
            AppendMenuW(recent_menu_, MF_STRING,
                Native(app::CommandId::recent_first) + static_cast<UINT>(index), label.c_str());
        }
        AppendMenuW(recent_menu_, MF_SEPARATOR, 0, nullptr);
        AddCommand(recent_menu_, app::CommandId::recent_clear, L"清除最近文件");
    }
    if (window_) DrawMenuBar(window_);
}

void MenuController::refresh() {
    if (!menu_ || !query_) return;
    for (std::uint16_t value = 100; value <= 500; ++value) {
        if (!IsKnown(value)) continue;
        const auto state = query_(static_cast<app::CommandId>(value));
        EnableMenuItem(menu_, value, MF_BYCOMMAND |
            (state.enabled ? MF_ENABLED : MF_GRAYED));
        CheckMenuItem(menu_, value, MF_BYCOMMAND |
            (state.checked ? MF_CHECKED : MF_UNCHECKED));
    }
    DrawMenuBar(window_);
}

HACCEL MenuController::accelerator() const noexcept { return accelerator_; }

void MenuController::AddCommand(HMENU menu, app::CommandId command,
                                const wchar_t* text) {
    AppendMenuW(menu, MF_STRING, Native(command), text);
}

}  // namespace markdownmay::ui
