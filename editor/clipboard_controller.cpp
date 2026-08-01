#include "markdownmay/editor/clipboard_controller.hpp"

#include "markdownmay/fileio/path_utils.hpp"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace markdownmay::editor {
namespace {

std::string Lower(std::string_view value) {
    std::string result(value);
    for (auto& character : result)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return result;
}
std::string Attribute(std::string_view tag, std::string_view name) {
    const auto lower = Lower(tag); name = std::string_view(name);
    auto position = lower.find(std::string(name));
    if (position == std::string::npos) return {};
    position = tag.find('=', position + name.size());
    if (position == std::string::npos) return {};
    ++position; while (position < tag.size() && std::isspace(static_cast<unsigned char>(tag[position]))) ++position;
    if (position >= tag.size()) return {};
    const auto quote = tag[position] == '\'' || tag[position] == '"' ? tag[position++] : '\0';
    auto end = position;
    while (end < tag.size() && (quote ? tag[end] != quote
        : !std::isspace(static_cast<unsigned char>(tag[end])) && tag[end] != '>')) ++end;
    return std::string(tag.substr(position, end - position));
}
std::string SafeTarget(std::string target) {
    const auto lower = Lower(target);
    if (lower.starts_with("javascript:") || lower.starts_with("vbscript:") ||
        lower.starts_with("data:")) return "#";
    return target;
}
void AppendEntity(std::string_view entity, std::string& output) {
    if (entity == "amp") output.push_back('&');
    else if (entity == "lt") output.push_back('<');
    else if (entity == "gt") output.push_back('>');
    else if (entity == "quot") output.push_back('"');
    else if (entity == "nbsp") output.push_back(' ');
    else { output.push_back('&'); output.append(entity); output.push_back(';'); }
}
bool ImageExtension(const std::filesystem::path& path) {
    const auto extension = Lower(path.extension().string());
    static const std::unordered_set<std::string> supported{
        ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".tif", ".tiff", ".webp"};
    return supported.contains(extension);
}
}

ClipboardController::ClipboardController(document::DocumentSession& session,
    ParagraphEditor& editor, ImageController& images)
    : session_(session), editor_(editor), images_(images) {}

ErrorCode ClipboardController::paste_plain(std::string_view text) {
    return editor_.insert_text(text);
}

ErrorCode ClipboardController::paste_html(std::string_view html) {
    return editor_.insert_text(HtmlToMarkdown(html));
}

std::string ClipboardController::HtmlToMarkdown(std::string_view html) {
    std::string output;
    bool suppressed = false;
    std::vector<std::string> links;
    for (std::size_t index = 0; index < html.size();) {
        if (html[index] == '<') {
            const auto close = html.find('>', index + 1);
            if (close == std::string_view::npos) break;
            auto tag = html.substr(index + 1, close - index - 1);
            const auto lower = Lower(tag);
            const bool ending = !lower.empty() && lower.front() == '/';
            auto name_begin = ending ? 1U : 0U;
            while (name_begin < lower.size() && std::isspace(static_cast<unsigned char>(lower[name_begin]))) ++name_begin;
            auto name_end = name_begin;
            while (name_end < lower.size() && (std::isalnum(static_cast<unsigned char>(lower[name_end])) || lower[name_end] == '-')) ++name_end;
            const auto name = lower.substr(name_begin, name_end - name_begin);
            if (name == "script" || name == "style") suppressed = !ending;
            else if (!suppressed) {
                if (name == "br") output += "\n";
                else if (name == "p" || name == "div" || name == "h1" || name == "h2" ||
                         name == "h3" || name == "li") {
                    if (!output.empty() && output.back() != '\n') output += ending ? "\n\n" : "\n";
                    if (!ending && name == "li") output += "- ";
                    if (!ending && name.size() == 2 && name[0] == 'h')
                        output += std::string(static_cast<std::size_t>(name[1] - '0'), '#') + " ";
                } else if (name == "strong" || name == "b") output += "**";
                else if (name == "em" || name == "i") output += "*";
                else if (name == "del" || name == "s") output += "~~";
                else if (name == "code") output += "`";
                else if (name == "a") {
                    if (!ending) { output += "["; links.push_back(SafeTarget(Attribute(tag, "href"))); }
                    else if (!links.empty()) { output += "](" + links.back() + ")"; links.pop_back(); }
                } else if (name == "img" && !ending) {
                    output += "![" + Attribute(tag, "alt") + "](" +
                        SafeTarget(Attribute(tag, "src")) + ")";
                }
            }
            index = close + 1;
        } else if (html[index] == '&' && !suppressed) {
            const auto semicolon = html.find(';', index + 1);
            if (semicolon == std::string_view::npos) { output.push_back(html[index++]); continue; }
            AppendEntity(html.substr(index + 1, semicolon - index - 1), output);
            index = semicolon + 1;
        } else {
            if (!suppressed) output.push_back(html[index]);
            ++index;
        }
    }
    while (!output.empty() && (output.back() == '\n' || output.back() == ' ')) output.pop_back();
    return output;
}

Result<DropResult> ClipboardController::drop_files(const std::filesystem::path& document_path,
    std::span<const std::filesystem::path> files, bool copy_images_to_assets) {
    DropResult result;
    for (const auto& file : files) {
        if (ImageExtension(file)) {
            const auto inserted = images_.insert_file(document_path, file, copy_images_to_assets,
                                                       file.stem().string());
            if (inserted != ErrorCode::ok) return Result<DropResult>::failure(inserted);
            ++result.inserted_images;
        } else result.documents_to_open.push_back(file);
    }
    return Result<DropResult>::success(std::move(result));
}

ErrorCode ClipboardController::paste_bitmap(const std::filesystem::path& document_path,
                                             HBITMAP bitmap) {
    if (document_path.empty() || !bitmap) return ErrorCode::image_import_failed;
    const auto assets = fileio::AssetsDirectoryFor(document_path);
    std::error_code error; std::filesystem::create_directories(assets, error);
    if (error) return ErrorCode::image_import_failed;
    SYSTEMTIME now{}; GetLocalTime(&now);
    wchar_t base[64]{};
    swprintf_s(base, L"image_%04u%02u%02u_%02u%02u%02u", now.wYear, now.wMonth, now.wDay,
               now.wHour, now.wMinute, now.wSecond);
    auto target = assets / (std::wstring(base) + L".png");
    for (std::uint32_t suffix = 2; std::filesystem::exists(target); ++suffix)
        target = assets / (std::wstring(base) + L"_" + std::to_wstring(suffix) + L".png");
    const auto temporary = target.wstring() + L".tmp";
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    Microsoft::WRL::ComPtr<IWICBitmap> source;
    Microsoft::WRL::ComPtr<IWICStream> stream;
    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    UINT width{}, height{};
    WICPixelFormatGUID pixel_format{};
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapUseAlpha, &source)) ||
        FAILED(source->GetSize(&width, &height)) ||
        FAILED(source->GetPixelFormat(&pixel_format)) ||
        FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromFilename(temporary.c_str(), GENERIC_WRITE)) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(width, height)) ||
        FAILED(frame->SetPixelFormat(&pixel_format)) ||
        FAILED(frame->WriteSource(source.Get(), nullptr)) || FAILED(frame->Commit()) ||
        FAILED(encoder->Commit())) {
        DeleteFileW(temporary.c_str()); return ErrorCode::image_import_failed;
    }
    frame.Reset(); encoder.Reset(); stream.Reset(); source.Reset(); factory.Reset();
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str()); return ErrorCode::image_import_failed;
    }
    return images_.insert_reference(fileio::ImageMarkdownTarget(document_path, target), "图片");
}

}  // namespace markdownmay::editor
