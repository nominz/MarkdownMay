#include "image_probe.hpp"

#include <windows.h>
#include <objbase.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>

int main() {
    const HRESULT com_result =
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com_result)) {
        return 1;
    }

    constexpr std::array<std::uint8_t, 58> bitmap{
        0x42, 0x4d, 0x3a, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00,
        0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x13, 0x0b,
        0x00, 0x00, 0x13, 0x0b, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
        0xff, 0x00};

    const auto path =
        std::filesystem::temp_directory_path() /
        "markdownmay-image-probe.bmp";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(
            reinterpret_cast<const char*>(bitmap.data()),
            static_cast<std::streamsize>(bitmap.size()));
        if (!output.good()) {
            CoUninitialize();
            return 2;
        }
    }

    markdownmay::prototype::ImageInformation information;
    const bool decoded =
        markdownmay::prototype::ProbeLocalImage(path, information);

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    CoUninitialize();

    if (!decoded) {
        return 3;
    }
    if (information.width != 1 || information.height != 1) {
        return 4;
    }
    return 0;
}
