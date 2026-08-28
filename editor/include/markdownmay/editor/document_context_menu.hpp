#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>

namespace markdownmay::editor {

enum class DocumentContextCommand : std::uint8_t {
    undo, redo, cut, copy, paste, remove, select_all
};

struct DocumentContextMenuState final {
    bool undo{};
    bool redo{};
    bool cut{};
    bool copy{};
    bool paste{};
    bool remove{};
    bool select_all{};
};

using DocumentContextStateQuery = std::function<DocumentContextMenuState()>;
using DocumentContextCommandHandler = std::function<void(DocumentContextCommand)>;

[[nodiscard]] bool ShowDocumentContextMenu(HWND owner, POINT screen_point,
    UINT dpi, COLORREF text, COLORREF background,
    const DocumentContextMenuState& state,
    const DocumentContextCommandHandler& handler);
[[nodiscard]] bool HandleDocumentContextMenuMessage(
    UINT message, WPARAM w_param, LPARAM l_param, LRESULT& result);

}  // namespace markdownmay::editor
