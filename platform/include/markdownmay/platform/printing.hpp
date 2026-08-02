#pragma once

#include "markdownmay/core/result.hpp"

#include <windows.h>

namespace markdownmay::platform {

struct PageSetup final {
    bool landscape{};
    DWORD left_hundredths_mm{2000};
    DWORD top_hundredths_mm{2000};
    DWORD right_hundredths_mm{2000};
    DWORD bottom_hundredths_mm{2000};
};

struct PrintLayout final { RECT page_twips{}; RECT content_twips{}; };
struct PrintDeviceMetrics final {
    int dpi_x{}; int dpi_y{}; int physical_width{}; int physical_height{};
    int offset_x{}; int offset_y{};
};

[[nodiscard]] PrintLayout CalculatePrintLayout(PrintDeviceMetrics metrics,
                                                const PageSetup& setup) noexcept;
[[nodiscard]] PrintLayout CalculatePrintLayout(HDC dc, const PageSetup& setup) noexcept;
[[nodiscard]] bool ShowPageSetupDialog(HWND owner, PageSetup& setup) noexcept;
[[nodiscard]] ErrorCode PrintRichEdit(HWND owner, HWND rich_edit,
                                      const PageSetup& setup) noexcept;

}  // namespace markdownmay::platform
