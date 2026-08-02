#include "markdownmay/platform/printing.hpp"

#include <commdlg.h>
#include <richedit.h>

#include <algorithm>

namespace markdownmay::platform {
namespace {
LONG Mm100ToTwips(DWORD value) noexcept {
    return MulDiv(static_cast<int>((std::min)(value, 10000UL)), 1440, 2540);
}
}

PrintLayout CalculatePrintLayout(PrintDeviceMetrics metrics,
                                 const PageSetup& setup) noexcept {
    if (metrics.dpi_x <= 0 || metrics.dpi_y <= 0 ||
        metrics.physical_width <= 0 || metrics.physical_height <= 0) return {};
    PrintLayout result;
    result.page_twips = {0, 0, MulDiv(metrics.physical_width, 1440, metrics.dpi_x),
        MulDiv(metrics.physical_height, 1440, metrics.dpi_y)};
    result.content_twips.left = (std::max)(0L, Mm100ToTwips(setup.left_hundredths_mm) -
        MulDiv(metrics.offset_x, 1440, metrics.dpi_x));
    result.content_twips.top = (std::max)(0L, Mm100ToTwips(setup.top_hundredths_mm) -
        MulDiv(metrics.offset_y, 1440, metrics.dpi_y));
    result.content_twips.right = (std::max)(result.content_twips.left + 1,
        result.page_twips.right - Mm100ToTwips(setup.right_hundredths_mm));
    result.content_twips.bottom = (std::max)(result.content_twips.top + 1,
        result.page_twips.bottom - Mm100ToTwips(setup.bottom_hundredths_mm));
    return result;
}

PrintLayout CalculatePrintLayout(HDC dc, const PageSetup& setup) noexcept {
    if (!dc) return {};
    return CalculatePrintLayout({GetDeviceCaps(dc, LOGPIXELSX),
        GetDeviceCaps(dc, LOGPIXELSY), GetDeviceCaps(dc, PHYSICALWIDTH),
        GetDeviceCaps(dc, PHYSICALHEIGHT), GetDeviceCaps(dc, PHYSICALOFFSETX),
        GetDeviceCaps(dc, PHYSICALOFFSETY)}, setup);
}

bool ShowPageSetupDialog(HWND owner, PageSetup& setup) noexcept {
    PRINTDLGW defaults{};
    defaults.lStructSize = sizeof(defaults);
    defaults.Flags = PD_RETURNDEFAULT;
    if (PrintDlgW(&defaults) && defaults.hDevMode) {
        auto* mode = static_cast<DEVMODEW*>(GlobalLock(defaults.hDevMode));
        if (mode) {
            mode->dmFields |= DM_ORIENTATION;
            mode->dmOrientation = setup.landscape ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
            GlobalUnlock(defaults.hDevMode);
        }
    }
    PAGESETUPDLGW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.Flags = PSD_MARGINS | PSD_INHUNDREDTHSOFMILLIMETERS;
    dialog.hDevMode = defaults.hDevMode;
    dialog.hDevNames = defaults.hDevNames;
    dialog.rtMargin = {static_cast<LONG>(setup.left_hundredths_mm),
        static_cast<LONG>(setup.top_hundredths_mm),
        static_cast<LONG>(setup.right_hundredths_mm),
        static_cast<LONG>(setup.bottom_hundredths_mm)};
    if (!PageSetupDlgW(&dialog)) {
        if (dialog.hDevMode) GlobalFree(dialog.hDevMode);
        if (dialog.hDevNames) GlobalFree(dialog.hDevNames);
        return false;
    }
    setup.left_hundredths_mm = dialog.rtMargin.left;
    setup.top_hundredths_mm = dialog.rtMargin.top;
    setup.right_hundredths_mm = dialog.rtMargin.right;
    setup.bottom_hundredths_mm = dialog.rtMargin.bottom;
    if (dialog.hDevMode) {
        const auto* mode = static_cast<const DEVMODEW*>(GlobalLock(dialog.hDevMode));
        if (mode) { setup.landscape = mode->dmOrientation == DMORIENT_LANDSCAPE; GlobalUnlock(dialog.hDevMode); }
    }
    if (dialog.hDevMode) GlobalFree(dialog.hDevMode);
    if (dialog.hDevNames) GlobalFree(dialog.hDevNames);
    return true;
}

ErrorCode PrintRichEdit(HWND owner, HWND rich_edit, const PageSetup& setup) noexcept {
    if (!rich_edit) return ErrorCode::platform_print_failed;
    PRINTDLGW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.Flags = PD_RETURNDC | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;
    dialog.nCopies = 1;
    if (!PrintDlgW(&dialog))
        return CommDlgExtendedError() == 0 ? ErrorCode::ok : ErrorCode::platform_print_failed;
    if (dialog.hDevMode) {
        auto* mode = static_cast<DEVMODEW*>(GlobalLock(dialog.hDevMode));
        if (mode) {
            mode->dmFields |= DM_ORIENTATION;
            mode->dmOrientation = setup.landscape ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
            dialog.hDC = ResetDCW(dialog.hDC, mode);
            GlobalUnlock(dialog.hDevMode);
        }
    }
    if (!dialog.hDC) {
        if (dialog.hDevMode) GlobalFree(dialog.hDevMode);
        if (dialog.hDevNames) GlobalFree(dialog.hDevNames);
        return ErrorCode::platform_print_failed;
    }
    const auto cleanup = [&] {
        if (dialog.hDC) DeleteDC(dialog.hDC);
        if (dialog.hDevMode) GlobalFree(dialog.hDevMode);
        if (dialog.hDevNames) GlobalFree(dialog.hDevNames);
    };
    DOCINFOW document{sizeof(document), L"马冬梅 Markdown 文档"};
    if (StartDocW(dialog.hDC, &document) <= 0) { cleanup(); return ErrorCode::platform_print_failed; }
    const auto layout = CalculatePrintLayout(dialog.hDC, setup);
    FORMATRANGE range{};
    range.hdc = dialog.hDC; range.hdcTarget = dialog.hDC;
    range.rc = layout.content_twips; range.rcPage = layout.page_twips;
    range.chrg.cpMin = 0; range.chrg.cpMax = -1;
    const auto length = GetWindowTextLengthW(rich_edit);
    bool failed{};
    while (range.chrg.cpMin < length) {
        if (StartPage(dialog.hDC) <= 0) { failed = true; break; }
        const auto next = static_cast<LONG>(SendMessageW(rich_edit, EM_FORMATRANGE, TRUE,
            reinterpret_cast<LPARAM>(&range)));
        if (EndPage(dialog.hDC) <= 0 || next <= range.chrg.cpMin) { failed = true; break; }
        range.chrg.cpMin = next;
    }
    SendMessageW(rich_edit, EM_FORMATRANGE, FALSE, 0);
    if (failed) AbortDoc(dialog.hDC); else if (EndDoc(dialog.hDC) <= 0) failed = true;
    cleanup();
    return failed ? ErrorCode::platform_print_failed : ErrorCode::ok;
}

}  // namespace markdownmay::platform
