#pragma once

#include "markdownmay/editor/source_sync.hpp"
#include "markdownmay/editor/paragraph_editor.hpp"

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <functional>

namespace markdownmay::editor {

class SourceView final {
public:
    explicit SourceView(document::DocumentSession& session);
    ~SourceView();
    SourceView(const SourceView&) = delete;
    SourceView& operator=(const SourceView&) = delete;

    [[nodiscard]] ErrorCode create(HWND parent, const RECT& bounds);
    [[nodiscard]] ErrorCode project();
    [[nodiscard]] ErrorCode synchronize_now();
    [[nodiscard]] ErrorCode save(const std::filesystem::path& target,
        fileio::TextEncoding encoding, fileio::LineEnding line_ending);
    [[nodiscard]] ErrorCode go_to_first_error();
    [[nodiscard]] ErrorCode last_error() const noexcept;
    [[nodiscard]] const std::vector<SourceDiagnostic>& diagnostics() const noexcept;
    [[nodiscard]] HWND handle() const noexcept;
    [[nodiscard]] HWND host_handle() const noexcept;
    [[nodiscard]] TextSelection source_selection() const noexcept;
    [[nodiscard]] ErrorCode select_source_range(TextSelection selection);
    void set_synchronized_callback(std::function<void(ErrorCode)> callback);
    void set_scroll_callback(std::function<void(std::uint64_t, std::uint64_t)> callback);
    static LRESULT CALLBACK HostProcedure(HWND window, UINT message,
                                           WPARAM w_param, LPARAM l_param);

private:
    void Configure();
    void ApplyStyles();
    void ApplyDiagnostics();
    void ScheduleSynchronize();
    [[nodiscard]] std::string ReadSource() const;

    document::DocumentSession& session_;
    SourceSync sync_;
    HWND host_{};
    HWND editor_{};
    bool projecting_{};
    std::uint64_t pending_since_{};
    ErrorCode last_error_{ErrorCode::ok};
    std::function<void(ErrorCode)> synchronized_callback_;
    std::function<void(std::uint64_t, std::uint64_t)> scroll_callback_;
};

}  // namespace markdownmay::editor
