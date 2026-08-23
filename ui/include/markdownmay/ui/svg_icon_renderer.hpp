#pragma once

#include <windows.h>

#include <cstdint>

namespace markdownmay::ui {

// Centralized renderer for the embedded, outline-style Tabler SVG resources.
// It keeps SVG as the shipped source, applies the state color at draw time and
// scales the 24x24 viewBox directly for the current DPI.
class SvgIconRenderer final {
public:
    [[nodiscard]] static bool draw(HDC dc, const RECT& bounds,
        std::uint16_t resource_id, COLORREF color, UINT dpi);
};

}  // namespace markdownmay::ui
