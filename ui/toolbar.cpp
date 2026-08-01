#include "markdownmay/ui/toolbar.hpp"

#include <commctrl.h>

#include <array>
#include <utility>

namespace markdownmay::ui {
namespace {
constexpr int Native(app::CommandId command) noexcept {
    return static_cast<int>(command);
}

struct ButtonDefinition final {
    app::CommandId command;
    const wchar_t* text;
    BYTE style;
};
}

Toolbar::Toolbar(Query query) : query_(std::move(query)) {}

bool Toolbar::create(HWND parent) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
    static_cast<void>(InitCommonControlsEx(&controls));
    handle_ = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS |
            CCS_NODIVIDER | CCS_NORESIZE,
        0, 0, 0, height_, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!handle_) return false;
    SendMessageW(handle_, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(handle_, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS);

    constexpr std::array<ButtonDefinition, 11> definitions{{
        {app::CommandId::format_bold, L"粗体", BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT},
        {app::CommandId::format_italic, L"斜体", BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT},
        {app::CommandId::format_strike, L"删除线", BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT},
        {app::CommandId::format_inline_code, L"代码", BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT},
        {app::CommandId::format_quote, L"引用", BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT},
        {app::CommandId::format_unordered_list, L"项目", BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT},
        {app::CommandId::format_ordered_list, L"编号", BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT},
        {app::CommandId::format_task_list, L"任务", BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT},
        {app::CommandId::view_render, L"渲染", BTNS_CHECKGROUP | BTNS_AUTOSIZE | BTNS_SHOWTEXT},
        {app::CommandId::view_source, L"源码", BTNS_CHECKGROUP | BTNS_AUTOSIZE | BTNS_SHOWTEXT},
        {app::CommandId::view_split, L"对照", BTNS_CHECKGROUP | BTNS_AUTOSIZE | BTNS_SHOWTEXT},
    }};
    std::array<TBBUTTON, definitions.size() + 1> buttons{};
    std::size_t output{};
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        if (index == 8) {
            buttons[output].fsStyle = BTNS_SEP;
            ++output;
        }
        const auto& definition = definitions[index];
        buttons[output].iBitmap = I_IMAGENONE;
        buttons[output].idCommand = Native(definition.command);
        buttons[output].fsState = TBSTATE_ENABLED;
        buttons[output].fsStyle = definition.style;
        buttons[output].iString = reinterpret_cast<INT_PTR>(definition.text);
        ++output;
    }
    if (!SendMessageW(handle_, TB_ADDBUTTONSW, output,
            reinterpret_cast<LPARAM>(buttons.data()))) return false;
    SendMessageW(handle_, TB_AUTOSIZE, 0, 0);
    RECT bounds{};
    GetWindowRect(handle_, &bounds);
    height_ = bounds.bottom - bounds.top;
    refresh();
    return true;
}

void Toolbar::resize(int width) {
    if (handle_) MoveWindow(handle_, 0, 0, width, height_, TRUE);
}

void Toolbar::refresh() {
    if (!handle_ || !query_) return;
    constexpr std::array commands{
        app::CommandId::format_bold, app::CommandId::format_italic,
        app::CommandId::format_strike, app::CommandId::format_inline_code,
        app::CommandId::format_quote, app::CommandId::format_unordered_list,
        app::CommandId::format_ordered_list, app::CommandId::format_task_list,
        app::CommandId::view_render, app::CommandId::view_source,
        app::CommandId::view_split};
    for (const auto command : commands) {
        const auto state = query_(command);
        SendMessageW(handle_, TB_ENABLEBUTTON, Native(command),
            MAKELONG(state.enabled, 0));
        SendMessageW(handle_, TB_CHECKBUTTON, Native(command),
            MAKELONG(state.checked, 0));
    }
}

HWND Toolbar::handle() const noexcept { return handle_; }
int Toolbar::height() const noexcept { return height_; }

const wchar_t* Toolbar::tooltip(std::uint16_t command) noexcept {
    switch (static_cast<app::CommandId>(command)) {
    case app::CommandId::format_bold: return L"粗体（Ctrl+B）";
    case app::CommandId::format_italic: return L"斜体（Ctrl+I）";
    case app::CommandId::format_strike: return L"删除线";
    case app::CommandId::format_inline_code: return L"行内代码";
    case app::CommandId::format_quote: return L"引用";
    case app::CommandId::format_unordered_list: return L"无序列表";
    case app::CommandId::format_ordered_list: return L"有序列表";
    case app::CommandId::format_task_list: return L"任务列表";
    case app::CommandId::view_render: return L"切换到渲染模式（Ctrl+1）";
    case app::CommandId::view_source: return L"切换到源码模式（Ctrl+2）";
    case app::CommandId::view_split: return L"切换到对照模式（Ctrl+3）";
    default: return L"";
    }
}

}  // namespace markdownmay::ui
