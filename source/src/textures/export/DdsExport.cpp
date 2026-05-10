// DDS writer — uncompressed 32-bpp ARGB. Hand-rolled because we're
// emitting a single-mip uncompressed image, which is the simplest
// possible DDS variant and doesn't justify a third-party dependency.
//
// On-disk layout (128 bytes total before pixel data):
//   bytes 0..3    = 'DDS ' magic
//   bytes 4..127  = DDS_HEADER (124 bytes, sizeof field reads as 124)
// Then `w*h*4` bytes of pixel data, A8 R8 G8 B8 per pixel as the
// default DDS RGBA channel layout (R=lowest byte, A=highest).

#include "TextureExport.h"

#include <fstream>
#include <cstdint>
#include <cstring>

namespace {

// Flag constants — duplicated here so we don't pull in the full DXGI
// / d3dx_dxgi headers just for four #defines.
constexpr uint32_t kDdsdCaps        = 0x00000001;
constexpr uint32_t kDdsdHeight      = 0x00000002;
constexpr uint32_t kDdsdWidth       = 0x00000004;
constexpr uint32_t kDdsdPitch       = 0x00000008;
constexpr uint32_t kDdsdPixelFormat = 0x00001000;
constexpr uint32_t kDdpfAlphaPixels = 0x00000001;
constexpr uint32_t kDdpfRgb         = 0x00000040;
constexpr uint32_t kDdscapsTexture  = 0x00001000;

#pragma pack(push, 1)
struct DdsPixelFormat {
    uint32_t dwSize;
    uint32_t dwFlags;
    uint32_t dwFourCC;
    uint32_t dwRGBBitCount;
    uint32_t dwRBitMask;
    uint32_t dwGBitMask;
    uint32_t dwBBitMask;
    uint32_t dwABitMask;
};
struct DdsHeader {
    uint32_t        dwSize;
    uint32_t        dwFlags;
    uint32_t        dwHeight;
    uint32_t        dwWidth;
    uint32_t        dwPitchOrLinearSize;
    uint32_t        dwDepth;
    uint32_t        dwMipMapCount;
    uint32_t        dwReserved1[11];
    DdsPixelFormat  ddspf;
    uint32_t        dwCaps;
    uint32_t        dwCaps2;
    uint32_t        dwCaps3;
    uint32_t        dwCaps4;
    uint32_t        dwReserved2;
};
#pragma pack(pop)
static_assert(sizeof(DdsPixelFormat) == 32, "DdsPixelFormat must be 32 bytes");
static_assert(sizeof(DdsHeader)      == 124, "DdsHeader must be 124 bytes");

} // anonymous

bool tex_export_dds(const std::string& path, const uint8_t* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return false;

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;

    DdsHeader hdr{};
    hdr.dwSize  = 124;
    hdr.dwFlags = kDdsdCaps | kDdsdHeight | kDdsdWidth
                | kDdsdPixelFormat | kDdsdPitch;
    hdr.dwHeight = (uint32_t)h;
    hdr.dwWidth  = (uint32_t)w;
    hdr.dwPitchOrLinearSize = (uint32_t)(w * 4);
    hdr.dwDepth = 0;
    hdr.dwMipMapCount = 0;
    std::memset(hdr.dwReserved1, 0, sizeof(hdr.dwReserved1));

    hdr.ddspf.dwSize        = 32;
    hdr.ddspf.dwFlags       = kDdpfRgb | kDdpfAlphaPixels;
    hdr.ddspf.dwFourCC      = 0;
    hdr.ddspf.dwRGBBitCount = 32;
    // RGBA in memory order (byte 0 = R, byte 3 = A). DDS writes masks
    // as the value of each channel in a packed DWORD, little-endian —
    // R goes in the LSB.
    hdr.ddspf.dwRBitMask    = 0x000000FFu;
    hdr.ddspf.dwGBitMask    = 0x0000FF00u;
    hdr.ddspf.dwBBitMask    = 0x00FF0000u;
    hdr.ddspf.dwABitMask    = 0xFF000000u;

    hdr.dwCaps = kDdscapsTexture;

    f.write("DDS ", 4);
    f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    f.write(reinterpret_cast<const char*>(rgba), (std::streamsize)((size_t)w * (size_t)h * 4));

    return f.good();
}
