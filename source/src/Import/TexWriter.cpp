#include "TexWriter.h"
#include "Xbox360Tiling.h"
#include "textures/LhTexCodec.h"

#define STB_DXT_IMPLEMENTATION
#include "stb_dxt.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace TexWriter {

namespace {

constexpr uint32_t kHeaderSign = 0xFFFFFFFEu;
constexpr uint32_t kHeaderUnknown0 = 0u;
constexpr size_t   kHeaderEntrySize = 84u;

constexpr uint32_t kPfBC1  = 35u;
constexpr uint32_t kPfBC3  = 39u;
constexpr uint32_t kPfBC5  = 40u;
constexpr uint32_t kPfARGB = 2u;

constexpr uint32_t kCompFlagRaw = 7u;

constexpr int kMinMipDim = 16;

uint32_t u1_flags_for_pf(uint32_t pf) {
    switch (pf) {
        case kPfBC1:  return 0x10u;
        case kPfBC3:  return 0xF0u;
        case kPfBC5:  return 0x110u;
        default:      return 0x10u;
    }
}

inline void put32be(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x >> 24));
    v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 8));
    v.push_back((uint8_t)x);
}

inline int next_pow2_le(int v) {
    int p = 1;
    while ((p << 1) <= v) p <<= 1;
    return p;
}

void resample_rgba(const uint8_t* src, int sw, int sh,
                   std::vector<uint8_t>& dst, int dw, int dh)
{
    dst.assign((size_t)dw * dh * 4, 0);
    for (int y = 0; y < dh; ++y) {
        float fy = (dh > 1) ? ((float)y + 0.5f) * sh / dh - 0.5f : 0.0f;
        int y0 = (int)std::floor(fy);
        float ty = fy - y0;
        int y1 = std::min(y0 + 1, sh - 1);
        y0 = std::max(y0, 0);
        for (int x = 0; x < dw; ++x) {
            float fx = (dw > 1) ? ((float)x + 0.5f) * sw / dw - 0.5f : 0.0f;
            int x0 = (int)std::floor(fx);
            float tx = fx - x0;
            int x1 = std::min(x0 + 1, sw - 1);
            x0 = std::max(x0, 0);
            uint8_t* o = dst.data() + ((size_t)y * dw + x) * 4;
            for (int c = 0; c < 4; ++c) {
                float a = src[((size_t)y0 * sw + x0) * 4 + c] * (1 - tx) +
                          src[((size_t)y0 * sw + x1) * 4 + c] * tx;
                float b = src[((size_t)y1 * sw + x0) * 4 + c] * (1 - tx) +
                          src[((size_t)y1 * sw + x1) * 4 + c] * tx;
                float r = a * (1 - ty) + b * ty;
                o[c] = (uint8_t)std::min(255.0f, std::max(0.0f, r + 0.5f));
            }
        }
    }
}

void downsample_half(const std::vector<uint8_t>& src, int sw, int sh,
                     std::vector<uint8_t>& dst, int& dw, int& dh)
{
    dw = std::max(1, sw / 2);
    dh = std::max(1, sh / 2);
    dst.assign((size_t)dw * dh * 4, 0);
    for (int y = 0; y < dh; ++y) {
        int y0 = std::min(y * 2, sh - 1);
        int y1 = std::min(y * 2 + 1, sh - 1);
        for (int x = 0; x < dw; ++x) {
            int x0 = std::min(x * 2, sw - 1);
            int x1 = std::min(x * 2 + 1, sw - 1);
            uint8_t* o = dst.data() + ((size_t)y * dw + x) * 4;
            for (int c = 0; c < 4; ++c) {
                int s = src[((size_t)y0 * sw + x0) * 4 + c] +
                        src[((size_t)y0 * sw + x1) * 4 + c] +
                        src[((size_t)y1 * sw + x0) * 4 + c] +
                        src[((size_t)y1 * sw + x1) * 4 + c];
                o[c] = (uint8_t)((s + 2) / 4);
            }
        }
    }
}

void gather_block_rgba(const uint8_t* rgba, int w, int h, int bx, int by,
                       uint8_t out[64])
{
    for (int py = 0; py < 4; ++py) {
        int yy = std::min(by * 4 + py, h - 1);
        for (int px = 0; px < 4; ++px) {
            int xx = std::min(bx * 4 + px, w - 1);
            std::memcpy(out + (py * 4 + px) * 4,
                        rgba + ((size_t)yy * w + xx) * 4, 4);
        }
    }
}

void build_mip_payload(const uint8_t* rgba, int w, int h, Format fmt,
                       std::vector<uint8_t>& payload)
{
    if (fmt == Format::RawARGB) {
        payload.resize((size_t)w * h * 4);
        for (size_t i = 0, n = (size_t)w * h; i < n; ++i) {
            payload[i * 4 + 0] = rgba[i * 4 + 3];
            payload[i * 4 + 1] = rgba[i * 4 + 0];
            payload[i * 4 + 2] = rgba[i * 4 + 1];
            payload[i * 4 + 3] = rgba[i * 4 + 2];
        }
        return;
    }
    const int bw = (w + 3) / 4, bh = (h + 3) / 4;
    const uint32_t block_bytes = (fmt == Format::BC1) ? 8u : 16u;
    payload.assign((size_t)bw * bh * block_bytes, 0);
    uint8_t block[64];
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            gather_block_rgba(rgba, w, h, bx, by, block);
            uint8_t* dst = payload.data() + ((size_t)by * bw + bx) * block_bytes;
            if (fmt == Format::BC1) {
                stb_compress_dxt_block(dst, block, 0, STB_DXT_HIGHQUAL);
            } else if (fmt == Format::BC3) {
                stb_compress_dxt_block(dst, block, 1, STB_DXT_HIGHQUAL);
            } else {
                uint8_t rg[32];
                for (int i = 0; i < 16; ++i) {
                    rg[i * 2 + 0] = block[i * 4 + 0];
                    rg[i * 2 + 1] = block[i * 4 + 1];
                }
                stb_compress_bc5_block(dst, rg);
            }
        }
    }
}

}

bool rgba_has_alpha(const uint8_t* rgba, int w, int h)
{
    const size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; ++i)
        if (rgba[i * 4 + 3] < 255) return true;
    return false;
}

bool build_from_rgba(const uint8_t* rgba_in, int w, int h,
                     const Options& opt, BuiltTex& out, std::string& err)
{
    out = BuiltTex{};
    if (!rgba_in || w <= 0 || h <= 0) { err = "empty image"; return false; }

    Format fmt = opt.format;
    if (fmt == Format::Auto)
        fmt = rgba_has_alpha(rgba_in, w, h) ? Format::BC3 : Format::BC1;

    // Geometry match (replace mode) pins the output to the original
    // texture's exact dimensions; otherwise clamp to a power of two.
    const bool match_geom = opt.match_width > 0 && opt.match_height > 0;
    int tw, th;
    if (match_geom) {
        tw = opt.match_width;
        th = opt.match_height;
    } else {
        tw = std::min(next_pow2_le(std::max(1, w)),
                      std::max(4, opt.max_dimension));
        th = std::min(next_pow2_le(std::max(1, h)),
                      std::max(4, opt.max_dimension));
        if (tw * 2 <= w && tw * 2 <= opt.max_dimension) tw *= 2;
        if (th * 2 <= h && th * 2 <= opt.max_dimension) th *= 2;
    }

    std::vector<uint8_t> level;
    if (tw != w || th != h) {
        resample_rgba(rgba_in, w, h, level, tw, th);
    } else {
        level.assign(rgba_in, rgba_in + (size_t)w * h * 4);
    }

    out.width  = (uint32_t)tw;
    out.height = (uint32_t)th;
    switch (fmt) {
        case Format::BC1:       out.pixel_format = kPfBC1;  break;
        case Format::BC3:       out.pixel_format = kPfBC3;  break;
        case Format::BC5Normal: out.pixel_format = kPfBC5;  break;
        case Format::RawARGB:   out.pixel_format = kPfARGB; break;
        default:                err = "bad format"; return false;
    }

    struct MipChunk { std::vector<uint8_t> bytes; int w, h; };
    std::vector<MipChunk> chunks;
    int lw = tw, lh = th;
    while (true) {
        std::vector<uint8_t> payload;
        build_mip_payload(level.data(), lw, lh, fmt, payload);

        // Chunks mirror the original texture's per-mip compression flags
        // when the caller provides them; the terrain/theme streamer only
        // decodes the Lionhead codec for large BC1 mips, so raw cf=7
        // there renders as the engine's fallback texture.
        uint32_t want_flag = 0;
        if (chunks.size() < opt.match_comp_flags.size()) {
            want_flag = opt.match_comp_flags[chunks.size()];
        } else if (match_geom && fmt == Format::BC1 &&
                   lw >= 64 && lh >= 64) {
            want_flag = 1u;
        }
        uint32_t comp_flag = kCompFlagRaw;
        if ((want_flag == 1u || want_flag == 11u) && fmt == Format::BC1) {
            std::vector<uint8_t> stream;
            std::string lh_err;
            if (!lh_encode_compressed_mip(payload.data(), lw, lh, stream,
                                          &lh_err, want_flag == 11u)) {
                err = "cf=" + std::to_string(want_flag) +
                      " encode failed for " + std::to_string(lw) + "x" +
                      std::to_string(lh) + " mip: " + lh_err;
                return false;
            }
            payload = std::move(stream);
            comp_flag = want_flag;
        } else if (fmt != Format::RawARGB) {
            X360Tile::swap_u16_endian(payload.data(), payload.size());
        }

        MipChunk ck; ck.w = lw; ck.h = lh;
        put32be(ck.bytes, comp_flag);
        put32be(ck.bytes, 48u);
        put32be(ck.bytes, (uint32_t)payload.size());
        for (int i = 0; i < 9; ++i) put32be(ck.bytes, 0u);
        ck.bytes.insert(ck.bytes.end(), payload.begin(), payload.end());
        chunks.push_back(std::move(ck));

        if (lw == 1 && lh == 1) break;
        if (opt.match_mip_count > 0) {
            // Reproduce the original's full chain down to 1x1.
            if ((int)chunks.size() >= opt.match_mip_count) break;
        } else {
            if (!opt.generate_mips) break;
            if (lw / 2 < kMinMipDim || lh / 2 < kMinMipDim) break;
        }
        std::vector<uint8_t> nxt; int nw = 0, nh = 0;
        downsample_half(level, lw, lh, nxt, nw, nh);
        level.swap(nxt); lw = nw; lh = nh;
    }

    if (opt.match_mip_count > 0 &&
        (int)chunks.size() != opt.match_mip_count) {
        err = "internal: produced " + std::to_string(chunks.size()) +
              " mips, original has " + std::to_string(opt.match_mip_count);
        return false;
    }

    out.mip_count = (uint32_t)chunks.size();

    const size_t header_size =
        std::max(kHeaderEntrySize, (size_t)32 + 4 * chunks.size());

    if (opt.force_mip0_split && chunks.size() < 2) {
        err = "the original texture streams its top mip separately; the "
              "replacement needs at least 2 mips (use a source of 32px or "
              "larger and keep mip generation on)";
        return false;
    }
    const bool split_mip0 =
        (opt.force_mip0_split ||
         out.width >= 1024 || out.height >= 1024) &&
        chunks.size() > 1;

    size_t total_data = 0;
    for (const auto& c : chunks) total_data += c.bytes.size();

    out.header.reserve(header_size);
    put32be(out.header, kHeaderSign);
    put32be(out.header, (uint32_t)total_data);
    put32be(out.header, kHeaderUnknown0);
    put32be(out.header, u1_flags_for_pf(out.pixel_format));
    put32be(out.header, out.width);
    put32be(out.header, out.height);
    put32be(out.header, out.pixel_format);
    put32be(out.header, out.mip_count);

    size_t cursor = header_size;
    for (const auto& c : chunks) {
        put32be(out.header, (uint32_t)cursor);
        cursor += c.bytes.size();
    }
    while (out.header.size() < header_size) out.header.push_back(0);

    size_t first_body_chunk = 0;
    if (split_mip0) {
        out.mip0 = chunks[0].bytes;
        first_body_chunk = 1;
    }
    for (size_t i = first_body_chunk; i < chunks.size(); ++i) {
        out.body.insert(out.body.end(),
                        chunks[i].bytes.begin(), chunks[i].bytes.end());
    }

    if (out.header.size() != header_size) { err = "header size mismatch"; return false; }
    return true;
}

}
