#include "markdownmay/app/application.hpp"

#include <commdlg.h>
#include <commctrl.h>

#include <array>
#include <cwchar>
#include <string_view>

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
    const TASKDIALOG_BUTTON buttons[]{
        {IDYES, L"保存"}, {IDNO, L"不保存"}, {IDCANCEL, L"取消"}};
    TASKDIALOGCONFIG dialog{};
    dialog.cbSize = sizeof(dialog);
    dialog.hwndParent = owner;
    dialog.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
    dialog.pszWindowTitle = L"马冬梅";
    dialog.pszMainIcon = TD_WARNING_ICON;
    dialog.pszMainInstruction = L"是否保存对此文档的更改？";
    dialog.pszContent = L"如果不保存，最近所做的更改将会丢失。";
    dialog.pButtons = buttons;
    dialog.cButtons = static_cast<UINT>(std::size(buttons));
    dialog.nDefaultButton = IDYES;
    int selected = IDCANCEL;
    using TaskDialogIndirectFunction = HRESULT (WINAPI*)(
        const TASKDIALOGCONFIG*, int*, int*, BOOL*);
    const auto common_controls = GetModuleHandleW(L"comctl32.dll");
    const auto task_dialog = common_controls
        ? reinterpret_cast<TaskDialogIndirectFunction>(
            GetProcAddress(common_controls, "TaskDialogIndirect"))
        : nullptr;
    if (task_dialog && SUCCEEDED(task_dialog(&dialog, &selected, nullptr, nullptr)))
        return selected;
    return MessageBoxW(owner,
        L"当前文档有尚未保存的修改。是否先保存？",
        L"马冬梅", MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON1);
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
          [this](std::size_t index) { return OpenRecentFile(index); },
          [this] { return ClearRecentFiles(); },
      }, {
          [this] { return file_association_.state(ExecutablePath()) !=
              platform::AssociationState::current; },
          [this] { return file_association_.state(ExecutablePath()) !=
              platform::AssociationState::not_registered; },
          [this] { return RegisterFileAssociations(); },
          [this] { return UnregisterFileAssociations(); },
          [this] { return OpenDefaultApps(); },
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
    dialog.lpstrFilter = L"Markdown 文档 (*.md;*.markdown)\0*.md;*.markdown\0所有文件 (*.*)\0*.*\0\0";
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
    const auto initial = document.is_named() ? document.path().wstring() : std::wstring(L"无标题.md");
    wcsncpy_s(path.data(), path.size(), initial.c_str(), _TRUNCATE);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = main_window_.handle();
    dialog.lpstrFilter = L"Markdown 文档 (*.md;*.markdown)\0*.md;*.markdown\0所有文件 (*.*)\0*.*\0\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrDefExt = L"md";
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
        L"马冬梅已注册为 Markdown 候选程序。\n\n"
        L"Windows 仍需要您亲自选择默认应用。现在打开系统设置吗？",
        L"文件关联注册完成", MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON1);
    return choice == IDYES ? OpenDefaultApps() : ErrorCode::ok;
}

ErrorCode Application::UnregisterFileAssociations() {
    const auto result = file_association_.unregister_application();
    if (result == ErrorCode::ok)
        MessageBoxW(main_window_.handle(), L"马冬梅的 Markdown 候选程序注册已撤销。",
            L"文件关联", MB_OK | MB_ICONINFORMATION);
    return result;
}

ErrorCode Application::OpenDefaultApps() {
    return platform::OpenDefaultAppsSettings(main_window_.handle());
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
