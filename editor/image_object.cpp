#include "markdownmay/editor/image_object.hpp"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>

namespace markdownmay::editor {

ImageObject LoadImageObject(const std::filesystem::path& document_path,
                            std::string target, std::string alternative,
                            std::uint16_t display_percent) noexcept {
    ImageObject result;
    result.reference = fileio::ResolveImageReference(document_path, std::move(target));
    result.alternative = std::move(alternative);
    result.display_percent = (std::clamp)(display_percent, std::uint16_t{10}, std::uint16_t{300});
    if (result.reference.kind == fileio::ImageLocationKind::remote) {
        result.state = ImageDisplayState::remote_blocked;
        return result;
    }
    if (result.reference.kind == fileio::ImageLocationKind::missing) {
        result.state = ImageDisplayState::missing;
        return result;
    }
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateDecoderFromFilename(result.reference.resolved_path.c_str(), nullptr,
            GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame))) {
        result.state = ImageDisplayState::decode_failed;
        return result;
    }
    UINT width{}, height{};
    if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0 ||
        static_cast<std::uint64_t>(width) * height > 100'000'000ULL) {
        result.state = ImageDisplayState::decode_failed;
        return result;
    }
    result.pixel_width = width;
    result.pixel_height = height;
    result.state = ImageDisplayState::ready;
    return result;
}

}  // namespace markdownmay::editor
