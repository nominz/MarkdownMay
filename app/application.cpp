#include "markdownmay/app/application.hpp"
#include "markdownmay/services/document_combine.hpp"
#include "markdownmay/services/document_split.hpp"
#include "markdownmay/services/source_position_mapper.hpp"
#include "markdownmay/export/docx_writer.hpp"
#include "markdownmay/export/html_writer.hpp"
#include "markdownmay/export/pdf_writer.hpp"
#include "markdownmay/export/txt_writer.hpp"

#include <commdlg.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shlobj.h>

#include <array>
#include <cwchar>
#include <string_view>
#include <atomic>
#include <thread>
#include <optional>

namespace markdownmay::app {
namespace {
fileio::LineEnding PreferredMixedEnding(std::string_view source) noexcept {
    std::size_t crlf{};
    std::size_t lf{};
    for (std::size_t index = 0; index < source.size(); ++index) {
        if (source[index] != '\n') continue;
        if (index && source[index - 1] == '\r') ++crlf;
        else ++lf;
    }
    return crlf >= lf ? fileio::LineEnding::crlf : fileio::LineEnding::lf;
}

std::filesystem::path RecentFilePath() {
    std::array<wchar_t, 32768> local{};
    const auto length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", local.data(), static_cast<DWORD>(local.size()));
    if (length > 0 && length < local.size())
        return std::filesystem::path(local.data()) / L"MarkdownMay" / L"recent.ini";
    return std::filesystem::temp_directory_path() / L"MarkdownMay-recent.ini";
}
std::filesystem::path SettingsFilePath() {
    auto path = RecentFilePath();
    path.replace_filename(L"settings.ini");
    return path;
}

ui::ThemePreference ToUiTheme(services::ThemeSetting value) noexcept {
    if (value == services::ThemeSetting::light) return ui::ThemePreference::light;
    if (value == services::ThemeSetting::dark) return ui::ThemePreference::dark;
    return ui::ThemePreference::follow_system;
}
services::ThemeSetting ToSettingTheme(ui::ThemePreference value) noexcept {
    if (value == ui::ThemePreference::light) return services::ThemeSetting::light;
    if (value == ui::ThemePreference::dark) return services::ThemeSetting::dark;
    return services::ThemeSetting::follow_system;
}

int ConfirmUnsavedChanges(HWND owner) {
    return MessageBoxW(owner,
        L"当前文档有尚未保存的修改。是否先保存？\n\n"
        L"选择“是”保存，选择“否”放弃修改，选择“取消”继续编辑。",
        L"马冬梅", MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON1);
}

class PlacementDialog final {
public:
    std::optional<std::filesystem::path> show(HWND owner,
                                              std::filesystem::path initial,
                                              ui::ThemePreference theme_preference) {
        owner_ = owner;
        value_ = std::move(initial);
        theme_preference_ = theme_preference;
        const wchar_t class_name[] = L"MarkdownMay.PlacementDialog";
        WNDCLASSEXW type{sizeof(type)};
        type.lpfnWndProc = WindowProc;
        type.hInstance = GetModuleHandleW(nullptr);
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        type.hbrBackground = nullptr;
        type.lpszClassName = class_name;
        RegisterClassExW(&type);
        window_ = CreateWindowExW(WS_EX_DLGMODALFRAME, class_name,
            L"安置马冬梅", WS_CAPTION | WS_SYSMENU | WS_POPUP,
            CW_USEDEFAULT, CW_USEDEFAULT, 640, 324, owner_, nullptr,
            GetModuleHandleW(nullptr), this);
        if (!window_) return std::nullopt;
        CenterAndShow();
        EnableWindow(owner_, FALSE);
        MSG message{};
        while (window_ && GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(window_, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        EnableWindow(owner_, TRUE);
        SetForegroundWindow(owner_);
        ReleaseAppearance();
        return accepted_ ? std::optional(value_) : std::nullopt;
    }

private:
    enum : int { edit_path = 1001, browse = 1002 };

    static LRESULT CALLBACK WindowProc(HWND window, UINT message,
                                       WPARAM w_param, LPARAM l_param) {
        auto* self = reinterpret_cast<PlacementDialog*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = reinterpret_cast<PlacementDialog*>(
                reinterpret_cast<CREATESTRUCTW*>(l_param)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->window_ = window;
        }
        return self ? self->HandleMessage(message, w_param, l_param)
                    : DefWindowProcW(window, message, w_param, l_param);
    }

    LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param) {
        if (message == WM_CREATE) {
            auto make = [&](const wchar_t* type, const wchar_t* text, DWORD style,
                            int id) {
                const auto control = CreateWindowExW(wcscmp(type, L"EDIT") == 0
                        ? WS_EX_CLIENTEDGE : 0,
                    type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                    window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                    GetModuleHandleW(nullptr), nullptr);
                return control;
            };
            heading_ = make(L"STATIC", L"选择马冬梅的长期保存位置", SS_LEFT, 0);
            note_ = make(L"STATIC",
                L"安置完成后，马冬梅将从新位置重新打开；原位置的程序不会被删除。",
                SS_LEFT, 0);
            size_note_ = make(L"STATIC",
                L"程序约 2 MB，可以放心保存在系统盘。", SS_LEFT, 0);
            label_ = make(L"STATIC", L"目标文件夹(&F)：", SS_LEFT, 0);
            edit_ = make(L"EDIT", value_.c_str(), ES_AUTOHSCROLL | WS_TABSTOP, edit_path);
            browse_ = make(L"BUTTON", L"浏览(&B)…", BS_PUSHBUTTON | WS_TABSTOP, browse);
            target_ = make(L"STATIC", L"目标程序：MarkdownMay.exe", SS_LEFT, 0);
            divider_ = make(L"STATIC", L"", SS_ETCHEDHORZ, 0);
            ok_ = make(L"BUTTON", L"安置(&P)", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK);
            cancel_ = make(L"BUTTON", L"取消", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL);
            dpi_ = GetDpiForWindow(window_);
            ApplyAppearance();
            SetFocus(edit_);
            SendMessageW(edit_, EM_SETSEL, 0, -1);
            return 0;
        }
        if (message == WM_COMMAND) {
            const auto id = LOWORD(w_param);
            if (id == browse) { Browse(); return 0; }
            if (id == IDOK) { Accept(); return 0; }
            if (id == IDCANCEL) { DestroyWindow(window_); return 0; }
        }
        if (message == WM_SETTINGCHANGE || message == WM_THEMECHANGED) {
            ApplyAppearance();
            return 0;
        }
        if (message == WM_DPICHANGED) {
            dpi_ = HIWORD(w_param);
            const auto* suggested = reinterpret_cast<const RECT*>(l_param);
            SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOACTIVATE | SWP_NOZORDER);
            ApplyAppearance();
            return 0;
        }
        if (message == WM_ERASEBKGND) {
            RECT client{};
            GetClientRect(window_, &client);
            FillRect(reinterpret_cast<HDC>(w_param), &client, background_brush_);
            return 1;
        }
        if (message == WM_CTLCOLORSTATIC) {
            const auto control = reinterpret_cast<HWND>(l_param);
            const auto dc = reinterpret_cast<HDC>(w_param);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, control == note_ || control == size_note_ || control == target_
                    ? palette_.muted : palette_.text);
            return reinterpret_cast<LRESULT>(background_brush_);
        }
        if (message == WM_CTLCOLOREDIT) {
            const auto dc = reinterpret_cast<HDC>(w_param);
            SetTextColor(dc, palette_.text);
            SetBkColor(dc, palette_.window);
            return reinterpret_cast<LRESULT>(edit_brush_);
        }
        if (message == WM_CLOSE) { DestroyWindow(window_); return 0; }
        if (message == WM_DESTROY) { window_ = nullptr; return 0; }
        return DefWindowProcW(window_, message, w_param, l_param);
    }

    void Layout() {
        auto px = [this](int value) { return ui::ScaleForDpi(value, dpi_); };
        RECT client{};
        GetClientRect(window_, &client);
        const int width = client.right - client.left;
        const int content_width = width - px(48);
        SetWindowPos(heading_, nullptr, px(24), px(22), content_width, px(30), SWP_NOZORDER);
        SetWindowPos(note_, nullptr, px(24), px(58), content_width, px(22), SWP_NOZORDER);
        SetWindowPos(size_note_, nullptr, px(24), px(82), content_width, px(22), SWP_NOZORDER);
        SetWindowPos(label_, nullptr, px(24), px(119), px(130), px(22), SWP_NOZORDER);
        SetWindowPos(edit_, nullptr, px(24), px(144), width - px(148), px(30), SWP_NOZORDER);
        SetWindowPos(browse_, nullptr, width - px(114), px(144), px(90), px(30), SWP_NOZORDER);
        SetWindowPos(target_, nullptr, px(24), px(181), content_width, px(22), SWP_NOZORDER);
        SetWindowPos(divider_, nullptr, 0, px(218), width, px(2), SWP_NOZORDER);
        SetWindowPos(ok_, nullptr, width - px(214), px(236), px(90), px(32), SWP_NOZORDER);
        SetWindowPos(cancel_, nullptr, width - px(114), px(236), px(90), px(32), SWP_NOZORDER);
    }

    void ApplyAppearance() {
        theme_ = ui::ResolveTheme(theme_preference_, ui::ReadSystemTheme());
        palette_ = ui::PaletteFor(theme_);
        ReleaseAppearance();
        background_brush_ = CreateSolidBrush(palette_.surface);
        edit_brush_ = CreateSolidBrush(palette_.window);
        NONCLIENTMETRICSW metrics{sizeof(metrics)};
        if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
                &metrics, 0, dpi_)) {
            font_ = CreateFontIndirectW(&metrics.lfMessageFont);
            auto heading = metrics.lfMessageFont;
            heading.lfHeight = -MulDiv(14, static_cast<int>(dpi_), 72);
            heading.lfWeight = FW_SEMIBOLD;
            heading_font_ = CreateFontIndirectW(&heading);
        }
        const auto fallback = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        for (const auto control : {note_, size_note_, label_, edit_, browse_, target_, ok_, cancel_})
            SendMessageW(control, WM_SETFONT,
                reinterpret_cast<WPARAM>(font_ ? font_ : fallback), TRUE);
        SendMessageW(heading_, WM_SETFONT,
            reinterpret_cast<WPARAM>(heading_font_ ? heading_font_ : fallback), TRUE);
        static_cast<void>(ui::ApplyTitleBarTheme(window_, theme_));
        Layout();
        InvalidateRect(window_, nullptr, TRUE);
    }

    void ReleaseAppearance() {
        if (font_) DeleteObject(font_);
        if (heading_font_) DeleteObject(heading_font_);
        if (background_brush_) DeleteObject(background_brush_);
        if (edit_brush_) DeleteObject(edit_brush_);
        font_ = nullptr;
        heading_font_ = nullptr;
        background_brush_ = nullptr;
        edit_brush_ = nullptr;
    }

    void CenterAndShow() {
        RECT owner{};
        GetWindowRect(owner_, &owner);
        const int width = ui::ScaleForDpi(640, dpi_);
        const int height = ui::ScaleForDpi(324, dpi_);
        const int x = owner.left + ((owner.right - owner.left) - width) / 2;
        const int y = owner.top + ((owner.bottom - owner.top) - height) / 2;
        SetWindowPos(window_, HWND_TOP, x, y, width, height, SWP_SHOWWINDOW);
    }

    void Browse() {
        IFileDialog* dialog{};
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&dialog)))) return;
        DWORD options{};
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
            FOS_PATHMUSTEXIST);
        std::array<wchar_t, 32768> current{};
        GetWindowTextW(edit_, current.data(), static_cast<int>(current.size()));
        IShellItem* initial{};
        if (*current.data() && SUCCEEDED(SHCreateItemFromParsingName(
                current.data(), nullptr, IID_PPV_ARGS(&initial)))) {
            dialog->SetFolder(initial);
            initial->Release();
        }
        if (SUCCEEDED(dialog->Show(window_))) {
            IShellItem* selected{};
            if (SUCCEEDED(dialog->GetResult(&selected))) {
                PWSTR path{};
                if (SUCCEEDED(selected->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    SetWindowTextW(edit_, path);
                    CoTaskMemFree(path);
                }
                selected->Release();
            }
        }
        dialog->Release();
    }

    void Accept() {
        std::array<wchar_t, 32768> path{};
        GetWindowTextW(edit_, path.data(), static_cast<int>(path.size()));
        if (!*path.data()) {
            MessageBoxW(window_, L"请输入或浏览选择一个目标文件夹。",
                L"安置马冬梅", MB_OK | MB_ICONWARNING);
            return;
        }
        value_ = path.data();
        accepted_ = true;
        DestroyWindow(window_);
    }

    HWND owner_{};
    HWND window_{};
    HWND heading_{};
    HWND note_{};
    HWND size_note_{};
    HWND label_{};
    HWND edit_{};
    HWND browse_{};
    HWND target_{};
    HWND divider_{};
    HWND ok_{};
    HWND cancel_{};
    UINT dpi_{USER_DEFAULT_SCREEN_DPI};
    ui::ThemePreference theme_preference_{ui::ThemePreference::follow_system};
    ui::ThemeKind theme_{ui::ThemeKind::light};
    ui::ThemePalette palette_{};
    HFONT font_{};
    HFONT heading_font_{};
    HBRUSH background_brush_{};
    HBRUSH edit_brush_{};
    std::filesystem::path value_;
    bool accepted_{};
};

std::wstring QuoteCommandLineArgument(std::wstring_view value) {
    std::wstring result(1, L'"');
    std::size_t slashes{};
    for (const auto character : value) {
        if (character == L'\\') { ++slashes; continue; }
        if (character == L'"') {
            result.append(slashes * 2 + 1, L'\\');
            result.push_back(L'"');
        } else {
            result.append(slashes, L'\\');
            result.push_back(character);
        }
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

struct ExportRun final {
    exporting::ExportDocument document;
    exporting::ExportFormat format{};
    std::filesystem::path target;
    exporting::CancellationSource cancellation;
    std::jthread worker;
    std::atomic<bool> done{};
    ErrorCode result{ErrorCode::document_invalid_state};
};

HRESULT CALLBACK ExportProgressCallback(HWND window, UINT message, WPARAM w_param,
                                        LPARAM, LONG_PTR data) {
    auto& run = *reinterpret_cast<ExportRun*>(data);
    if (message == TDN_CREATED) {
        SendMessageW(window, TDM_SET_PROGRESS_BAR_RANGE, 0, MAKELPARAM(0, 100));
        run.worker = std::jthread([&run, window] {
            const auto progress = [window](const exporting::ExportProgress& value) {
                PostMessageW(window, TDM_SET_PROGRESS_BAR_POS, value.completed, 0);
            };
            switch (run.format) {
            case exporting::ExportFormat::pdf: run.result = exporting::ExportPdf(run.document, run.target, run.cancellation.token(), progress); break;
            case exporting::ExportFormat::docx: run.result = exporting::ExportDocx(run.document, run.target, run.cancellation.token(), progress); break;
            case exporting::ExportFormat::txt: run.result = exporting::ExportTxt(run.document, run.target, run.cancellation.token(), progress); break;
            case exporting::ExportFormat::html: run.result = exporting::ExportHtml(run.document, run.target, run.cancellation.token(), progress); break;
            }
            run.done = true;
            PostMessageW(window, TDM_CLICK_BUTTON, IDCANCEL, 0);
        });
    } else if (message == TDN_BUTTON_CLICKED && w_param == IDCANCEL && !run.done) {
        run.cancellation.cancel();
        return S_FALSE;
    }
    return S_OK;
}
}

Application::Application(HINSTANCE instance)
    : instance_(instance),
      dispatcher_(main_window_.document_window(), [this] {
          if (main_window_.handle()) PostMessageW(main_window_.handle(), WM_CLOSE, 0, 0);
      }, {
          [] { return true; },
          [this] { return NewDocument(); },
          [this] { return OpenDocumentDialog(); },
          [this] { return SaveDocument(); },
          [this] { return SaveDocumentAs(); },
          [this] { return PrintDocument(); },
          [this] { return PageSetup(); },
          [this] { return ExportDocumentDialog(); },
          [this](std::size_t index) { return OpenRecentFile(index); },
          [this] { return ClearRecentFiles(); },
      }, {
          [] { return true; },
          [this] { return main_window_.document_window().is_named(); },
          [this] { return InsertDocumentDialog(); },
          [this] { return SplitDocumentDialog(); },
      }, {
          [this] { return file_association_.state(ExecutablePath()) !=
              platform::AssociationState::current; },
          [this] { return file_association_.state(ExecutablePath()) !=
              platform::AssociationState::not_registered; },
          [this] { return RegisterFileAssociations(); },
          [this] { return UnregisterFileAssociations(); },
          [this] { return OpenDefaultApps(); },
          [this] { return PlaceApplication(); },
      }, {
          [this] { return main_window_.theme_preference(); },
          [this](ui::ThemePreference value) {
              main_window_.set_theme_preference(value);
              settings_.theme = ToSettingTheme(value);
              SaveSettings();
          },
      }),
      recent_files_(RecentFilePath(), 20),
      settings_store_(SettingsFilePath()),
      file_association_() {
    main_window_.set_command_callbacks(
        [this](CommandId command) { return dispatcher_.query(command); },
        [this](CommandId command) {
            const auto result = dispatcher_.execute(command);
            if (result != ErrorCode::ok && main_window_.handle()) {
                if (command >= CommandId::file_new && command <= CommandId::file_save_as)
                    ShowFileError(result);
                else if (command == CommandId::tools_place_application) {
                    // PlaceApplication already presents a specific diagnostic.
                }
                else if (command == CommandId::edit_insert_document ||
                         command == CommandId::edit_split_document) {
                    // The document operation already presents a specific diagnostic.
                }
                else
                    MessageBoxW(main_window_.handle(), L"当前操作无法完成，请检查文档内容后重试。",
                        L"马冬梅", MB_OK | MB_ICONWARNING);
            }
        });
    main_window_.set_drop_callback([this](const std::filesystem::path& path) {
        if (!ConfirmDocumentReplacement()) return;
        const auto result = OpenPath(path);
        if (result != ErrorCode::ok) ShowFileError(result);
    });
    main_window_.set_close_callback([this] { return ConfirmClose(); });
    main_window_.set_activate_callback([this] { CheckExternalModification(); });
    main_window_.set_open_request_callback([this] { DrainOpenRequests(); });
}

int Application::run(int show_command) {
    LoadSettings();
    main_window_.document_window().set_outline_visible(settings_.outline_visible);
    main_window_.set_theme_preference(ToUiTheme(settings_.theme));
    if (main_window_.create(instance_, show_command) != ErrorCode::ok) return 1;
    const auto initial_mode = settings_.default_mode == services::DefaultViewMode::source
        ? editor::ViewMode::source : settings_.default_mode == services::DefaultViewMode::split
        ? editor::ViewMode::split : editor::ViewMode::render;
    static_cast<void>(main_window_.document_window().modes().switch_to(initial_mode));
    RefreshRecentFiles();
    DrainOpenRequests();
    MSG message{};
    int result{};
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (main_window_.accelerator() && TranslateAcceleratorW(
                main_window_.handle(), main_window_.accelerator(), &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    const auto mode = main_window_.document_window().modes().mode();
    settings_.default_mode = mode == editor::ViewMode::source
        ? services::DefaultViewMode::source : mode == editor::ViewMode::split
        ? services::DefaultViewMode::split : services::DefaultViewMode::render;
    settings_.outline_visible = main_window_.document_window().outline_visible();
    SaveSettings();
    return result < 0 ? 2 : static_cast<int>(message.wParam);
}

void Application::LoadSettings() {
    const auto loaded = settings_store_.load();
    settings_ = loaded.is_ok() ? loaded.value() : services::Settings{};
    page_setup_ = {settings_.print_landscape,
        settings_.margin_left_hundredths_mm, settings_.margin_top_hundredths_mm,
        settings_.margin_right_hundredths_mm, settings_.margin_bottom_hundredths_mm};
}

void Application::SaveSettings() {
    settings_.outline_visible = main_window_.document_window().outline_visible();
    settings_.print_landscape = page_setup_.landscape;
    settings_.margin_left_hundredths_mm = page_setup_.left_hundredths_mm;
    settings_.margin_top_hundredths_mm = page_setup_.top_hundredths_mm;
    settings_.margin_right_hundredths_mm = page_setup_.right_hundredths_mm;
    settings_.margin_bottom_hundredths_mm = page_setup_.bottom_hundredths_mm;
    static_cast<void>(settings_store_.save(settings_));
}

ErrorCode Application::PrintDocument() {
    auto& modes = main_window_.document_window().modes();
    const auto prepared = modes.switch_to(editor::ViewMode::render);
    if (prepared != ErrorCode::ok) return prepared;
    return platform::PrintRichEdit(main_window_.handle(), modes.render_view().handle(), page_setup_);
}

ErrorCode Application::PageSetup() {
    if (platform::ShowPageSetupDialog(main_window_.handle(), page_setup_)) SaveSettings();
    return ErrorCode::ok;
}

ErrorCode Application::ExportDocumentDialog() {
    const TASKDIALOG_BUTTON choices[]{{100, L"仅大纲 — PDF"}, {101, L"仅大纲 — Word (DOCX)"},
        {102, L"仅大纲 — 纯文本 (TXT)"}, {110, L"全文 — PDF"},
        {111, L"全文 — Word (DOCX)"}, {112, L"全文 — 纯文本 (TXT)"},
        {113, L"全文 — 离线 HTML"}};
    TASKDIALOGCONFIG options{sizeof(options)}; options.hwndParent=main_window_.handle();
    options.dwFlags=TDF_USE_COMMAND_LINKS; options.pszWindowTitle=L"导出文档";
    options.pszMainInstruction=L"先选择内容范围，再选择导出格式";
    options.pszContent=L"仅大纲只包含 H1～H6；全文不重复插入目录。";
    options.pButtons=choices;options.cButtons=static_cast<UINT>(std::size(choices));
    const auto default_base=settings_.export_scope==services::ExportScopeSetting::outline?100:110;
    const auto default_offset=settings_.export_format==services::ExportFormatSetting::pdf?0:settings_.export_format==services::ExportFormatSetting::docx?1:settings_.export_format==services::ExportFormatSetting::txt?2:3;
    options.nDefaultButton=(default_base==100&&default_offset==3)?110:default_base+default_offset;options.dwCommonButtons=TDCBF_CANCEL_BUTTON;int choice{};
    if(FAILED(TaskDialogIndirect(&options,&choice,nullptr,nullptr))||choice==IDCANCEL)return ErrorCode::ok;
    const auto scope=choice<110?exporting::ExportScope::outline:exporting::ExportScope::full;
    const auto format=(choice%10)==0?exporting::ExportFormat::pdf:(choice%10)==1?exporting::ExportFormat::docx:(choice%10)==2?exporting::ExportFormat::txt:exporting::ExportFormat::html;
    settings_.export_scope=scope==exporting::ExportScope::outline?services::ExportScopeSetting::outline:services::ExportScopeSetting::full;
    settings_.export_format=format==exporting::ExportFormat::pdf?services::ExportFormatSetting::pdf:format==exporting::ExportFormat::docx?services::ExportFormatSetting::docx:format==exporting::ExportFormat::txt?services::ExportFormatSetting::txt:services::ExportFormatSetting::html;SaveSettings();
    const auto snapshot=session_.snapshot();const auto built=exporting::BuildExportDocument(snapshot,snapshot.source_revision,scope,format,
        {main_window_.document_window().is_named()?main_window_.document_window().path():std::filesystem::path{}});
    if(!built.is_ok())return built.error();
    std::array<wchar_t,32768> path{};const auto stem=main_window_.document_window().is_named()?main_window_.document_window().path().stem().wstring():L"无标题";
    const wchar_t* extension=format==exporting::ExportFormat::pdf?L"pdf":format==exporting::ExportFormat::docx?L"docx":format==exporting::ExportFormat::txt?L"txt":L"html";
    const auto initial=stem+L"."+extension;wcsncpy_s(path.data(),path.size(),initial.c_str(),_TRUNCATE);
    const wchar_t* filter=format==exporting::ExportFormat::pdf?L"PDF 文档 (*.pdf)\0*.pdf\0\0":format==exporting::ExportFormat::docx?L"Word 文档 (*.docx)\0*.docx\0\0":format==exporting::ExportFormat::txt?L"纯文本 (*.txt)\0*.txt\0\0":L"离线 HTML (*.html)\0*.html\0\0";
    OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=main_window_.handle();dialog.lpstrFilter=filter;dialog.lpstrFile=path.data();dialog.nMaxFile=static_cast<DWORD>(path.size());dialog.lpstrDefExt=extension;dialog.Flags=OFN_PATHMUSTEXIST|OFN_OVERWRITEPROMPT|OFN_NOCHANGEDIR;
    if(!GetSaveFileNameW(&dialog))return ErrorCode::ok;
    ExportRun run{std::move(built).value(),format,path.data()};TASKDIALOGCONFIG progress{sizeof(progress)};progress.hwndParent=main_window_.handle();progress.dwFlags=TDF_SHOW_PROGRESS_BAR|TDF_CALLBACK_TIMER;progress.dwCommonButtons=TDCBF_CANCEL_BUTTON;progress.pszWindowTitle=L"导出文档";progress.pszMainInstruction=L"正在生成并验证导出文件…";progress.pfCallback=ExportProgressCallback;progress.lpCallbackData=reinterpret_cast<LONG_PTR>(&run);int ignored{};TaskDialogIndirect(&progress,&ignored,nullptr,nullptr);if(run.worker.joinable())run.worker.join();
    if(run.result==ErrorCode::ok)MessageBoxW(main_window_.handle(),L"导出已完成。",L"马冬梅",MB_OK|MB_ICONINFORMATION);
    else if(run.result==ErrorCode::export_cancelled)MessageBoxW(main_window_.handle(),L"导出已取消，原目标文件未被覆盖。",L"马冬梅",MB_OK|MB_ICONINFORMATION);
    return run.result;
}

ErrorCode Application::InsertDocumentDialog() {
    auto& document = main_window_.document_window();
    auto& modes = document.modes();
    const auto selection = modes.synchronized_source_selection();
    if (!selection.is_ok()) return selection.error();
    const auto snapshot = session_.snapshot();
    const auto position = services::SourcePositionMapper::MapCaret(snapshot,
        snapshot.source_revision, selection.value().anchor, selection.value().caret);
    if (!position.is_ok()) {
        MessageBoxW(main_window_.handle(), L"请先取消文本选择，将光标放在要插入的位置。",
            L"插入文档", MB_OK | MB_ICONINFORMATION);
        return position.error();
    }
    std::array<wchar_t, 32768> path{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = main_window_.handle();
    dialog.lpstrFilter = L"支持的文档 (*.md;*.markdown;*.txt)\0*.md;*.markdown;*.txt\0Markdown 文档 (*.md;*.markdown)\0*.md;*.markdown\0纯文本 (*.txt)\0*.txt\0\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY |
                   OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return ErrorCode::ok;
    const auto plan = services::DocumentCombineService::Prepare({snapshot,
        document.is_named() ? document.path() : std::filesystem::path{}, path.data(),
        position.value().utf8_byte});
    if (!plan.is_ok()) {
        MessageBoxW(main_window_.handle(),
            plan.error() == ErrorCode::insert_target_unsaved
                ? L"要插入的 Markdown 包含本地资源，请先保存当前文档，以确定资源目录。"
                : L"无法读取或插入该文档，请确认文件是可读的 MD、MARKDOWN 或 TXT 文档。",
            L"插入文档", MB_OK | MB_ICONWARNING);
        return plan.error();
    }
    const auto committed = services::DocumentCombineService::Commit(
        session_, plan.value(), next_service_transaction_++);
    if (!committed.is_ok()) {
        MessageBoxW(main_window_.handle(), L"插入未完成，当前文档和资源文件已尽可能保持原状。",
            L"插入文档", MB_OK | MB_ICONWARNING);
        return committed.error();
    }
    const auto refreshed = modes.refresh_after_session_edit();
    main_window_.refresh_document_chrome();
    if (refreshed != ErrorCode::ok) return refreshed;
    MessageBoxW(main_window_.handle(), L"文档已插入到当前光标位置。",
        L"插入文档", MB_OK | MB_ICONINFORMATION);
    return ErrorCode::ok;
}

ErrorCode Application::SplitDocumentDialog() {
    auto& document = main_window_.document_window();
    if (!document.is_named()) {
        MessageBoxW(main_window_.handle(), L"请先保存当前文档，再进行切分。",
            L"切分文档", MB_OK | MB_ICONINFORMATION);
        return ErrorCode::document_invalid_state;
    }
    auto& modes = document.modes();
    const auto selection = modes.synchronized_source_selection();
    if (!selection.is_ok()) return selection.error();
    const auto snapshot = session_.snapshot();
    const auto position = services::SourcePositionMapper::MapCaret(snapshot,
        snapshot.source_revision, selection.value().anchor, selection.value().caret);
    if (!position.is_ok()) {
        MessageBoxW(main_window_.handle(), L"请先取消文本选择，将光标放在要切分的位置。",
            L"切分文档", MB_OK | MB_ICONINFORMATION);
        return position.error();
    }
    auto split_line_ending = document.line_ending();
    if (split_line_ending == fileio::LineEnding::mixed) {
        const auto choice = MessageBoxW(main_window_.handle(),
            L"当前文档同时包含 Windows 和 Unix 换行。\n\n选择“是”使两个结果使用 CRLF，选择“否”使用 LF。\n当前文档不会被修改。",
            L"选择切分结果的换行方式", MB_YESNOCANCEL | MB_ICONQUESTION |
            (PreferredMixedEnding(snapshot.source) == fileio::LineEnding::crlf
                ? MB_DEFBUTTON1 : MB_DEFBUTTON2));
        if (choice == IDCANCEL) return ErrorCode::ok;
        split_line_ending = choice == IDYES ? fileio::LineEnding::crlf
                                             : fileio::LineEnding::lf;
    }
    const auto extension = document.path().extension().wstring();
    const auto folder = document.path().parent_path();
    const auto stem = document.path().stem().wstring();
    auto choose_target = [&](const wchar_t* suffix,
                             const std::filesystem::path& other = {})
        -> std::filesystem::path {
        std::array<wchar_t, 32768> path{};
        const auto initial = (folder / (stem + suffix + extension)).wstring();
        wcsncpy_s(path.data(), path.size(), initial.c_str(), _TRUNCATE);
        const wchar_t* filter = session_.kind() == document::DocumentKind::plain_text
            ? L"纯文本 (*.txt)\0*.txt\0\0"
            : extension == L".markdown" || extension == L".MARKDOWN"
                ? L"Markdown 文档 (*.markdown)\0*.markdown\0\0"
                : L"Markdown 文档 (*.md)\0*.md\0\0";
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = main_window_.handle();
        dialog.lpstrFilter = filter; dialog.lpstrFile = path.data();
        dialog.nMaxFile = static_cast<DWORD>(path.size());
        dialog.lpstrDefExt = extension.c_str() + 1;
        dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&dialog)) return {};
        const std::filesystem::path selected(path.data());
        std::error_code ignored;
        if (std::filesystem::exists(selected, ignored) ||
            (!other.empty() && _wcsicmp(selected.c_str(), other.c_str()) == 0)) {
            MessageBoxW(main_window_.handle(), L"切分目标必须是两个不同且尚不存在的新文件。",
                L"切分文档", MB_OK | MB_ICONWARNING);
            return {};
        }
        return selected;
    };
    const auto first = choose_target(L"_前半部分");
    if (first.empty()) return ErrorCode::ok;
    const auto second = choose_target(L"_后半部分", first);
    if (second.empty()) return ErrorCode::ok;
    const auto plan = services::DocumentSplitService::Prepare({snapshot, document.path(),
        first, second, position.value().utf8_byte, document.encoding(),
        split_line_ending});
    if (!plan.is_ok()) {
        MessageBoxW(main_window_.handle(), L"切分目标必须与当前文档扩展名相同，且两个目标都尚未存在。",
            L"切分文档", MB_OK | MB_ICONWARNING);
        return plan.error();
    }
    bool confirmed = false;
    if (plan.value().requires_confirmation()) {
        confirmed = MessageBoxW(main_window_.handle(),
            L"切分后的某一部分可能不是完整的 Markdown 结构，是否仍要继续？",
            L"切分文档", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
        if (!confirmed) return ErrorCode::ok;
    }
    const auto committed = services::DocumentSplitService::Commit(plan.value(), confirmed);
    if (committed != ErrorCode::ok) {
        MessageBoxW(main_window_.handle(), L"切分未完成，两个目标文件已尽可能回滚。",
            L"切分文档", MB_OK | MB_ICONWARNING);
        return committed;
    }
    const auto message = L"切分已完成：\n" + first.wstring() + L"\n" + second.wstring();
    MessageBoxW(main_window_.handle(), message.c_str(), L"切分文档",
        MB_OK | MB_ICONINFORMATION);
    return ErrorCode::ok;
}

ui::MainWindow& Application::main_window() noexcept { return main_window_; }

void Application::enqueue_open_paths(std::vector<std::filesystem::path> paths) {
    {
        std::lock_guard lock(incoming_mutex_);
        if (paths.empty()) activate_requested_ = true;
        incoming_paths_.insert(incoming_paths_.end(),
            std::make_move_iterator(paths.begin()), std::make_move_iterator(paths.end()));
    }
    main_window_.notify_open_requests();
}

bool Application::ConfirmDocumentReplacement() {
    if (!session_.is_dirty()) return true;
    const auto choice = ConfirmUnsavedChanges(main_window_.handle());
    if (choice == IDCANCEL) return false;
    if (choice == IDNO) return true;
    const auto result = SaveDocument();
    if (result != ErrorCode::ok) { ShowFileError(result); return false; }
    return !session_.is_dirty();
}

bool Application::ConfirmClose() {
    if (placement_exit_requested_) return true;
    const auto was_processing = processing_open_request_;
    processing_open_request_ = true;
    const bool document_ready = ConfirmDocumentReplacement();
    processing_open_request_ = was_processing;
    if (!document_ready) return false;
    if (pending_paths_.empty()) return true;
    const auto message = L"还有 " + std::to_wstring(pending_paths_.size()) +
        L" 个文件等待打开。退出将取消这些请求，是否仍要退出？";
    return MessageBoxW(main_window_.handle(), message.c_str(), L"马冬梅",
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}

ErrorCode Application::NewDocument() {
    if (!ConfirmDocumentReplacement()) return ErrorCode::ok;
    const auto result = main_window_.document_window().new_document();
    if (result == ErrorCode::ok) main_window_.refresh_document_chrome();
    return result;
}

ErrorCode Application::OpenDocumentDialog() {
    if (!ConfirmDocumentReplacement()) return ErrorCode::ok;
    std::array<wchar_t, 32768> path{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = main_window_.handle();
    dialog.lpstrFilter = L"支持的文档 (*.md;*.markdown;*.txt)\0*.md;*.markdown;*.txt\0Markdown 文档 (*.md;*.markdown)\0*.md;*.markdown\0纯文本 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return ErrorCode::ok;
    return OpenPath(path.data());
}

ErrorCode Application::SaveDocument() {
    if (!PrepareLineEndingForSave()) return ErrorCode::ok;
    auto& document = main_window_.document_window();
    if (!document.is_named()) return SaveDocumentAs();
    if (document.is_read_only()) {
        MessageBoxW(main_window_.handle(), L"该文件是只读文件，请另存为一个新文件。",
            L"马冬梅", MB_OK | MB_ICONINFORMATION);
        return SaveDocumentAs();
    }
    if (document.has_external_change()) {
        const auto choice = MessageBoxW(main_window_.handle(),
            L"该文件已被其他程序修改。继续保存将覆盖外部修改。是否继续？",
            L"检测到外部修改", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
        if (choice != IDYES) return ErrorCode::ok;
    }
    const auto result = document.save_document();
    if (result == ErrorCode::ok) {
        main_window_.refresh_document_chrome();
        RememberRecentFile(document.path());
        ProcessNextOpenRequest();
    }
    return result;
}

ErrorCode Application::SaveDocumentAs() {
    if (!PrepareLineEndingForSave()) return ErrorCode::ok;
    std::array<wchar_t, 32768> path{};
    auto& document = main_window_.document_window();
    const auto initial = document.is_named() ? document.path().wstring() :
        std::wstring(session_.kind() == document::DocumentKind::plain_text ? L"无标题.txt" : L"无标题.md");
    wcsncpy_s(path.data(), path.size(), initial.c_str(), _TRUNCATE);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = main_window_.handle();
    dialog.lpstrFilter = L"Markdown 文档 (*.md;*.markdown)\0*.md;*.markdown\0纯文本 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrDefExt = session_.kind() == document::DocumentKind::plain_text ? L"txt" : L"md";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) return ErrorCode::ok;
    const auto result = document.save_document_as(path.data());
    if (result == ErrorCode::ok) {
        main_window_.refresh_document_chrome();
        RememberRecentFile(document.path());
        ProcessNextOpenRequest();
    }
    return result;
}

bool Application::PrepareLineEndingForSave() {
    auto& document = main_window_.document_window();
    if (document.line_ending() != fileio::LineEnding::mixed) return true;
    const auto choice = MessageBoxW(main_window_.handle(),
        L"文档同时包含 Windows 和 Unix 换行。\n\n选择“是”统一为 CRLF，选择“否”统一为 LF。",
        L"选择保存换行方式", MB_YESNOCANCEL | MB_ICONQUESTION |
        (PreferredMixedEnding(session_.snapshot().source) == fileio::LineEnding::crlf
            ? MB_DEFBUTTON1 : MB_DEFBUTTON2));
    if (choice == IDCANCEL) return false;
    document.set_line_ending(choice == IDYES ? fileio::LineEnding::crlf : fileio::LineEnding::lf);
    main_window_.refresh_document_chrome();
    return true;
}

ErrorCode Application::OpenPath(const std::filesystem::path& path) {
    const auto result = main_window_.document_window().open_document(path);
    if (result == ErrorCode::ok) {
        main_window_.refresh_document_chrome();
        RememberRecentFile(main_window_.document_window().path());
    }
    return result;
}

void Application::RememberRecentFile(const std::filesystem::path& path) {
    if (!path.empty() && recent_files_.touch(path) == ErrorCode::ok) RefreshRecentFiles();
}

ErrorCode Application::OpenRecentFile(std::size_t index) {
    if (index >= recent_file_list_.size()) return ErrorCode::file_not_found;
    if (!ConfirmDocumentReplacement()) return ErrorCode::ok;
    const auto result = OpenPath(recent_file_list_[index]);
    if (result == ErrorCode::file_not_found) RefreshRecentFiles();
    return result;
}

ErrorCode Application::ClearRecentFiles() {
    const auto result = recent_files_.clear();
    if (result == ErrorCode::ok) RefreshRecentFiles();
    return result;
}

void Application::RefreshRecentFiles() {
    const auto loaded = recent_files_.load();
    recent_file_list_ = loaded.is_ok() ? loaded.value()
                                       : std::vector<std::filesystem::path>{};
    main_window_.set_recent_files(recent_file_list_);
}

void Application::DrainOpenRequests() {
    std::vector<std::filesystem::path> incoming;
    bool activate{};
    {
        std::lock_guard lock(incoming_mutex_);
        incoming.swap(incoming_paths_);
        activate = activate_requested_;
        activate_requested_ = false;
    }
    if (activate && main_window_.handle()) {
        ShowWindow(main_window_.handle(), SW_RESTORE);
        SetForegroundWindow(main_window_.handle());
    }
    for (auto& path : incoming) pending_paths_.push_back(std::move(path));
    main_window_.set_pending_open_count(pending_paths_.size());
    ProcessNextOpenRequest();
}

void Application::ProcessNextOpenRequest() {
    if (processing_open_request_ || pending_paths_.empty()) return;
    processing_open_request_ = true;
    if (ConfirmDocumentReplacement()) {
        auto path = std::move(pending_paths_.front());
        pending_paths_.pop_front();
        main_window_.set_pending_open_count(pending_paths_.size());
        const auto result = OpenPath(path);
        if (result != ErrorCode::ok) ShowFileError(result);
    }
    processing_open_request_ = false;
}

std::filesystem::path Application::ExecutablePath() const {
    std::array<wchar_t, 32768> path{};
    const auto length = GetModuleFileNameW(nullptr, path.data(),
        static_cast<DWORD>(path.size()));
    return length && length < path.size()
        ? std::filesystem::path(path.data()) : std::filesystem::path{};
}

ErrorCode Application::RegisterFileAssociations() {
    const auto executable = ExecutablePath();
    if (executable.empty()) return ErrorCode::platform_association_write_failed;
    const auto result = file_association_.register_application(executable);
    if (result != ErrorCode::ok) return result;
    const auto choice = MessageBoxW(main_window_.handle(),
        L"马冬梅已注册为 Markdown 和 TXT 的候选打开程序。\n\n"
        L"这不会抢占当前默认应用。是否现在打开 Windows 设置，由您亲自选择默认应用？",
        L"文件关联注册完成", MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON1);
    return choice == IDYES ? OpenDefaultApps() : ErrorCode::ok;
}

ErrorCode Application::UnregisterFileAssociations() {
    const auto result = file_association_.unregister_application(ExecutablePath());
    if (result == ErrorCode::ok)
        MessageBoxW(main_window_.handle(), L"马冬梅的 Markdown 和 TXT 候选程序注册已撤销。",
            L"文件关联", MB_OK | MB_ICONINFORMATION);
    return result;
}

ErrorCode Application::OpenDefaultApps() {
    return platform::OpenDefaultAppsSettings(main_window_.handle());
}

ErrorCode Application::PlaceApplication() {
    PlacementDialog dialog;
    const auto folder = dialog.show(main_window_.handle(),
        platform::DefaultPlacementFolder(), main_window_.theme_preference());
    if (!folder) return ErrorCode::ok;
    const auto inspected = placement_service_.inspect(ExecutablePath(), *folder);
    if (!inspected.is_ok()) { ShowPlacementError(inspected.error()); return inspected.error(); }
    const auto& plan = inspected.value();
    if (plan.same_path) {
        const auto repaired = file_association_.register_application(plan.source);
        if (repaired == ErrorCode::ok)
            MessageBoxW(main_window_.handle(),
                L"马冬梅已经在这个位置，无需再次复制。候选文件关联已按当前位置修复。",
                L"安置马冬梅", MB_OK | MB_ICONINFORMATION);
        else ShowPlacementError(repaired);
        return repaired;
    }
    bool replace = plan.target_exists && plan.same_content;
    if (plan.target_exists && !plan.same_content) {
        const auto text = L"目标文件夹中已经存在 MarkdownMay.exe。\n\n是否用当前版本替换它？\n原位置的程序不会被删除。";
        if (MessageBoxW(main_window_.handle(), text, L"确认替换",
                MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            return ErrorCode::ok;
        replace = true;
    }
    if (!ConfirmDocumentReplacement()) return ErrorCode::ok;
    const auto placed = placement_service_.place(plan, replace);
    if (placed != ErrorCode::ok) { ShowPlacementError(placed); return placed; }

    auto command = QuoteCommandLineArgument(plan.target.wstring()) +
        L" --wait-for-process " + std::to_wstring(GetCurrentProcessId()) +
        L" --repair-file-types";
    const auto& document = main_window_.document_window();
    if (document.is_named())
        command += L" " + QuoteCommandLineArgument(document.path().wstring());
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(plan.target.c_str(), mutable_command.data(), nullptr, nullptr,
            FALSE, 0, nullptr, plan.folder.c_str(), &startup, &process)) {
        ShowPlacementError(ErrorCode::platform_placement_launch_failed);
        return ErrorCode::platform_placement_launch_failed;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    placement_exit_requested_ = true;
    PostMessageW(main_window_.handle(), WM_CLOSE, 0, 0);
    return ErrorCode::ok;
}

void Application::ShowPlacementError(ErrorCode error) {
    const wchar_t* message = L"无法安置马冬梅，请检查目标文件夹后重试。";
    if (error == ErrorCode::platform_placement_invalid_target)
        message = L"目标文件夹无效或无法访问，请选择其他位置。";
    else if (error == ErrorCode::platform_placement_copy_failed)
        message = L"复制或提交程序失败，旧位置的马冬梅仍在运行。";
    else if (error == ErrorCode::platform_placement_verify_failed)
        message = L"复制后的程序校验失败，未切换到新位置。";
    else if (error == ErrorCode::platform_placement_target_changed)
        message = L"目标程序在确认后发生变化，为避免覆盖已取消安置。";
    else if (error == ErrorCode::platform_placement_launch_failed)
        message = L"程序已经复制，但无法从新位置启动；当前马冬梅将继续运行。";
    else if (error == ErrorCode::platform_registry_access_denied ||
             error == ErrorCode::platform_association_write_failed)
        message = L"无法按当前位置修复候选文件关联。";
    MessageBoxW(main_window_.handle(), message, L"安置马冬梅",
        MB_OK | MB_ICONERROR);
}

void Application::CheckExternalModification() {
    if (checking_external_) return;
    auto& document = main_window_.document_window();
    if (!document.is_named() || !document.has_external_change()) return;
    checking_external_ = true;
    const wchar_t* text = session_.is_dirty()
        ? L"磁盘上的文件已被其他程序修改。重新加载会丢失当前未保存内容。是否重新加载？"
        : L"磁盘上的文件已被其他程序修改。是否重新加载最新内容？";
    const auto choice = MessageBoxW(main_window_.handle(), text,
        L"检测到外部修改", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON1);
    if (choice == IDYES) {
        const auto result = document.reload_document();
        if (result != ErrorCode::ok) ShowFileError(result);
        else main_window_.refresh_document_chrome();
    } else {
        document.acknowledge_external_change();
    }
    checking_external_ = false;
}

void Application::ShowFileError(ErrorCode error) {
    const wchar_t* message = L"无法完成文件操作。";
    if (error == ErrorCode::file_not_found) message = L"找不到指定的文件。";
    else if (error == ErrorCode::file_encoding_unsupported || error == ErrorCode::file_encoding_invalid)
        message = L"文件编码不受支持，未打开文件。";
    else if (error == ErrorCode::file_write_failed)
        message = L"无法保存文件，原文件未被覆盖。";
    else if (error == ErrorCode::file_read_only)
        message = L"该文件是只读文件，请另存为一个新文件。";
    MessageBoxW(main_window_.handle(), message, L"马冬梅", MB_OK | MB_ICONERROR);
}

}  // namespace markdownmay::app
