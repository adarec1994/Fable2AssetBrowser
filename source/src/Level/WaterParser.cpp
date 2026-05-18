#include "WaterParser.h"

#include <cmath>
#include <cstring>

namespace Level {

namespace {

constexpr uint32_t kTileMagic = 0x00000FECu;

uint32_t read_u32_be(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) <<  8) |  uint32_t(p[3]);
}

float read_f32_be(const uint8_t* p) {
    uint32_t u = read_u32_be(p);
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

// Parse a single body starting at `off` (which should point to the body's
// leading 0x0FEC marker). Returns true if at least the header + path were
// recovered. `end` bounds the body; any tile loop terminates when the
// next 0x0FEC isn't followed by a plausible tile bound block.
bool parse_body(const std::vector<uint8_t>& bytes,
                size_t off,
                size_t end,
                WaterBody& out)
{
    if (off + 8 > end) return false;

    const uint8_t* p = bytes.data();

    const uint32_t magic = read_u32_be(p + off);
    if (magic != kTileMagic) return false;
    off += 4;

    // Reserved u32 (always observed as 0).
    off += 4;

    // 40 wave-param floats follow the reserved u32.
    constexpr size_t kWaveFloats = 40;
    if (off + kWaveFloats * 4 > end) return false;
    for (size_t i = 0; i < kWaveFloats; ++i) {
        out.wave_params[i] = read_f32_be(p + off + i * 4);
    }
    off += kWaveFloats * 4;

    // Base water surface Y.
    if (off + 4 > end) return false;
    out.base_height = read_f32_be(p + off);
    off += 4;

    // ASCIIZ normal-map texture path.
    size_t str_start = off;
    while (off < end && bytes[off] != 0) ++off;
    if (off >= end) return false;
    out.normal_map_path.assign(
        reinterpret_cast<const char*>(p + str_start), off - str_start);
    ++off;  // skip null terminator

    // Padding bytes to align to the next 4-byte boundary of the file. The
    // file as observed isn't strictly 4-aligned here — it pads up to the
    // byte that introduces the next 0x0FEC marker. Scan forward up to a
    // small distance looking for a 0x0FEC u32.
    bool tile_loop_started = false;
    for (size_t scan = off; scan < off + 8 && scan + 4 <= end; ++scan) {
        if (read_u32_be(p + scan) == kTileMagic) {
            off = scan;
            tile_loop_started = true;
            break;
        }
    }
    if (!tile_loop_started) {
        // No tiles in this body — that's allowed, treat as success so the
        // caller can move on to the next body.
        return true;
    }

    // Tile loop.
    while (off + 0x30 <= end) {
        const uint32_t start_magic = read_u32_be(p + off);
        if (start_magic != kTileMagic) break;

        WaterTile tile;
        tile.cx    = read_f32_be(p + off + 0x04);
        tile.cz    = read_f32_be(p + off + 0x08);
        tile.ex    = read_f32_be(p + off + 0x0C);
        tile.ez    = read_f32_be(p + off + 0x10);
        tile.h_min = read_f32_be(p + off + 0x14);
        tile.h_max = read_f32_be(p + off + 0x18);
        tile.flags[0] = read_u32_be(p + off + 0x1C);
        tile.flags[1] = read_u32_be(p + off + 0x20);
        tile.flags[2] = read_u32_be(p + off + 0x24);
        tile.flags[3] = read_u32_be(p + off + 0x28);  // mask byte count

        const uint32_t mask_count = tile.flags[3];

        // Plausibility guard — tiles with absurd extents or mask sizes
        // means we've stepped into the terminator/next body block. Bail
        // out and let the caller move on.
        const bool bounds_sane =
            std::isfinite(tile.cx) && std::isfinite(tile.cz) &&
            std::isfinite(tile.ex) && std::isfinite(tile.ez) &&
            tile.ex > 0.0f && tile.ex < 5000.0f &&
            tile.ez > 0.0f && tile.ez < 5000.0f;
        if (!bounds_sane || mask_count > 65536u) break;

        const size_t mask_off = off + 0x2C;
        if (mask_off + mask_count > end) break;
        tile.mask.assign(p + mask_off, p + mask_off + mask_count);

        const size_t end_off = mask_off + mask_count;
        if (end_off + 4 > end) break;
        const uint32_t end_magic = read_u32_be(p + end_off);
        if (end_magic != kTileMagic) break;

        out.tiles.push_back(std::move(tile));
        off = end_off + 4;
    }

    return !out.tiles.empty();
}

}  // namespace

bool ParseWaterFile(const std::vector<uint8_t>& bytes, WaterScene& out)
{
    out = WaterScene{};

    // Global header: 5 u32s (version, body_count, total_tile_count,
    // body1_offset, body2_offset). Following bodies start at offsets
    // listed sequentially; body 0 always starts immediately after the
    // header (offset 0x14).
    if (bytes.size() < 0x14) return false;

    const uint8_t* p = bytes.data();
    out.version    = read_u32_be(p + 0x00);
    out.body_count = read_u32_be(p + 0x04);
    out.tile_count = read_u32_be(p + 0x08);

    if (out.body_count == 0 || out.body_count > 64) return false;

    // Collect body offsets. Offsets for bodies 1..N-1 are stored as u32s
    // at file offsets 0x0C, 0x10, ... (one slot per "extra" body). Body 0
    // starts at 0x14 (the byte just past the header table) regardless.
    std::vector<size_t> body_offsets;
    body_offsets.reserve(out.body_count);

    const size_t header_table_end =
        0x0C + (out.body_count > 1 ? (out.body_count - 1) * 4 : 0);
    if (header_table_end > bytes.size()) return false;

    body_offsets.push_back(header_table_end);
    for (uint32_t i = 1; i < out.body_count; ++i) {
        const size_t off = 0x0C + (i - 1) * 4;
        const uint32_t v = read_u32_be(p + off);
        if (v >= bytes.size()) return false;
        body_offsets.push_back(v);
    }

    out.bodies.resize(out.body_count);
    for (uint32_t i = 0; i < out.body_count; ++i) {
        const size_t start = body_offsets[i];
        const size_t end   = (i + 1 < out.body_count) ? body_offsets[i + 1]
                                                      : bytes.size();
        parse_body(bytes, start, end, out.bodies[i]);
    }

    // Trim any all-empty bodies at the back just in case.
    while (!out.bodies.empty() && out.bodies.back().tiles.empty() &&
           out.bodies.back().normal_map_path.empty())
    {
        out.bodies.pop_back();
    }

    return !out.bodies.empty();
}

}  // namespace Level
