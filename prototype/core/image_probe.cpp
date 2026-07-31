#include "image_probe.hpp"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace markdownmay::prototype {

bool ProbeLocalImage(
    const std::filesystem::path& path,
    ImageInformation& information) noexcept {
    information = {};

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    HRESULT result = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(result)) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    result = factory->CreateDecoderFromFilename(
        path.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        &decoder);
    if (FAILED(result)) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    result = decoder->GetFrame(0, &frame);
    if (FAILED(result)) {
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    result = frame->GetSize(&width, &height);
    if (FAILED(result) || width == 0 || height == 0) {
        return false;
    }

    constexpr std::uint64_t kMaximumPixels = 100'000'000ULL;
    if (static_cast<std::uint64_t>(width) * height > kMaximumPixels) {
        return false;
    }

    information.width = width;
    information.height = height;
    return true;
}

}  // namespace markdownmay::prototype
