#include "markdownmay/editor/richedit_host.hpp"

#include "markdownmay/fileio/file_service.hpp"

#include <richedit.h>
#include <commdlg.h>

#include <array>
#include <filesystem>
#include <string_view>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    using namespace markdownmay;
    const auto loaded = fileio::LoadTextFile(
        std::filesystem::path(MARKDOWNMAY_COMPLETE_FIXTURE));
    if (!loaded.is_ok()) return 1;
    document::DocumentSession session(loaded.value().source);
    HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED,
        0, 0, 900, 700, nullptr, nullptr, instance, nullptr);
    if (!parent) return 2;
    editor::RichEditHost host(session);
    host.set_document_path(loaded.value().path);
    RECT bounds{0, 0, 900, 700};
    if (host.create(parent, bounds) != ErrorCode::ok) return 3;

    const auto length = GetWindowTextLengthW(host.handle());
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(host.handle(), text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    if (text.find(L"完整文档") == std::wstring::npos ||
        text.find(L"&、©、中") == std::wstring::npos ||
        text.find(L"原始 HTML 块必须可见且原样保留") == std::wstring::npos ||
        text.find(L"远程图片未加载") == std::wstring::npos ||
        text.find(L"```cpp") != std::wstring::npos) return 4;

    const auto original = session.snapshot().source;
    const auto raw_begin = original.find("<section data-keep=\"yes\">");
    const auto raw_close = original.find("</section>", raw_begin);
    if (raw_begin == std::string::npos || raw_close == std::string::npos) return 5;
    const auto raw_block = original.substr(raw_begin,
        raw_close + std::string_view("</section>").size() - raw_begin);
    if (host.project() != ErrorCode::ok || session.snapshot().source != original ||
        session.is_dirty()) return 5;

    FINDTEXTEXW find_paragraph{{0, -1}, const_cast<wchar_t*>(L"普通段落"), {}};
    if (SendMessageW(host.handle(), EM_FINDTEXTEXW, FR_DOWN,
        reinterpret_cast<LPARAM>(&find_paragraph)) < 0) return 6;
    const auto insert_at = find_paragraph.chrgText.cpMin + 2;
    SendMessageW(host.handle(), EM_SETSEL, insert_at, insert_at);
    SendMessageW(host.handle(), EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"新"));
    if (host.synchronize_change() != ErrorCode::ok ||
        session.snapshot().source.find("普通新段落") == std::string::npos ||
        session.snapshot().source.find(raw_block) == std::string::npos) return 7;
    if (host.undo() != ErrorCode::ok || session.snapshot().source != original) return 8;
    DestroyWindow(parent);
    return 0;
}
