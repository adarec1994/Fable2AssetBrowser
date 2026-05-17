#include "TextureExport.h"

#include <fstream>
#include <cstdint>

bool tex_export_tiff(const std::string& path, const uint8_t* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return false;

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;

    auto put_u16 = [&](uint16_t v) {
        char b[2] = { (char)(v & 0xFF), (char)((v >> 8) & 0xFF) };
        f.write(b, 2);
    };
    auto put_u32 = [&](uint32_t v) {
        char b[4] = {
            (char)(v & 0xFF), (char)((v >> 8) & 0xFF),
            (char)((v >> 16) & 0xFF), (char)((v >> 24) & 0xFF)
        };
        f.write(b, 4);
    };

    put_u16(0x4949);
    put_u16(42);
    put_u32(8);

    constexpr uint16_t kNumEntries = 11;

    constexpr uint32_t kIfdSize        = 2 + kNumEntries * 12 + 4;
    constexpr uint32_t kHeaderSize     = 8;
    constexpr uint32_t kBpsOffset      = kHeaderSize + kIfdSize;
    constexpr uint32_t kBpsBytes       = 4 * 2;
    const     uint32_t kImageDataOff   = kBpsOffset + kBpsBytes;
    const     uint32_t kImageBytes     = (uint32_t)w * (uint32_t)h * 4;

    put_u16(kNumEntries);

    auto put_entry_long = [&](uint16_t tag, uint32_t val) {

        put_u16(tag);
        put_u16(4);
        put_u32(1);
        put_u32(val);
    };
    auto put_entry_short = [&](uint16_t tag, uint16_t val) {

        put_u16(tag);
        put_u16(3);
        put_u32(1);
        put_u32((uint32_t)val);
    };

    put_entry_long (256, (uint32_t)w);
    put_entry_long (257, (uint32_t)h);

    put_u16(258); put_u16(3); put_u32(4); put_u32(kBpsOffset);
    put_entry_short(259, 1);
    put_entry_short(262, 2);
    put_entry_long (273, kImageDataOff);
    put_entry_short(277, 4);
    put_entry_long (278, (uint32_t)h);
    put_entry_long (279, kImageBytes);
    put_entry_short(284, 1);
    put_entry_short(338, 2);

    put_u32(0);

    put_u16(8); put_u16(8); put_u16(8); put_u16(8);

    f.write(reinterpret_cast<const char*>(rgba), (std::streamsize)kImageBytes);

    return f.good();
}
