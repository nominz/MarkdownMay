#include "markdownmay/app/about_dialog.hpp"

#include "markdownmay/build_version.hpp"
#include "resource.h"

#include <commctrl.h>
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#include <shellapi.h>

#include <algorithm>
#include <memory>
#include <string>

namespace markdownmay::app {
namespace {

constexpr COLORREF kText = RGB(32, 32, 32);
constexpr COLORREF kMuted = RGB(102, 102, 102);

struct Fonts final {
    HFONT name{};
    HFONT tagline{};
    HFONT normal{};
    HFONT small_font{};
    ~Fonts() {
        if (name) DeleteObject(name);
        if (tagline) DeleteObject(tagline);
        if (normal) DeleteObject(normal);
        if (small_font) DeleteObject(small_font);
    }
};

struct AboutState final {
    UINT dpi{96};
    std::unique_ptr<Fonts> fonts;
};

int Px(int logical, UINT dpi) { return MulDiv(logical, static_cast<int>(dpi), 96); }

HFONT MakeFont(UINT dpi, int points, int weight = FW_NORMAL) {
    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi);
    auto font = metrics.lfMessageFont;
    font.lfHeight = -MulDiv(points, static_cast<int>(dpi), 72);
    font.lfWeight = weight;
    wcscpy_s(font.lfFaceName, L"Microsoft YaHei UI");
    return CreateFontIndirectW(&font);
}

void ApplyFonts(HWND dialog, AboutState& state) {
    state.fonts = std::make_unique<Fonts>();
    state.fonts->name = MakeFont(state.dpi, 21, FW_SEMIBOLD);
    state.fonts->tagline = MakeFont(state.dpi, 10, FW_SEMIBOLD);
    state.fonts->normal = MakeFont(state.dpi, 10);
    state.fonts->small_font = MakeFont(state.dpi, 9);
    const auto set = [dialog](int id, HFONT font) {
        SendDlgItemMessageW(dialog, id, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    };
    set(IDC_ABOUT_NAME, state.fonts->name);
    set(IDC_ABOUT_TAGLINE, state.fonts->tagline);
    for (const int id : {IDC_ABOUT_ENGLISH_NAME, IDC_ABOUT_DESCRIPTION,
            IDC_ABOUT_CONTACT_TITLE, IDC_ABOUT_EMAIL_LINK, IDC_ABOUT_WECHAT_LINK,
            IDC_ABOUT_GITHUB_LINK, IDC_ABOUT_GITEE_LINK, IDC_ABOUT_ISSUES_LINK,
            IDC_ABOUT_CLOSE}) set(id, state.fonts->normal);
    set(IDC_ABOUT_VERSION, state.fonts->small_font);
    set(IDC_ABOUT_COPYRIGHT, state.fonts->small_font);
}

void SetClientSize(HWND window, int width, int height, UINT dpi) {
    RECT rectangle{0, 0, Px(width, dpi), Px(height, dpi)};
    AdjustWindowRectExForDpi(&rectangle, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)),
        FALSE, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE)), dpi);
    SetWindowPos(window, nullptr, 0, 0, rectangle.right - rectangle.left,
        rectangle.bottom - rectangle.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Move(HWND dialog, int id, int x, int y, int width, int height, UINT dpi) {
    SetWindowPos(GetDlgItem(dialog, id), nullptr, Px(x, dpi), Px(y, dpi), Px(width, dpi),
        Px(height, dpi), SWP_NOZORDER | SWP_NOACTIVATE);
}

void LayoutAbout(HWND dialog, UINT dpi) {
    Move(dialog, IDC_ABOUT_PORTRAIT, 26, 47, 260, 260, dpi);
    Move(dialog, IDC_ABOUT_APP_ICON, 320, 28, 48, 48, dpi);
    Move(dialog, IDC_ABOUT_NAME, 382, 24, 300, 34, dpi);
    Move(dialog, IDC_ABOUT_ENGLISH_NAME, 382, 58, 270, 24, dpi);
    Move(dialog, IDC_ABOUT_VERSION, 382, 83, 220, 20, dpi);
    Move(dialog, IDC_ABOUT_TAGLINE, 320, 126, 374, 25, dpi);
    Move(dialog, IDC_ABOUT_DESCRIPTION, 320, 155, 374, 24, dpi);
    Move(dialog, IDC_ABOUT_CONTACT_TITLE, 320, 203, 160, 24, dpi);
    Move(dialog, IDC_ABOUT_EMAIL_ICON, 320, 235, 20, 20, dpi);
    Move(dialog, IDC_ABOUT_EMAIL_LINK, 350, 232, 240, 25, dpi);
    Move(dialog, IDC_ABOUT_WECHAT_ICON, 320, 269, 20, 20, dpi);
    Move(dialog, IDC_ABOUT_WECHAT_LINK, 350, 266, 180, 25, dpi);
    Move(dialog, IDC_ABOUT_GITHUB_ICON, 320, 310, 18, 18, dpi);
    Move(dialog, IDC_ABOUT_GITHUB_LINK, 346, 306, 68, 25, dpi);
    Move(dialog, IDC_ABOUT_GITEE_ICON, 427, 310, 18, 18, dpi);
    Move(dialog, IDC_ABOUT_GITEE_LINK, 453, 306, 58, 25, dpi);
    Move(dialog, IDC_ABOUT_ISSUES_LINK, 532, 306, 100, 25, dpi);
    Move(dialog, IDC_ABOUT_SEPARATOR, 26, 350, 668, 2, dpi);
    Move(dialog, IDC_ABOUT_COPYRIGHT, 26, 375, 500, 22, dpi);
    Move(dialog, IDC_ABOUT_CLOSE, 616, 368, 78, 28, dpi);
}

void CenterOnOwner(HWND dialog) {
    RECT owner{};
    RECT window{};
    GetWindowRect(GetParent(dialog), &owner);
    GetWindowRect(dialog, &window);
    const auto x = owner.left + ((owner.right - owner.left) - (window.right - window.left)) / 2;
    const auto y = owner.top + ((owner.bottom - owner.top) - (window.bottom - window.top)) / 2;
    SetWindowPos(dialog, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void DrawResourceImage(const DRAWITEMSTRUCT& draw, int resource_id) {
    const auto module = GetModuleHandleW(nullptr);
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(resource_id), RT_RCDATA);
    const auto loaded = resource ? LoadResource(module, resource) : nullptr;
    const auto bytes = loaded ? LockResource(loaded) : nullptr;
    const auto size = resource ? SizeofResource(module, resource) : 0;
    if (!bytes || !size) return;
    const auto memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory) return;
    auto* target = GlobalLock(memory);
    if (!target) { GlobalFree(memory); return; }
    std::memcpy(target, bytes, size);
    GlobalUnlock(memory);
    IStream* stream{};
    if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) {
        GlobalFree(memory);
        return;
    }
    Gdiplus::Bitmap image(stream, FALSE);
    if (image.GetLastStatus() == Gdiplus::Ok && image.GetWidth() && image.GetHeight()) {
        Gdiplus::Graphics graphics(draw.hDC);
        graphics.Clear(Gdiplus::Color(255, 255, 255, 255));
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        const auto available_width = draw.rcItem.right - draw.rcItem.left;
        const auto available_height = draw.rcItem.bottom - draw.rcItem.top;
        const auto scale = std::min(static_cast<double>(available_width) / image.GetWidth(),
            static_cast<double>(available_height) / image.GetHeight());
        const auto width = static_cast<int>(image.GetWidth() * scale);
        const auto height = static_cast<int>(image.GetHeight() * scale);
        const auto x = draw.rcItem.left + (available_width - width) / 2;
        const auto y = draw.rcItem.top + (available_height - height) / 2;
        graphics.DrawImage(&image, Gdiplus::Rect(x, y, width, height));
    }
    stream->Release();
}

INT_PTR CALLBACK WechatProcedure(HWND dialog, UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case WM_INITDIALOG: {
        const auto dpi = GetDpiForWindow(dialog);
        SetClientSize(dialog, 330, 390, dpi);
        Move(dialog, IDC_WECHAT_QR, 35, 24, 260, 300, dpi);
        Move(dialog, IDC_WECHAT_HINT, 35, 330, 260, 24, dpi);
        Move(dialog, IDOK, 126, 357, 78, 28, dpi);
        SendDlgItemMessageW(dialog, IDC_WECHAT_HINT, WM_SETFONT,
            SendMessageW(dialog, WM_GETFONT, 0, 0), TRUE);
        CenterOnOwner(dialog);
        return TRUE;
    }
    case WM_DRAWITEM:
        if (w_param == IDC_WECHAT_QR) {
            DrawResourceImage(*reinterpret_cast<const DRAWITEMSTRUCT*>(l_param), IDR_ABOUT_WECHAT_JPG);
            return TRUE;
        }
        break;
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(l_param);
        SetWindowPos(dialog, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        const auto dpi = HIWORD(w_param);
        SetClientSize(dialog, 330, 390, dpi);
        Move(dialog, IDC_WECHAT_QR, 35, 24, 260, 300, dpi);
        Move(dialog, IDC_WECHAT_HINT, 35, 330, 260, 24, dpi);
        Move(dialog, IDOK, 126, 357, 78, 28, dpi);
        InvalidateRect(dialog, nullptr, TRUE);
        return TRUE;
    }
    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(w_param) == IDOK || LOWORD(w_param) == IDCANCEL) {
            EndDialog(dialog, LOWORD(w_param));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void OpenLink(HWND dialog, const wchar_t* target) {
    ShellExecuteW(dialog, L"open", target, nullptr, nullptr, SW_SHOWNORMAL);
}

INT_PTR CALLBACK AboutProcedure(HWND dialog, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* state = reinterpret_cast<AboutState*>(GetWindowLongPtrW(dialog, DWLP_USER));
    switch (message) {
    case WM_INITDIALOG: {
        state = new AboutState;
        state->dpi = GetDpiForWindow(dialog);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
        SetClientSize(dialog, 720, 420, state->dpi);
        ApplyFonts(dialog, *state);
        LayoutAbout(dialog, state->dpi);
        SetDlgItemTextW(dialog, IDC_ABOUT_VERSION,
            (std::wstring(L"Version ") + MARKDOWNMAY_VERSION_WSTRING).c_str());
        SetDlgItemTextW(dialog, IDC_ABOUT_EMAIL_LINK,
            L"<a href=\"mailto:nominz@qq.com\">nominz@qq.com</a>");
        SetDlgItemTextW(dialog, IDC_ABOUT_WECHAT_LINK,
            L"<a href=\"wechat\">微信：nominz</a>");
        SetDlgItemTextW(dialog, IDC_ABOUT_GITHUB_LINK,
            L"<a href=\"https://github.com/nominz/MarkdownMay\">GitHub</a>");
        SetDlgItemTextW(dialog, IDC_ABOUT_GITEE_LINK,
            L"<a href=\"https://gitee.com/nominz/MarkdownMay\">Gitee</a>");
        SetDlgItemTextW(dialog, IDC_ABOUT_ISSUES_LINK,
            L"<a href=\"https://github.com/nominz/MarkdownMay/issues\">问题反馈</a>");
        SendDlgItemMessageW(dialog, IDC_ABOUT_APP_ICON, STM_SETICON,
            reinterpret_cast<WPARAM>(LoadImageW(GetModuleHandleW(nullptr),
                MAKEINTRESOURCEW(IDI_MARKDOWNMAY), IMAGE_ICON, Px(48, state->dpi),
                Px(48, state->dpi), LR_DEFAULTCOLOR | LR_SHARED)), 0);
        CenterOnOwner(dialog);
        SetFocus(GetDlgItem(dialog, IDC_ABOUT_CLOSE));
        return FALSE;
    }
    case WM_DRAWITEM: {
        int resource{};
        switch (w_param) {
        case IDC_ABOUT_PORTRAIT: resource = IDR_ABOUT_PORTRAIT_PNG; break;
        case IDC_ABOUT_EMAIL_ICON: resource = IDR_ABOUT_EMAIL_PNG; break;
        case IDC_ABOUT_WECHAT_ICON: resource = IDR_ABOUT_WECHAT_PNG; break;
        case IDC_ABOUT_GITHUB_ICON: resource = IDR_ABOUT_GITHUB_PNG; break;
        case IDC_ABOUT_GITEE_ICON: resource = IDR_ABOUT_GITEE_PNG; break;
        default: break;
        }
        if (resource) {
            DrawResourceImage(*reinterpret_cast<const DRAWITEMSTRUCT*>(l_param), resource);
            return TRUE;
        }
        break;
    }
    case WM_ERASEBKGND: {
        RECT client{};
        GetClientRect(dialog, &client);
        FillRect(reinterpret_cast<HDC>(w_param), &client,
            reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        return TRUE;
    }
    case WM_CTLCOLORDLG:
        return reinterpret_cast<INT_PTR>(GetStockObject(WHITE_BRUSH));
    case WM_CTLCOLORSTATIC: {
        const auto control = reinterpret_cast<HWND>(l_param);
        const auto id = GetDlgCtrlID(control);
        SetBkMode(reinterpret_cast<HDC>(w_param), TRANSPARENT);
        SetTextColor(reinterpret_cast<HDC>(w_param),
            (id == IDC_ABOUT_VERSION || id == IDC_ABOUT_DESCRIPTION ||
             id == IDC_ABOUT_COPYRIGHT) ? kMuted : kText);
        return reinterpret_cast<INT_PTR>(GetStockObject(WHITE_BRUSH));
    }
    case WM_NOTIFY: {
        const auto* link = reinterpret_cast<const NMLINK*>(l_param);
        if (!link || (link->hdr.code != NM_CLICK && link->hdr.code != NM_RETURN)) break;
        if (link->hdr.idFrom == IDC_ABOUT_WECHAT_LINK) {
            DialogBoxW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_WECHAT), dialog,
                WechatProcedure);
        } else {
            OpenLink(dialog, link->item.szUrl);
        }
        return TRUE;
    }
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(l_param);
        SetWindowPos(dialog, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        state->dpi = HIWORD(w_param);
        SetClientSize(dialog, 720, 420, state->dpi);
        ApplyFonts(dialog, *state);
        LayoutAbout(dialog, state->dpi);
        InvalidateRect(dialog, nullptr, TRUE);
        return TRUE;
    }
    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(w_param) == IDC_ABOUT_CLOSE || LOWORD(w_param) == IDCANCEL) {
            EndDialog(dialog, LOWORD(w_param));
            return TRUE;
        }
        break;
    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(dialog, DWLP_USER, 0);
        break;
    }
    return FALSE;
}

}  // namespace

void ShowAboutDialog(HWND owner) {
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token{};
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) return;
    DialogBoxW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_ABOUT), owner, AboutProcedure);
    Gdiplus::GdiplusShutdown(token);
}

}  // namespace markdownmay::app
