#include "WaterParser.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace Level {

namespace {

constexpr uint32_t kTileMagic = 0x00000FECu;
constexpr uint32_t kMaxBodyCount = 64;
constexpr uint32_t kMaxDimCount = 8;
constexpr uint32_t kMaxMaskBytes = 65536;
constexpr size_t kBodyParamFloats = 38;
constexpr uint32_t kMaxTileCount = 4096;

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

bool finite_extent(float v) {
    return std::isfinite(v) && v > 0.0f && v < 5000.0f;
}

bool parse_body(const std::vector<uint8_t>& bytes,
                size_t off,
                size_t end,
                WaterBody& out)
{
    if (off + 8 + kBodyParamFloats * 4 > end) return false;

    const uint8_t* p = bytes.data();

    if (read_u32_be(p + off) != kTileMagic) return false;
    off += 4;

    off += 4;

    for (size_t i = 0; i < kBodyParamFloats; ++i) {
        out.wave_params[i] = read_f32_be(p + off + i * 4);
    }
    out.base_height = std::isfinite(out.wave_params[0])
        ? out.wave_params[0]
        : 0.0f;
    off += kBodyParamFloats * 4;

    auto read_cstring = [&](std::string& dst) -> bool {
        const size_t str_start = off;
        while (off < end && bytes[off] != 0) ++off;
        if (off >= end) return false;
        dst.assign(reinterpret_cast<const char*>(p + str_start),
                   off - str_start);
        ++off;
        return true;
    };

    if (!read_cstring(out.normal_map_path)) return false;
    const size_t after_first_path = off;

    // IDA shows two HString paths after the fixed water body block. Some
    // dumps appear to omit the second path, so only consume it when the next
    // bytes do not already look like "tile count, tile marker".
    const bool next_is_count_then_tile =
        off + 8 <= end &&
        read_u32_be(p + off) <= kMaxTileCount &&
        read_u32_be(p + off + 4) == kTileMagic;
    if (!next_is_count_then_tile) {
        if (!read_cstring(out.secondary_map_path)) {
            out.secondary_map_path.clear();
            off = after_first_path;
        }
    }

    bool tile_loop_started = false;
    if (off + 4 <= end) {
        const uint32_t declared = read_u32_be(p + off);
        if (declared <= kMaxTileCount) {
            out.declared_tile_count = declared;
            off += 4;
            if (off + 4 <= end && read_u32_be(p + off) == kTileMagic) {
                tile_loop_started = true;
            }
        }
    }

    if (!tile_loop_started) {
        for (size_t scan = off; scan < off + 32 && scan + 4 <= end; ++scan) {
            if (read_u32_be(p + scan) == kTileMagic) {
                if (scan >= 4) {
                    const uint32_t declared = read_u32_be(p + scan - 4);
                    if (declared <= kMaxTileCount) {
                        out.declared_tile_count = declared;
                    }
                }
                off = scan;
                tile_loop_started = true;
                break;
            }
        }
    }
    if (!tile_loop_started && after_first_path != off) {
        off = after_first_path;
        for (size_t scan = off; scan < off + 32 && scan + 4 <= end; ++scan) {
            if (read_u32_be(p + scan) == kTileMagic) {
                if (scan >= 4) {
                    const uint32_t declared = read_u32_be(p + scan - 4);
                    if (declared <= kMaxTileCount) {
                        out.declared_tile_count = declared;
                    }
                }
                off = scan;
                tile_loop_started = true;
                break;
            }
        }
    }
    if (!tile_loop_started) return true;

    while (off + 0x24 <= end) {
        if (out.declared_tile_count != 0 &&
            out.tiles.size() >= out.declared_tile_count) {
            break;
        }

        if (read_u32_be(p + off) != kTileMagic) break;

        WaterTile tile;
        tile.cx    = read_f32_be(p + off + 0x04);
        tile.cz    = read_f32_be(p + off + 0x08);
        tile.ex    = read_f32_be(p + off + 0x0C);
        tile.ez    = read_f32_be(p + off + 0x10);
        tile.h_min = read_f32_be(p + off + 0x14);
        tile.h_max = read_f32_be(p + off + 0x18);

        const bool bounds_sane =
            std::isfinite(tile.cx) && std::isfinite(tile.cz) &&
            finite_extent(tile.ex) && finite_extent(tile.ez) &&
            std::isfinite(tile.h_min) && std::isfinite(tile.h_max);
        if (!bounds_sane) break;

        const uint32_t dim_count = read_u32_be(p + off + 0x1C);
        if (dim_count > kMaxDimCount) break;

        const size_t dims_off = off + 0x20;
        const size_t mask_count_off = dims_off + size_t(dim_count) * 4;
        if (mask_count_off + 4 > end) break;

        tile.dims.reserve(dim_count);
        for (uint32_t i = 0; i < dim_count; ++i) {
            tile.dims.push_back(read_u32_be(p + dims_off + size_t(i) * 4));
        }

        const uint32_t mask_count = read_u32_be(p + mask_count_off);
        if (mask_count > kMaxMaskBytes) break;

        const size_t mask_off = mask_count_off + 4;
        const size_t end_off = mask_off + mask_count;
        if (end_off + 4 > end) break;

        tile.mask.assign(p + mask_off, p + end_off);
        if (read_u32_be(p + end_off) != kTileMagic) break;

        out.tiles.push_back(std::move(tile));
        off = end_off + 4;
    }

    return !out.normal_map_path.empty();
}

}

bool ParseWaterFile(const std::vector<uint8_t>& bytes, WaterScene& out)
{
    out = WaterScene{};

    if (bytes.size() < 0x0C) return false;

    const uint8_t* p = bytes.data();
    out.version    = read_u32_be(p + 0x00);
    out.body_count = read_u32_be(p + 0x04);

    if (out.body_count == 0 || out.body_count > kMaxBodyCount) return false;

    const size_t table_end = 0x08 + size_t(out.body_count) * 4;
    if (table_end > bytes.size()) return false;

    std::vector<size_t> body_offsets;
    body_offsets.reserve(out.body_count);
    for (uint32_t i = 0; i < out.body_count; ++i) {
        const uint32_t v = read_u32_be(p + 0x08 + size_t(i) * 4);
        if (v < table_end || v >= bytes.size()) return false;
        if (!body_offsets.empty() && v <= body_offsets.back()) return false;
        body_offsets.push_back(v);
    }

    out.bodies.reserve(out.body_count);
    for (uint32_t i = 0; i < out.body_count; ++i) {
        const size_t start = body_offsets[i];
        const size_t end   = (i + 1 < out.body_count) ? body_offsets[i + 1]
                                                      : bytes.size();
        WaterBody body;
        if (parse_body(bytes, start, end, body)) {
            out.tile_count += uint32_t(body.tiles.size());
            out.bodies.push_back(std::move(body));
        }
    }

    return out.tile_count != 0;
}

}
