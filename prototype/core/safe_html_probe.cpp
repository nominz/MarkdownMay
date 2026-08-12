#include "safe_html_probe.hpp"

#include <fstream>
#include <string>

namespace markdownmay::prototype {
namespace {

std::string EscapeHtml(std::string_view input) {
    std::string result;
    result.reserve(input.size());
    for (const char value : input) {
        switch (value) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&#39;"; break;
        default: result.push_back(value); break;
        }
    }
    return result;
}

}  // namespace

bool WriteSafeHtmlProbe(
    const std::filesystem::path& target,
    std::string_view untrusted_text,
    std::string_view mermaid_source,
    std::string_view formula_source) noexcept {
    try {
        const std::string html =
            "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
            "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; "
            "img-src data:; style-src 'unsafe-inline'; script-src 'none'; "
            "font-src 'none'; connect-src 'none'; object-src 'none'; base-uri 'none'; "
            "form-action 'none'\">"
            "<meta name=\"referrer\" content=\"no-referrer\">"
            "<title>Markdown May HTML 安全样机</title>"
            "<style>body{font-family:'Segoe UI','Microsoft YaHei UI',sans-serif;"
            "max-width:52rem;margin:2rem auto;padding:0 1rem;line-height:1.65;color:#202124}"
            ".probe{border:1px solid #b8bec6;border-radius:.4rem;padding:1rem;margin:1rem 0}"
            ".fallback{background:#f5f6f8;white-space:pre-wrap;overflow-wrap:anywhere}"
            "math{font-family:'Cambria Math','Segoe UI Symbol',serif;font-size:1.25em}"
            "</style></head><body><main><h1>离线 HTML 安全样机</h1>"
            "<section id=\"untrusted\" class=\"probe\"><h2>不可信文本</h2><p>" +
            EscapeHtml(untrusted_text) +
            "</p></section><section id=\"mermaid\" class=\"probe\" "
            "data-render-state=\"source-fallback\"><h2>Mermaid 源码回退</h2>"
            "<p>当前样机不执行 Mermaid JavaScript；正式路线需要固定资源审查。</p>"
            "<pre class=\"fallback\"><code>" + EscapeHtml(mermaid_source) +
            "</code></pre></section><section id=\"formula\" class=\"probe\" "
            "data-render-state=\"mathml-system-font\"><h2>公式系统字体样机</h2>"
            "<math xmlns=\"http://www.w3.org/1998/Math/MathML\" display=\"block\" "
            "aria-label=\"x 等于负 b 加减根号 b 平方减四 ac 除以二 a\">"
            "<mrow><mi>x</mi><mo>=</mo><mfrac><mrow><mo>−</mo><mi>b</mi><mo>±</mo>"
            "<msqrt><msup><mi>b</mi><mn>2</mn></msup><mo>−</mo><mn>4</mn><mi>a</mi>"
            "<mi>c</mi></msqrt></mrow><mrow><mn>2</mn><mi>a</mi></mrow></mfrac></mrow>"
            "</math><details><summary>原始公式源码</summary><pre class=\"fallback\"><code>" +
            EscapeHtml(formula_source) +
            "</code></pre></details></section>"
            "<p id=\"offline-marker\">本文件不包含外部 URL、脚本或字体资源。</p>"
            "</main></body></html>";
        std::ofstream output(target, std::ios::binary | std::ios::trunc);
        output.write(html.data(), static_cast<std::streamsize>(html.size()));
        output.flush();
        return output.good();
    } catch (...) {
        return false;
    }
}

}  // namespace markdownmay::prototype
