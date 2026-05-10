// TIFF writer — baseline little-endian uncompressed RGBA8. Hand-rolled
// because stb_image_write doesn't ship a TIFF backend and pulling in
// libtiff for a one-shot save would be overkill. The format is simple
// enough that ~80 lines does the job. See:
//   https://www.adobe.io/open/standards/TIFF.html (TIFF6 spec)
//
// Layout we produce:
//   bytes 0..7      = TIFF header (II, magic 42, IFD offset = 8)
//   bytes 8..145    = IFD: 2-byte entry count + 11 × 12-byte entries
//                          + 4-byte next-IFD offset (= 0)
//   bytes 146..153  = BitsPerSample[4] = {8,8,8,8} (out-of-line)
//   bytes 154..end  = pixel data (top-down rows, RGBA)
//
// Tags emitted (sorted ascending — TIFF requires it):
//   256 ImageWidth, 257 ImageLength, 258 BitsPerSample,
//   259 Compression=1, 262 PhotometricInterpretation=2 (RGB),
//   273 StripOffsets, 277 SamplesPerPixel=4, 278 RowsPerStrip,
//   279 StripByteCounts, 284 PlanarConfiguration=1, 338 ExtraSamples=2.

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

    // ---- 8-byte header ----
    put_u16(0x4949);   // "II" — little-endian byte order
    put_u16(42);       // TIFF magic
    put_u32(8);        // offset to first IFD (immediately after header)

    // ---- Layout offsets ----
    constexpr uint16_t kNumEntries = 11;
    // IFD size = 2 (count) + N*12 (entries) + 4 (next-offset)
    constexpr uint32_t kIfdSize        = 2 + kNumEntries * 12 + 4;     // 138
    constexpr uint32_t kHeaderSize     = 8;
    constexpr uint32_t kBpsOffset      = kHeaderSize + kIfdSize;       // 146
    constexpr uint32_t kBpsBytes       = 4 * 2;                         // 8 (4 SHORTs)
    const     uint32_t kImageDataOff   = kBpsOffset + kBpsBytes;        // 154
    const     uint32_t kImageBytes     = (uint32_t)w * (uint32_t)h * 4;

    // ---- IFD ----
    put_u16(kNumEntries);

    auto put_entry_long = [&](uint16_t tag, uint32_t val) {
        // type=4 (LONG), count=1, value packed in 4 bytes
        put_u16(tag);
        put_u16(4);
        put_u32(1);
        put_u32(val);
    };
    auto put_entry_short = [&](uint16_t tag, uint16_t val) {
        // type=3 (SHORT), count=1, value left-justified in 4-byte field.
        // For LE TIFF, writing the SHORT in the low 2 bytes of a u32
        // places it at file bytes 0..1 of the value field — that's the
        // "left-justified" position the spec wants.
        put_u16(tag);
        put_u16(3);
        put_u32(1);
        put_u32((uint32_t)val);
    };

    put_entry_long (256, (uint32_t)w);            // ImageWidth
    put_entry_long (257, (uint32_t)h);            // ImageLength
    // BitsPerSample: type=SHORT, count=4. Doesn't fit inline → offset.
    put_u16(258); put_u16(3); put_u32(4); put_u32(kBpsOffset);
    put_entry_short(259, 1);                       // Compression: none
    put_entry_short(262, 2);                       // PhotometricInterpretation: RGB
    put_entry_long (273, kImageDataOff);           // StripOffsets
    put_entry_short(277, 4);                       // SamplesPerPixel
    put_entry_long (278, (uint32_t)h);             // RowsPerStrip
    put_entry_long (279, kImageBytes);             // StripByteCounts
    put_entry_short(284, 1);                       // PlanarConfiguration: chunky
    put_entry_short(338, 2);                       // ExtraSamples: unassociated alpha

    put_u32(0);                                    // next IFD offset (none)

    // ---- BitsPerSample[4] data ----
    put_u16(8); put_u16(8); put_u16(8); put_u16(8);

    // ---- Pixel data ----
    f.write(reinterpret_cast<const char*>(rgba), (std::streamsize)kImageBytes);

    return f.good();
}
