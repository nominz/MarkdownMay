#pragma once

#include "markdownmay/editor/richedit_host.hpp"
#include "markdownmay/editor/source_view.hpp"

#include <windows.h>

namespace markdownmay::editor {

class SplitView final {
public:
    explicit SplitView(document::DocumentSession& session);
    ~SplitView();
    SplitView(const SplitView&) = delete;
    SplitView& operator=(const SplitView&) = delete;

    [[nodiscard]] ErrorCode create(HWND parent, const RECT& bounds);
    [[nodiscard]] ErrorCode project();
    [[nodiscard]] HWND handle() const noexcept;
    [[nodiscard]] SourceView& source_view() noexcept;
    [[nodiscard]] RichEditHost& render_view() noexcept;
    static LRESULT CALLBACK HostProcedure(HWND window, UINT message,
                                           WPARAM w_param, LPARAM l_param);

private:
    void Layout(int width, int height);
    void RefreshRender(ErrorCode source_result);

    document::DocumentSession& session_;
    SourceView source_;
    RichEditHost render_;
    HWND host_{};
};

}  // namespace markdownmay::editor
