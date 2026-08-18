#pragma once

#include "markdownmay/document/document_session.hpp"
#include "markdownmay/editor/view_mode_controller.hpp"

#include <windows.h>

#include <memory>
#include <vector>

namespace markdownmay::ui {

class OutlineView final {
public:
    struct Item final {
        std::uint64_t source_offset{};
        std::uint8_t level{1};
        std::wstring label;
    };
    OutlineView(document::DocumentSession& session,
                editor::ViewModeController& modes);
    ~OutlineView();
    [[nodiscard]] bool create(HWND parent);
    void resize(const RECT& bounds);
    void refresh();
    [[nodiscard]] bool handle_control(HWND control, std::uint16_t notification);
    [[nodiscard]] bool handle_notify(const NMHDR& notification);
    [[nodiscard]] HWND handle() const noexcept;
    [[nodiscard]] int width() const noexcept;
    void set_width(int width) noexcept;
    [[nodiscard]] bool has_headings() const noexcept;
    void apply_appearance(COLORREF text, COLORREF background, UINT dpi);

private:
    document::DocumentSession& session_;
    editor::ViewModeController& modes_;
    HWND handle_{};
    HFONT font_{};
    UINT dpi_{96};
    int width_{240};
    std::vector<Item> items_;
    std::shared_ptr<int> lifetime_{std::make_shared<int>(0)};
};

}  // namespace markdownmay::ui
