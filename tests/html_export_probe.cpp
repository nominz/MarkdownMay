#include "safe_html_probe.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
    const auto target = argc > 1 ? std::filesystem::path(argv[1]) :
        std::filesystem::temp_directory_path() / "markdownmay-html-probe.html";
    const std::string attack =
        "<img src=https://invalid.example/probe onerror=alert(1)>"
        "<script>document.body.dataset.compromised='yes'</script>";
    const std::string mermaid =
        "flowchart TD\nA[开始] --> B{安全?}\nB -->|是| C[离线输出]";
    const std::string formula = "x = \\frac{-b \\pm \\sqrt{b^2-4ac}}{2a}";
    if (!markdownmay::prototype::WriteSafeHtmlProbe(
            target, attack, mermaid, formula)) return 1;

    std::ifstream input(target, std::ios::binary);
    const std::string html{std::istreambuf_iterator<char>(input), {}};
    if (html.find("default-src 'none'") == std::string::npos ||
        html.find("connect-src 'none'") == std::string::npos ||
        html.find("script-src 'none'") == std::string::npos ||
        html.find("font-src 'none'") == std::string::npos) return 2;
    if (html.find("<script>") != std::string::npos ||
        html.find("<img src=https://invalid.example") != std::string::npos ||
        html.find("&lt;script&gt;") == std::string::npos) return 3;
    if (html.find("src=\"http") != std::string::npos ||
        html.find("href=\"http") != std::string::npos ||
        html.find("url(http") != std::string::npos) return 4;
    const auto math_namespace = html.find("http://www.w3.org/1998/Math/MathML");
    if (math_namespace == std::string::npos ||
        html.find("http://", math_namespace + 1) != std::string::npos) return 4;
    if (html.find("data-render-state=\"source-fallback\"") == std::string::npos ||
        html.find("data-render-state=\"mathml-system-font\"") == std::string::npos ||
        html.find("<math xmlns=\"http://www.w3.org/1998/Math/MathML\"") ==
            std::string::npos) return 5;
    return 0;
}
