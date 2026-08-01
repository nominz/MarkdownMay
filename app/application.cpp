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
}

Application::Application(HINSTANCE instance)
    : instance_(instance),
      dispatcher_(main_window_.document_window(), [this] {
          if (main_window_.handle())
              PostMessageW(main_window_.handle(), WM_CLOSE, 0, 0);
      }, {
          [this] { return CanReplaceDocument(); },
          [this] { return NewDocument(); },
          [this] { return OpenDocumentDialog(); },
          [this] { return SaveDocument(); },
          [this] { return SaveDocumentAs(); },
      }) {
    main_window_.set_command_callbacks(
        [this](CommandId command) { return dispatcher_.query(command); },
        [this](CommandId command) {
            const auto result = dispatcher_.execute(command);
            if (result != ErrorCode::ok && main_window_.handle()) {
                if (command >= CommandId::file_new &&
                    command <= CommandId::file_save_as) {
                    ShowFileError(result);
                } else {
                    MessageBoxW(main_window_.handle(),
                        L"当前操作无法完成，请检查文档内容后重试。",
                        L"马冬梅", MB_OK | MB_ICONWARNING);
                }
            }
        });
    main_window_.set_drop_callback([this](const std::filesystem::path& path) {
        if (!CanReplaceDocument()) {
            MessageBoxW(main_window_.handle(),
                L"当前文档尚未保存，暂时不能打开另一个文件。",
                L"马冬梅", MB_OK | MB_ICONWARNING);
            return;
        }
        const auto result = OpenPath(path);
        if (result != ErrorCode::ok) ShowFileError(result);
    });
}

int Application::run(int show_command) {
    if (main_window_.create(instance_, show_command) != ErrorCode::ok) return 1;
    MSG message{};
    int result{};
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (main_window_.accelerator() && TranslateAcceleratorW(
                main_window_.handle(), main_window_.accelerator(), &message))
            continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return result < 0 ? 2 : static_cast<int>(message.wParam);
}

ui::MainWindow& Application::main_window() noexcept { return main_window_; }

bool Application::CanReplaceDocument() const noexcept { return !session_.is_dirty(); }

ErrorCode Application::NewDocument() {
    const auto result = main_window_.document_window().new_document();
    if (result == ErrorCode::ok) main_window_.refresh_document_chrome();
    return result;
}

ErrorCode Application::OpenDocumentDialog() {
    std::array<wchar_t, 32768> path{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = main_window_.handle();
    dialog.lpstrFilter = L"Markdown 文档 (*.md;*.markdown)\0*.md;*.markdown\0所有文件 (*.*)\0*.*\0\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
        OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return ErrorCode::ok;
    return OpenPath(path.data());
}

ErrorCode Application::SaveDocument() {
    if (!PrepareLineEndingForSave()) return ErrorCode::ok;
    if (!main_window_.document_window().is_named()) return SaveDocumentAs();
    const auto result = main_window_.document_window().save_document();
    if (result == ErrorCode::ok) main_window_.refresh_document_chrome();
    return result;
}

ErrorCode Application::SaveDocumentAs() {
    if (!PrepareLineEndingForSave()) return ErrorCode::ok;
    std::array<wchar_t, 32768> path{};
    if (main_window_.document_window().is_named()) {
        const auto current = main_window_.document_window().path().wstring();
        wcsncpy_s(path.data(), path.size(), current.c_str(), _TRUNCATE);
    } else {
        wcsncpy_s(path.data(), path.size(), L"无标题.md", _TRUNCATE);
    }
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = main_window_.handle();
    dialog.lpstrFilter = L"Markdown 文档 (*.md;*.markdown)\0*.md;*.markdown\0所有文件 (*.*)\0*.*\0\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrDefExt = L"md";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) return ErrorCode::ok;
    const auto result = main_window_.document_window().save_document_as(path.data());
    if (result == ErrorCode::ok) main_window_.refresh_document_chrome();
    return result;
}

bool Application::PrepareLineEndingForSave() {
    auto& document = main_window_.document_window();
    if (document.line_ending() != fileio::LineEnding::mixed) return true;
    const auto choice = MessageBoxW(main_window_.handle(),
        L"文档同时包含 Windows 和 Unix 换行。\n\n"
        L"选择“是”统一为 CRLF，选择“否”统一为 LF。",
        L"选择保存换行方式", MB_YESNOCANCEL | MB_ICONQUESTION |
        (PreferredMixedEnding(session_.snapshot().source) == fileio::LineEnding::crlf
            ? MB_DEFBUTTON1 : MB_DEFBUTTON2));
    if (choice == IDCANCEL) return false;
    document.set_line_ending(choice == IDYES
        ? fileio::LineEnding::crlf : fileio::LineEnding::lf);
    main_window_.refresh_document_chrome();
    return true;
}

ErrorCode Application::OpenPath(const std::filesystem::path& path) {
    const auto result = main_window_.document_window().open_document(path);
    if (result == ErrorCode::ok) main_window_.refresh_document_chrome();
    return result;
}

void Application::ShowFileError(ErrorCode error) {
    const wchar_t* message = L"无法完成文件操作。";
    if (error == ErrorCode::file_not_found) message = L"找不到指定的文件。";
    else if (error == ErrorCode::file_encoding_unsupported ||
             error == ErrorCode::file_encoding_invalid)
        message = L"文件编码不受支持，未打开文件。";
    else if (error == ErrorCode::file_write_failed)
        message = L"无法保存文件，原文件未被覆盖。";
    MessageBoxW(main_window_.handle(), message, L"马冬梅",
        MB_OK | MB_ICONERROR);
}

}  // namespace markdownmay::app
