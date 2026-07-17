#include "TiffHeightmap.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <thread>
#include <zlib.h>

namespace Level {
namespace Creation {

namespace {

uint16_t rd_u16(const uint8_t* p, bool be) {
    return be ? uint16_t((p[0] << 8) | p[1]) : uint16_t((p[1] << 8) | p[0]);
}

uint32_t rd_u32(const uint8_t* p, bool be) {
    return be ? (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
                    (uint32_t(p[2]) << 8) | p[3]
              : (uint32_t(p[3]) << 24) | (uint32_t(p[2]) << 16) |
                    (uint32_t(p[1]) << 8) | p[0];
}

struct IfdEntry {
    uint16_t tag = 0;
    uint16_t type = 0;
    uint32_t count = 0;
    const uint8_t* value = nullptr;   
};

size_t type_size(uint16_t type) {
    switch (type) {
        case 1: case 2: case 6: case 7: return 1;   
        case 3: case 8: return 2;                   
        case 4: case 9: case 11: return 4;          
        case 5: case 10: case 12: return 8;         
        default: return 0;
    }
}

uint32_t entry_uint(const IfdEntry& e, bool be, uint32_t index) {
    if (!e.value || index >= e.count) return 0;
    if (e.type == 3) return rd_u16(e.value + index * 2, be);
    if (e.type == 4) return rd_u32(e.value + index * 4, be);
    if (e.type == 1) return e.value[index];
    return 0;
}



bool lzw_decode(const uint8_t* src, size_t n, std::vector<uint8_t>& out) {
    struct Entry {
        int16_t prev;
        uint8_t byte;
    };
    std::vector<Entry> dict(4096);
    for (int i = 0; i < 256; ++i) dict[i] = {-1, (uint8_t)i};

    uint32_t acc = 0;
    int acc_bits = 0;
    size_t pos = 0;
    int width = 9;
    int next = 258;
    int prev_code = -1;
    std::vector<uint8_t> seq;
    seq.reserve(4096);

    auto read_code = [&]() -> int {
        while (acc_bits < width) {
            if (pos >= n) return 257;
            acc = (acc << 8) | src[pos++];
            acc_bits += 8;
        }
        acc_bits -= width;
        return int((acc >> acc_bits) & ((1u << width) - 1u));
    };
    auto expand = [&](int code) {
        seq.clear();
        while (code >= 0) {
            seq.push_back(dict[code].byte);
            code = dict[code].prev;
        }
        out.insert(out.end(), seq.rbegin(), seq.rend());
    };

    for (;;) {
        int code = read_code();
        if (code == 257) return true;
        if (code == 256) {
            next = 258;
            width = 9;
            prev_code = -1;
            continue;
        }
        if (prev_code < 0) {
            if (code > 255) return false;
            expand(code);
            prev_code = code;
            continue;
        }
        const size_t out_before = out.size();
        if (code < next) {
            expand(code);
        } else if (code == next) {
            expand(prev_code);
            out.push_back(out[out_before]);   
        } else {
            return false;
        }
        if (next < 4096) {
            dict[next] = {(int16_t)prev_code, out[out_before]};
            ++next;
            if (next == (1 << width) - 1 && width < 12) ++width;
        }
        prev_code = code;
    }
}

bool packbits_decode(const uint8_t* src, size_t n,
                     std::vector<uint8_t>& out) {
    size_t pos = 0;
    while (pos < n) {
        const int8_t c = (int8_t)src[pos++];
        if (c >= 0) {
            const size_t count = (size_t)c + 1;
            if (pos + count > n) return false;
            out.insert(out.end(), src + pos, src + pos + count);
            pos += count;
        } else if (c != -128) {
            if (pos >= n) return false;
            out.insert(out.end(), (size_t)(1 - c), src[pos++]);
        }
    }
    return true;
}

bool zlib_decode(const uint8_t* src, size_t n, std::vector<uint8_t>& out,
                 size_t expected) {
    out.resize(expected);
    uLongf dest_len = (uLongf)expected;
    if (uncompress(out.data(), &dest_len, src, (uLong)n) != Z_OK) {
        return false;
    }
    out.resize(dest_len);
    return true;
}

struct TiffInfo {
    uint32_t width = 0, height = 0;
    uint16_t bits = 0, compression = 1, spp = 1, sample_format = 1;
    uint16_t predictor = 1, planar = 1;
    uint32_t rows_per_strip = 0xFFFFFFFFu;
    uint32_t tile_w = 0, tile_h = 0;
    std::vector<uint32_t> chunk_offsets;   
    std::vector<uint32_t> chunk_counts;
    bool tiled = false;
};


void undo_predictor(uint8_t* row, uint32_t pixels, uint16_t spp,
                    uint16_t bits, bool be) {
    const uint32_t samples = pixels * spp;
    if (bits == 8) {
        for (uint32_t i = spp; i < samples; ++i) row[i] += row[i - spp];
    } else if (bits == 16) {
        for (uint32_t i = spp; i < samples; ++i) {
            const uint16_t prev = rd_u16(row + (i - spp) * 2, be);
            const uint16_t cur = rd_u16(row + i * 2, be);
            const uint16_t v = uint16_t(prev + cur);
            if (be) {
                row[i * 2] = uint8_t(v >> 8);
                row[i * 2 + 1] = uint8_t(v);
            } else {
                row[i * 2] = uint8_t(v);
                row[i * 2 + 1] = uint8_t(v >> 8);
            }
        }
    } else if (bits == 32) {
        for (uint32_t i = spp; i < samples; ++i) {
            const uint32_t prev = rd_u32(row + (i - spp) * 4, be);
            const uint32_t cur = rd_u32(row + i * 4, be);
            const uint32_t v = prev + cur;
            uint8_t* d = row + i * 4;
            if (be) {
                d[0] = uint8_t(v >> 24); d[1] = uint8_t(v >> 16);
                d[2] = uint8_t(v >> 8);  d[3] = uint8_t(v);
            } else {
                d[3] = uint8_t(v >> 24); d[2] = uint8_t(v >> 16);
                d[1] = uint8_t(v >> 8);  d[0] = uint8_t(v);
            }
        }
    }
}

float sample_to_float(const uint8_t* p, uint16_t bits,
                      uint16_t sample_format, bool be) {
    if (sample_format == 3) {
        const uint32_t u = rd_u32(p, be);
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    if (bits == 8) return float(*p);
    if (bits == 16) return float(rd_u16(p, be));
    return float(rd_u32(p, be));
}

}

bool LoadTiffHeightmap(const std::string& path, std::vector<float>& values,
                       int& width, int& height, std::string& error) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        
        
        for (int attempt = 0; attempt < 4 && !f; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            f.clear();
            f.open(path, std::ios::binary | std::ios::ate);
        }
    }
    if (!f) {
        error = "cannot open " + path +
                " (locked or still syncing from the cloud?)";
        return false;
    }
    const std::streamoff len = f.tellg();
    if (len < 8) {
        error = "not a TIFF file";
        return false;
    }
    std::vector<uint8_t> data((size_t)len);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(data.data()), len);
    if (!f) {
        error = "short read from " + path;
        return false;
    }

    const bool be = data[0] == 'M' && data[1] == 'M';
    if (!be && !(data[0] == 'I' && data[1] == 'I')) {
        error = "not a TIFF file (bad byte-order mark)";
        return false;
    }
    if (rd_u16(data.data() + 2, be) != 42) {
        error = "not a TIFF file (bad magic)";
        return false;
    }

    const uint32_t ifd_off = rd_u32(data.data() + 4, be);
    if (ifd_off + 2 > data.size()) {
        error = "TIFF IFD out of range";
        return false;
    }
    const uint16_t entry_count = rd_u16(data.data() + ifd_off, be);
    if (ifd_off + 2 + (size_t)entry_count * 12 > data.size()) {
        error = "TIFF IFD truncated";
        return false;
    }

    TiffInfo info;
    std::vector<uint32_t> tile_offsets, tile_counts;
    for (uint16_t i = 0; i < entry_count; ++i) {
        const uint8_t* e = data.data() + ifd_off + 2 + (size_t)i * 12;
        IfdEntry entry;
        entry.tag = rd_u16(e, be);
        entry.type = rd_u16(e + 2, be);
        entry.count = rd_u32(e + 4, be);
        const size_t bytes = type_size(entry.type) * entry.count;
        if (bytes == 0) continue;
        if (bytes <= 4) {
            entry.value = e + 8;
        } else {
            const uint32_t off = rd_u32(e + 8, be);
            if ((size_t)off + bytes > data.size()) continue;
            entry.value = data.data() + off;
        }

        auto read_list = [&](std::vector<uint32_t>& out_list) {
            out_list.resize(entry.count);
            for (uint32_t k = 0; k < entry.count; ++k) {
                out_list[k] = entry_uint(entry, be, k);
            }
        };

        switch (entry.tag) {
            case 256: info.width = entry_uint(entry, be, 0); break;
            case 257: info.height = entry_uint(entry, be, 0); break;
            case 258: info.bits = (uint16_t)entry_uint(entry, be, 0); break;
            case 259:
                info.compression = (uint16_t)entry_uint(entry, be, 0);
                break;
            case 273: read_list(info.chunk_offsets); break;
            case 277: info.spp = (uint16_t)entry_uint(entry, be, 0); break;
            case 278:
                info.rows_per_strip = entry_uint(entry, be, 0);
                break;
            case 279: read_list(info.chunk_counts); break;
            case 284: info.planar = (uint16_t)entry_uint(entry, be, 0); break;
            case 317:
                info.predictor = (uint16_t)entry_uint(entry, be, 0);
                break;
            case 322: info.tile_w = entry_uint(entry, be, 0); break;
            case 323: info.tile_h = entry_uint(entry, be, 0); break;
            case 324: read_list(tile_offsets); break;
            case 325: read_list(tile_counts); break;
            case 339:
                info.sample_format = (uint16_t)entry_uint(entry, be, 0);
                break;
            default: break;
        }
    }
    if (!tile_offsets.empty()) {
        info.tiled = true;
        info.chunk_offsets = tile_offsets;
        info.chunk_counts = tile_counts;
    }

    if (info.width < 2 || info.height < 2 || info.width > 16384 ||
        info.height > 16384) {
        error = "TIFF dimensions unsupported (" +
                std::to_string(info.width) + "x" +
                std::to_string(info.height) + ")";
        return false;
    }
    if (info.bits != 8 && info.bits != 16 && info.bits != 32) {
        error = "only 8/16/32-bit TIFF samples are supported (file has " +
                std::to_string(info.bits) + "-bit)";
        return false;
    }
    if (info.sample_format == 3 && info.bits != 32) {
        error = "float TIFF samples must be 32-bit";
        return false;
    }
    if (info.planar != 1) {
        error = "planar TIFF layout is not supported";
        return false;
    }
    if (info.predictor == 3) {
        error = "floating-point TIFF predictor is not supported";
        return false;
    }
    if (info.chunk_offsets.empty() ||
        info.chunk_counts.size() != info.chunk_offsets.size()) {
        error = "TIFF strip/tile tables missing";
        return false;
    }

    const size_t bytes_per_sample = info.bits / 8;
    const size_t pixel_stride = bytes_per_sample * info.spp;

    const uint32_t chunk_w = info.tiled ? info.tile_w : info.width;
    const uint32_t chunk_h =
        info.tiled ? info.tile_h
                   : std::min<uint32_t>(info.rows_per_strip, info.height);
    if (chunk_w == 0 || chunk_h == 0) {
        error = "TIFF chunk geometry invalid";
        return false;
    }
    const size_t chunk_row_bytes = (size_t)chunk_w * pixel_stride;
    const uint32_t across =
        info.tiled ? (info.width + chunk_w - 1) / chunk_w : 1;

    std::vector<uint8_t> assembled((size_t)info.width * info.height *
                                   pixel_stride);
    std::vector<uint8_t> chunk;
    for (size_t c = 0; c < info.chunk_offsets.size(); ++c) {
        const uint32_t off = info.chunk_offsets[c];
        const uint32_t cnt = info.chunk_counts[c];
        if ((size_t)off + cnt > data.size()) {
            error = "TIFF chunk " + std::to_string(c) + " out of range";
            return false;
        }
        const uint32_t chunk_row0 =
            info.tiled ? (uint32_t)(c / across) * chunk_h
                       : (uint32_t)c * chunk_h;
        const uint32_t chunk_col0 =
            info.tiled ? (uint32_t)(c % across) * chunk_w : 0;
        if (chunk_row0 >= info.height) break;
        const uint32_t rows_here =
            info.tiled ? chunk_h
                       : std::min<uint32_t>(chunk_h,
                                            info.height - chunk_row0);
        const size_t expected = chunk_row_bytes * rows_here;

        chunk.clear();
        bool ok = false;
        switch (info.compression) {
            case 1:
                chunk.assign(data.begin() + off, data.begin() + off + cnt);
                ok = chunk.size() >= expected;
                break;
            case 5:
                chunk.reserve(expected);
                ok = lzw_decode(data.data() + off, cnt, chunk) &&
                     chunk.size() >= expected;
                break;
            case 8:
            case 32946:
                ok = zlib_decode(data.data() + off, cnt, chunk, expected) &&
                     chunk.size() >= expected;
                break;
            case 32773:
                chunk.reserve(expected);
                ok = packbits_decode(data.data() + off, cnt, chunk) &&
                     chunk.size() >= expected;
                break;
            default:
                error = "unsupported TIFF compression " +
                        std::to_string(info.compression) +
                        " (use none, LZW, Deflate or PackBits)";
                return false;
        }
        if (!ok) {
            error = "TIFF chunk " + std::to_string(c) + " failed to decode";
            return false;
        }

        for (uint32_t r = 0; r < rows_here; ++r) {
            uint8_t* row = chunk.data() + (size_t)r * chunk_row_bytes;
            if (info.predictor == 2) {
                undo_predictor(row, chunk_w, info.spp, info.bits, be);
            }
            const uint32_t y = chunk_row0 + r;
            if (y >= info.height) break;
            const uint32_t cols =
                std::min<uint32_t>(chunk_w, info.width - chunk_col0);
            std::memcpy(assembled.data() +
                            ((size_t)y * info.width + chunk_col0) *
                                pixel_stride,
                        row, (size_t)cols * pixel_stride);
        }
    }

    
    const size_t pixels = (size_t)info.width * info.height;
    values.resize(pixels);
    if (info.sample_format == 3) {
        float lo = std::numeric_limits<float>::infinity();
        float hi = -std::numeric_limits<float>::infinity();
        for (size_t i = 0; i < pixels; ++i) {
            const float v = sample_to_float(
                assembled.data() + i * pixel_stride, info.bits, 3, be);
            values[i] = v;
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        const float span = hi - lo;
        for (size_t i = 0; i < pixels; ++i) {
            values[i] = span > 0.0f ? (values[i] - lo) / span : 0.0f;
        }
    } else {
        const float scale =
            info.bits == 8 ? 255.0f
                           : (info.bits == 16 ? 65535.0f : 4294967295.0f);
        for (size_t i = 0; i < pixels; ++i) {
            values[i] = sample_to_float(assembled.data() + i * pixel_stride,
                                        info.bits, info.sample_format, be) /
                        scale;
        }
    }

    width = (int)info.width;
    height = (int)info.height;
    return true;
}

}
}
