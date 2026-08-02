#include "markdownmay/app/application.hpp"

#include <commdlg.h>

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
          [this](std::size_t index) { return OpenRecentFile(index); },
          [this] { return ClearRecentFiles(); },
      }),
      recent_files_(RecentFilePath(), 20) {
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
}

int Application::run(int show_command) {
    if (main_window_.create(instance_, show_command) != ErrorCode::ok) return 1;
    RefreshRecentFiles();
    MSG message{};
    int result{};
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (main_window_.accelerator() && TranslateAcceleratorW(
                main_window_.handle(), main_window_.accelerator(), &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return result < 0 ? 2 : static_cast<int>(message.wParam);
}

ui::MainWindow& Application::main_window() noexcept { return main_window_; }

bool Application::ConfirmDocumentReplacement() {
    if (!session_.is_dirty()) return true;
    const auto choice = MessageBoxW(main_window_.handle(),
        L"当前文档有尚未保存的修改。是否先保存？\n\n"
        L"选择“是”保存，选择“否”放弃修改，选择“取消”继续编辑。",
        L"马冬梅", MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON1);
    if (choice == IDCANCEL) return false;
    if (choice == IDNO) return true;
    const auto result = SaveDocument();
    if (result != ErrorCode::ok) { ShowFileError(result); return false; }
    return !session_.is_dirty();
}

bool Application::ConfirmClose() { return ConfirmDocumentReplacement(); }

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
