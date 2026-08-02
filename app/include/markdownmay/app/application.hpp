#pragma once

#include "markdownmay/document/document_session.hpp"
#include "markdownmay/app/command_dispatcher.hpp"
#include "markdownmay/ui/main_window.hpp"
#include "markdownmay/services/local_services.hpp"

#include <windows.h>

#include <filesystem>

namespace markdownmay::app {

class Application final {
public:
    explicit Application(HINSTANCE instance);
    [[nodiscard]] int run(int show_command);
    [[nodiscard]] ui::MainWindow& main_window() noexcept;

private:
    [[nodiscard]] bool ConfirmDocumentReplacement();
    [[nodiscard]] bool ConfirmClose();
    void CheckExternalModification();
    void RememberRecentFile(const std::filesystem::path& path);
    [[nodiscard]] ErrorCode OpenRecentFile(std::size_t index);
    [[nodiscard]] ErrorCode ClearRecentFiles();
    void RefreshRecentFiles();
    [[nodiscard]] ErrorCode NewDocument();
    [[nodiscard]] ErrorCode OpenDocumentDialog();
    [[nodiscard]] ErrorCode SaveDocument();
    [[nodiscard]] ErrorCode SaveDocumentAs();
    [[nodiscard]] ErrorCode OpenPath(const std::filesystem::path& path);
    [[nodiscard]] bool PrepareLineEndingForSave();
    void ShowFileError(ErrorCode error);

    HINSTANCE instance_{};
    document::DocumentSession session_{""};
    ui::MainWindow main_window_{session_};
    CommandDispatcher dispatcher_;
    services::RecentFilesStore recent_files_;
    bool checking_external_{};
    std::vector<std::filesystem::path> recent_file_list_;
};

}  // namespace markdownmay::app
