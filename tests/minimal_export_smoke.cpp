#include "minimal_export.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

bool StartsWith(
    const std::filesystem::path& path,
    const std::array<char, 4>& expected) {
    std::ifstream stream(path, std::ios::binary);
    std::array<char, 4> actual{};
    stream.read(actual.data(), static_cast<std::streamsize>(actual.size()));
    return stream.good() && actual == expected;
}

}  // namespace

int main() {
    const auto temporary = std::filesystem::temp_directory_path();
    const auto pdf = temporary / "markdownmay-prototype-smoke.pdf";
    const auto docx = temporary / "markdownmay-prototype-smoke.docx";

    if (!markdownmay::prototype::WriteMinimalPdf(
            pdf,
            "Markdown May PDF prototype")) {
        return 1;
    }
    if (!StartsWith(pdf, {'%', 'P', 'D', 'F'})) {
        return 2;
    }

    if (!markdownmay::prototype::WriteMinimalDocx(
            docx,
            "马冬梅 DOCX 样机")) {
        return 3;
    }
    if (!StartsWith(docx, {'P', 'K', '\x03', '\x04'})) {
        return 4;
    }

    std::error_code ignored;
    std::filesystem::remove(pdf, ignored);
    std::filesystem::remove(docx, ignored);
    return 0;
}
