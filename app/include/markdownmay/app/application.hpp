#pragma once

#include "markdownmay/document/document_session.hpp"
#include "markdownmay/app/command_dispatcher.hpp"
#include "markdownmay/ui/main_window.hpp"
#include "markdownmay/services/local_services.hpp"
#include "markdownmay/platform/file_association.hpp"
#include "markdownmay/platform/printing.hpp"

#include <windows.h>

#include <filesystem>
#include <deque>
#include <mutex>
#include <vector>

namespace markdownmay::app {

class Application final {
public:
    explicit Application(HINSTANCE instance);
    [[nodiscard]] int run(int show_command);
    [[nodiscard]] ui::MainWindow& main_window() noexcept;
    void enqueue_open_paths(std::vector<std::filesystem::path> paths);

private:
    [[nodiscard]] bool ConfirmDocumentReplacement();
    [[nodiscard]] bool ConfirmClose();
    void CheckExternalModification();
    void RememberRecentFile(const std::filesystem::path& path);
    [[nodiscard]] ErrorCode OpenRecentFile(std::size_t index);
    [[nodiscard]] ErrorCode ClearRecentFiles();
    void RefreshRecentFiles();
    void DrainOpenRequests();
    void ProcessNextOpenRequest();
    [[nodiscard]] ErrorCode RegisterFileAssociations();
    [[nodiscard]] ErrorCode UnregisterFileAssociations();
    [[nodiscard]] ErrorCode OpenDefaultApps();
    [[nodiscard]] ErrorCode PrintDocument();
    [[nodiscard]] ErrorCode PageSetup();
    void LoadSettings();
    void SaveSettings();
    [[nodiscard]] std::filesystem::path ExecutablePath() const;
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
    services::SettingsStore settings_store_;
    services::Settings settings_;
    platform::PageSetup page_setup_;
    bool checking_external_{};
    std::vector<std::filesystem::path> recent_file_list_;
    std::mutex incoming_mutex_;
    std::vector<std::filesystem::path> incoming_paths_;
    bool activate_requested_{};
    std::deque<std::filesystem::path> pending_paths_;
    bool processing_open_request_{};
    platform::FileAssociationRegistry file_association_;
};

}  // namespace markdownmay::app
