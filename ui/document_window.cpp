#include "markdownmay/ui/document_window.hpp"

#include <windows.h>

namespace markdownmay::ui {

DocumentWindow::DocumentWindow(document::DocumentSession& session) : modes_(session) {}

ErrorCode DocumentWindow::create(HWND parent, const RECT& bounds) {
    return modes_.create(parent, bounds);
}

void DocumentWindow::resize(const RECT& bounds) {
    if (!modes_.handle()) return;
    MoveWindow(modes_.handle(), bounds.left, bounds.top,
        bounds.right - bounds.left, bounds.bottom - bounds.top, TRUE);
}

HWND DocumentWindow::handle() const noexcept { return modes_.handle(); }
editor::ViewModeController& DocumentWindow::modes() noexcept { return modes_; }

ErrorCode DocumentWindow::new_document() {
    const auto result = modes_.reload("");
    if (result != ErrorCode::ok) return result;
    path_.clear();
    encoding_ = fileio::TextEncoding::utf8;
    line_ending_ = fileio::LineEnding::crlf;
    read_only_ = false;
    disk_source_.clear();
    modes_.set_document_path({});
    return ErrorCode::ok;
}

ErrorCode DocumentWindow::open_document(const std::filesystem::path& path) {
    const auto loaded = fileio::LoadTextFile(path);
    if (!loaded.is_ok()) return loaded.error();
    const auto result = modes_.reload(loaded.value().source);
    if (result != ErrorCode::ok) return result;
    path_ = loaded.value().path;
    encoding_ = loaded.value().encoding;
    line_ending_ = loaded.value().line_ending;
    const auto attributes = GetFileAttributesW(path_.c_str());
    read_only_ = attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_READONLY) != 0;
    disk_source_ = loaded.value().source;
    modes_.set_document_path(path_);
    return ErrorCode::ok;
}

ErrorCode DocumentWindow::save_document() {
    if (path_.empty()) return ErrorCode::document_invalid_state;
    if (read_only_) return ErrorCode::file_read_only;
    const auto result = modes_.save(path_, encoding_, line_ending_);
    if (result == ErrorCode::ok) acknowledge_external_change();
    return result;
}

ErrorCode DocumentWindow::save_document_as(const std::filesystem::path& path) {
    const auto result = modes_.save(path, encoding_, line_ending_);
    if (result != ErrorCode::ok) return result;
    path_ = std::filesystem::absolute(path).lexically_normal();
    read_only_ = false;
    acknowledge_external_change();
    modes_.set_document_path(path_);
    return ErrorCode::ok;
}

ErrorCode DocumentWindow::reload_document() {
    if (path_.empty()) return ErrorCode::document_invalid_state;
    return open_document(path_);
}

bool DocumentWindow::is_named() const noexcept { return !path_.empty(); }
bool DocumentWindow::is_read_only() const noexcept { return read_only_; }
bool DocumentWindow::has_external_change() const {
    if (path_.empty()) return false;
    const auto loaded = fileio::LoadTextFile(path_);
    return !loaded.is_ok() || loaded.value().source != disk_source_;
}
void DocumentWindow::acknowledge_external_change() {
    if (path_.empty()) { disk_source_.clear(); return; }
    const auto loaded = fileio::LoadTextFile(path_);
    if (loaded.is_ok()) disk_source_ = loaded.value().source;
}
const std::filesystem::path& DocumentWindow::path() const noexcept { return path_; }
fileio::TextEncoding DocumentWindow::encoding() const noexcept { return encoding_; }
fileio::LineEnding DocumentWindow::line_ending() const noexcept { return line_ending_; }
void DocumentWindow::set_line_ending(fileio::LineEnding line_ending) noexcept {
    line_ending_ = line_ending;
}
void DocumentWindow::apply_appearance(COLORREF text, COLORREF background,
                                      COLORREF accent, UINT dpi) {
    modes_.apply_appearance(text, background, accent, dpi);
}

}  // namespace markdownmay::ui
