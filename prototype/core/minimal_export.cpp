#include "minimal_export.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace markdownmay::prototype {
namespace {

using Bytes = std::vector<std::uint8_t>;

struct ZipEntry final {
    std::string path;
    std::string content;
    std::uint32_t crc{};
    std::uint32_t local_offset{};
};

void AppendU16(Bytes& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void AppendU32(Bytes& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
}

void AppendString(Bytes& output, std::string_view value) {
    output.insert(output.end(), value.begin(), value.end());
}

std::uint32_t Crc32(std::string_view input) {
    std::uint32_t crc = 0xffffffffU;
    for (const unsigned char byte : input) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask =
                0U - static_cast<std::uint32_t>(crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

bool WriteBytes(const std::filesystem::path& target, const Bytes& bytes) {
    std::ofstream stream(target, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }
    stream.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    stream.flush();
    return stream.good();
}

std::string EscapeXml(std::string_view input) {
    std::string escaped;
    escaped.reserve(input.size());
    for (const char value : input) {
        switch (value) {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '"':
                escaped += "&quot;";
                break;
            case '\'':
                escaped += "&apos;";
                break;
            default:
                escaped.push_back(value);
                break;
        }
    }
    return escaped;
}

bool BuildStoredZip(
    const std::filesystem::path& target,
    std::vector<ZipEntry> entries) {
    Bytes output;
    for (auto& entry : entries) {
        if (entry.path.size() > 0xffffU ||
            entry.content.size() > 0xffffffffULL ||
            output.size() > 0xffffffffULL) {
            return false;
        }

        entry.crc = Crc32(entry.content);
        entry.local_offset = static_cast<std::uint32_t>(output.size());

        AppendU32(output, 0x04034b50U);
        AppendU16(output, 20);
        AppendU16(output, 0x0800U);
        AppendU16(output, 0);
        AppendU16(output, 0);
        AppendU16(output, 0);
        AppendU32(output, entry.crc);
        AppendU32(output, static_cast<std::uint32_t>(entry.content.size()));
        AppendU32(output, static_cast<std::uint32_t>(entry.content.size()));
        AppendU16(output, static_cast<std::uint16_t>(entry.path.size()));
        AppendU16(output, 0);
        AppendString(output, entry.path);
        AppendString(output, entry.content);
    }

    if (output.size() > 0xffffffffULL) {
        return false;
    }
    const auto central_offset = static_cast<std::uint32_t>(output.size());

    for (const auto& entry : entries) {
        AppendU32(output, 0x02014b50U);
        AppendU16(output, 20);
        AppendU16(output, 20);
        AppendU16(output, 0x0800U);
        AppendU16(output, 0);
        AppendU16(output, 0);
        AppendU16(output, 0);
        AppendU32(output, entry.crc);
        AppendU32(output, static_cast<std::uint32_t>(entry.content.size()));
        AppendU32(output, static_cast<std::uint32_t>(entry.content.size()));
        AppendU16(output, static_cast<std::uint16_t>(entry.path.size()));
        AppendU16(output, 0);
        AppendU16(output, 0);
        AppendU16(output, 0);
        AppendU16(output, 0);
        AppendU32(output, 0);
        AppendU32(output, entry.local_offset);
        AppendString(output, entry.path);
    }

    if (output.size() > 0xffffffffULL ||
        entries.size() > 0xffffU) {
        return false;
    }
    const auto central_size =
        static_cast<std::uint32_t>(output.size()) - central_offset;

    AppendU32(output, 0x06054b50U);
    AppendU16(output, 0);
    AppendU16(output, 0);
    AppendU16(output, static_cast<std::uint16_t>(entries.size()));
    AppendU16(output, static_cast<std::uint16_t>(entries.size()));
    AppendU32(output, central_size);
    AppendU32(output, central_offset);
    AppendU16(output, 0);

    return WriteBytes(target, output);
}

}  // namespace

bool WriteMinimalPdf(
    const std::filesystem::path& target,
    std::string_view ascii_title) noexcept {
    try {
        std::string safe_title;
        safe_title.reserve(ascii_title.size());
        for (const unsigned char value : ascii_title) {
            if (value >= 32U && value <= 126U &&
                value != '(' && value != ')' && value != '\\') {
                safe_title.push_back(static_cast<char>(value));
            } else {
                safe_title.push_back(' ');
            }
        }

        const std::string stream_text =
            "BT /F1 22 Tf 72 760 Td (" + safe_title + ") Tj ET\n";
        const std::array<std::string, 5> objects{
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
            "/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
            "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
            "<< /Length " + std::to_string(stream_text.size()) +
                " >>\nstream\n" + stream_text + "endstream"};

        std::string pdf = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
        std::array<std::size_t, 6> offsets{};
        for (std::size_t index = 0; index < objects.size(); ++index) {
            offsets[index + 1] = pdf.size();
            pdf += std::to_string(index + 1);
            pdf += " 0 obj\n";
            pdf += objects[index];
            pdf += "\nendobj\n";
        }

        const std::size_t xref_offset = pdf.size();
        std::ostringstream xref;
        xref << "xref\n0 6\n";
        xref << "0000000000 65535 f \n";
        for (std::size_t index = 1; index < offsets.size(); ++index) {
            xref << std::setw(10) << std::setfill('0') << offsets[index]
                 << " 00000 n \n";
        }
        xref << "trailer\n<< /Size 6 /Root 1 0 R >>\n";
        xref << "startxref\n" << xref_offset << "\n%%EOF\n";
        pdf += xref.str();

        std::ofstream output(target, std::ios::binary | std::ios::trunc);
        output.write(pdf.data(), static_cast<std::streamsize>(pdf.size()));
        output.flush();
        return output.good();
    } catch (...) {
        return false;
    }
}

bool WriteMinimalDocx(
    const std::filesystem::path& target,
    std::string_view utf8_title) noexcept {
    try {
        const std::string title = EscapeXml(utf8_title);
        std::vector<ZipEntry> entries{
            {
                "[Content_Types].xml",
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<Types xmlns=\"http://schemas.openxmlformats.org/package/"
                "2006/content-types\">"
                "<Default Extension=\"rels\" ContentType=\"application/vnd."
                "openxmlformats-package.relationships+xml\"/>"
                "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                "<Override PartName=\"/word/document.xml\" ContentType=\""
                "application/vnd.openxmlformats-officedocument."
                "wordprocessingml.document.main+xml\"/>"
                "<Override PartName=\"/word/styles.xml\" ContentType=\""
                "application/vnd.openxmlformats-officedocument."
                "wordprocessingml.styles+xml\"/>"
                "</Types>"},
            {
                "_rels/.rels",
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<Relationships xmlns=\"http://schemas.openxmlformats.org/"
                "package/2006/relationships\">"
                "<Relationship Id=\"rId1\" Type=\"http://schemas."
                "openxmlformats.org/officeDocument/2006/relationships/"
                "officeDocument\" Target=\"word/document.xml\"/>"
                "</Relationships>"},
            {
                "word/_rels/document.xml.rels",
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<Relationships xmlns=\"http://schemas.openxmlformats.org/"
                "package/2006/relationships\">"
                "<Relationship Id=\"rId1\" Type=\"http://schemas."
                "openxmlformats.org/officeDocument/2006/relationships/"
                "styles\" Target=\"styles.xml\"/>"
                "</Relationships>"},
            {
                "word/styles.xml",
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/"
                "wordprocessingml/2006/main\">"
                "<w:style w:type=\"paragraph\" w:default=\"1\" "
                "w:styleId=\"Normal\"><w:name w:val=\"Normal\"/></w:style>"
                "<w:style w:type=\"paragraph\" w:styleId=\"Heading1\">"
                "<w:name w:val=\"heading 1\"/><w:basedOn w:val=\"Normal\"/>"
                "<w:pPr><w:outlineLvl w:val=\"0\"/></w:pPr>"
                "<w:rPr><w:b/><w:sz w:val=\"32\"/></w:rPr></w:style>"
                "</w:styles>"},
            {
                "word/document.xml",
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                "<w:document xmlns:w=\"http://schemas.openxmlformats.org/"
                "wordprocessingml/2006/main\"><w:body>"
                "<w:p><w:pPr><w:pStyle w:val=\"Heading1\"/></w:pPr>"
                "<w:r><w:t>" +
                    title +
                    "</w:t></w:r></w:p>"
                    "<w:p><w:r><w:t>Markdown May DOCX prototype</w:t>"
                    "</w:r></w:p>"
                    "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/>"
                    "<w:pgMar w:top=\"1134\" w:right=\"1134\" "
                    "w:bottom=\"1134\" w:left=\"1134\"/></w:sectPr>"
                    "</w:body></w:document>"}};

        return BuildStoredZip(target, std::move(entries));
    } catch (...) {
        return false;
    }
}

}  // namespace markdownmay::prototype
