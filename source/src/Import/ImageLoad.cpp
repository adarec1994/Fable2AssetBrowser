#include "ImageLoad.h"

#include "stb_image.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>

#include <zlib.h>

namespace ImageLoad {

namespace {

std::string lower_ext(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string e = path.substr(dot);
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return e;
}

void finish(Image& out) {
    out.has_alpha = false;
    for (size_t i = 3; i < out.rgba.size(); i += 4) {
        if (out.rgba[i] < 255) { out.has_alpha = true; break; }
    }
}



inline uint32_t rd32le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

inline uint8_t expand5(uint32_t v) { return (uint8_t)((v << 3) | (v >> 2)); }
inline uint8_t expand6(uint32_t v) { return (uint8_t)((v << 2) | (v >> 4)); }

void dds_decode_bc1_block(const uint8_t* b, uint32_t* out, bool opaque_mode) {
    uint16_t c0 = (uint16_t)(b[0] | (b[1] << 8));
    uint16_t c1 = (uint16_t)(b[2] | (b[3] << 8));
    uint8_t r0 = expand5((c0 >> 11) & 31), g0 = expand6((c0 >> 5) & 63), b0 = expand5(c0 & 31);
    uint8_t r1 = expand5((c1 >> 11) & 31), g1 = expand6((c1 >> 5) & 63), b1 = expand5(c1 & 31);
    uint32_t cols[4];
    cols[0] = 0xFF000000u | ((uint32_t)b0 << 16) | ((uint32_t)g0 << 8) | r0;
    cols[1] = 0xFF000000u | ((uint32_t)b1 << 16) | ((uint32_t)g1 << 8) | r1;
    if (c0 > c1 || opaque_mode) {
        cols[2] = 0xFF000000u | ((uint32_t)((2*b0+b1)/3) << 16) | ((uint32_t)((2*g0+g1)/3) << 8) | (uint32_t)((2*r0+r1)/3);
        cols[3] = 0xFF000000u | ((uint32_t)((b0+2*b1)/3) << 16) | ((uint32_t)((g0+2*g1)/3) << 8) | (uint32_t)((r0+2*r1)/3);
    } else {
        cols[2] = 0xFF000000u | ((uint32_t)((b0+b1)/2) << 16) | ((uint32_t)((g0+g1)/2) << 8) | (uint32_t)((r0+r1)/2);
        cols[3] = 0x00000000u;
    }
    uint32_t idx = rd32le(b + 4);
    for (int i = 0; i < 16; ++i) out[i] = cols[(idx >> (2 * i)) & 3];
}

void dds_decode_bc4_channel(const uint8_t* b, uint8_t* out16) {
    uint8_t a0 = b[0], a1 = b[1];
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= (uint64_t)b[2 + i] << (8 * i);
    uint8_t tab[8];
    tab[0] = a0; tab[1] = a1;
    if (a0 > a1) {
        for (int i = 1; i <= 6; ++i) tab[i + 1] = (uint8_t)(((7 - i) * a0 + i * a1) / 7);
    } else {
        for (int i = 1; i <= 4; ++i) tab[i + 1] = (uint8_t)(((5 - i) * a0 + i * a1) / 5);
        tab[6] = 0; tab[7] = 255;
    }
    for (int i = 0; i < 16; ++i) out16[i] = tab[(bits >> (3 * i)) & 7];
}

bool load_dds(const uint8_t* d, size_t n, Image& out, std::string& err) {
    if (n < 128 || std::memcmp(d, "DDS ", 4) != 0) { err = "not a DDS file"; return false; }
    const uint8_t* h = d + 4;
    uint32_t hsize = rd32le(h);
    if (hsize != 124) { err = "bad DDS header size"; return false; }
    int height = (int)rd32le(h + 8);
    int width  = (int)rd32le(h + 12);
    const uint8_t* pf = h + 72;
    uint32_t pf_flags  = rd32le(pf + 4);
    uint32_t fourcc    = rd32le(pf + 8);
    uint32_t bitcount  = rd32le(pf + 12);
    uint32_t rmask = rd32le(pf + 16), gmask = rd32le(pf + 20);
    uint32_t bmask = rd32le(pf + 24), amask = rd32le(pf + 28);

    size_t data_off = 128;
    if ((pf_flags & 0x4) && fourcc == 0x30315844u) { 
        if (n < 148) { err = "truncated DX10 DDS"; return false; }
        uint32_t dxgi = rd32le(d + 128);
        data_off = 148;
        
        switch (dxgi) {
            case 71: case 72: fourcc = 0x31545844u; break;          
            case 74: case 75: fourcc = 0x33545844u; break;          
            case 77: case 78: fourcc = 0x35545844u; break;          
            case 80: case 81: fourcc = 0x55344342u; break;          
            case 83: case 84: fourcc = 0x55354342u; break;          
            case 28: case 87: case 88:
                pf_flags = 0x41; bitcount = 32;
                rmask = 0x000000FF; gmask = 0x0000FF00;
                bmask = 0x00FF0000; amask = 0xFF000000;
                if (dxgi == 87 || dxgi == 88) { 
                    rmask = 0x00FF0000; bmask = 0x000000FF;
                }
                fourcc = 0; break;
            default: err = "unsupported DXGI format " + std::to_string(dxgi); return false;
        }
    }

    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        err = "bad DDS dimensions"; return false;
    }
    out.width = width; out.height = height;
    out.rgba.assign((size_t)width * height * 4, 0xFF);
    uint32_t* px = (uint32_t*)out.rgba.data();

    const int bw = (width + 3) / 4, bh = (height + 3) / 4;
    auto place_block = [&](int bx, int by, const uint32_t* block) {
        for (int py = 0; py < 4; ++py) {
            int yy = by * 4 + py; if (yy >= height) break;
            for (int qx = 0; qx < 4; ++qx) {
                int xx = bx * 4 + qx; if (xx >= width) break;
                px[(size_t)yy * width + xx] = block[py * 4 + qx];
            }
        }
    };

    if (pf_flags & 0x4) { 
        size_t bsz = 16;
        if (fourcc == 0x31545844u ) bsz = 8;
        else if (fourcc == 0x55344342u  || fourcc == 0x31495441u ) bsz = 8;
        size_t need = (size_t)bw * bh * bsz;
        if (n < data_off + need) { err = "truncated DDS data"; return false; }
        const uint8_t* src = d + data_off;
        for (int by = 0; by < bh; ++by) {
            for (int bx = 0; bx < bw; ++bx) {
                const uint8_t* blk = src + ((size_t)by * bw + bx) * bsz;
                uint32_t block[16];
                switch (fourcc) {
                    case 0x31545844u: 
                        dds_decode_bc1_block(blk, block, false);
                        break;
                    case 0x32545844u: case 0x33545844u: { 
                        dds_decode_bc1_block(blk + 8, block, true);
                        for (int i = 0; i < 16; ++i) {
                            uint8_t a4 = (uint8_t)((blk[i / 2] >> ((i & 1) * 4)) & 0xF);
                            uint8_t a = (uint8_t)(a4 * 17);
                            block[i] = (block[i] & 0x00FFFFFFu) | ((uint32_t)a << 24);
                        }
                        break;
                    }
                    case 0x34545844u: case 0x35545844u: { 
                        dds_decode_bc1_block(blk + 8, block, true);
                        uint8_t alpha[16];
                        dds_decode_bc4_channel(blk, alpha);
                        for (int i = 0; i < 16; ++i)
                            block[i] = (block[i] & 0x00FFFFFFu) | ((uint32_t)alpha[i] << 24);
                        break;
                    }
                    case 0x55344342u: case 0x31495441u: { 
                        uint8_t r[16];
                        dds_decode_bc4_channel(blk, r);
                        for (int i = 0; i < 16; ++i)
                            block[i] = 0xFF000000u | ((uint32_t)r[i] << 16) |
                                       ((uint32_t)r[i] << 8) | r[i];
                        break;
                    }
                    case 0x55354342u: case 0x32495441u: { 
                        uint8_t r[16], g[16];
                        dds_decode_bc4_channel(blk, r);
                        dds_decode_bc4_channel(blk + 8, g);
                        for (int i = 0; i < 16; ++i) {
                            float nx = r[i] / 255.0f * 2 - 1, ny = g[i] / 255.0f * 2 - 1;
                            float nz2 = 1 - nx * nx - ny * ny;
                            uint8_t bz = (uint8_t)((nz2 > 0 ? std::sqrt(nz2) : 0.0f) * 127.5f + 127.5f);
                            block[i] = 0xFF000000u | ((uint32_t)bz << 16) |
                                       ((uint32_t)g[i] << 8) | r[i];
                        }
                        break;
                    }
                    default:
                        err = "unsupported DDS fourcc"; return false;
                }
                place_block(bx, by, block);
            }
        }
        finish(out);
        return true;
    }

    if (bitcount != 32 && bitcount != 24) {
        err = "unsupported DDS bit count " + std::to_string(bitcount);
        return false;
    }
    const size_t bpp = bitcount / 8;
    size_t need = (size_t)width * height * bpp;
    if (n < data_off + need) { err = "truncated DDS data"; return false; }
    auto chan = [&](uint32_t v, uint32_t mask) -> uint8_t {
        if (!mask) return 0xFF;
        uint32_t m = mask; int shift = 0;
        while (!(m & 1)) { m >>= 1; ++shift; }
        uint32_t val = (v & mask) >> shift;
        
        int bits = 0; while (m) { ++bits; m >>= 1; }
        if (bits >= 8) return (uint8_t)(val >> (bits - 8));
        return (uint8_t)(val * 255 / ((1u << bits) - 1));
    };
    const uint8_t* src = d + data_off;
    for (size_t i = 0, np = (size_t)width * height; i < np; ++i) {
        uint32_t v = 0;
        std::memcpy(&v, src + i * bpp, bpp);
        out.rgba[i * 4 + 0] = chan(v, rmask);
        out.rgba[i * 4 + 1] = chan(v, gmask);
        out.rgba[i * 4 + 2] = chan(v, bmask);
        out.rgba[i * 4 + 3] = (pf_flags & 0x1) ? chan(v, amask) : 0xFF;
    }
    finish(out);
    return true;
}



struct TiffField { uint16_t tag, type; uint32_t count, value_off; };

class TiffReader {
public:
    bool load(const uint8_t* d, size_t n, Image& out, std::string& err) {
        d_ = d; n_ = n;
        if (n < 8) { err = "truncated TIFF"; return false; }
        if (d[0] == 'I' && d[1] == 'I' && d[2] == 42 && d[3] == 0) le_ = true;
        else if (d[0] == 'M' && d[1] == 'M' && d[2] == 0 && d[3] == 42) le_ = false;
        else { err = "not a TIFF file"; return false; }

        uint32_t ifd = u32(4);
        if (ifd + 2 > n) { err = "bad TIFF IFD"; return false; }
        uint16_t nfields = u16(ifd);
        if (ifd + 2 + (size_t)nfields * 12 > n) { err = "bad TIFF IFD"; return false; }

        uint32_t width = 0, height = 0, compression = 1, photometric = 1;
        uint32_t rows_per_strip = 0xFFFFFFFF, predictor = 1, planar = 1;
        std::vector<uint32_t> bits, strip_offs, strip_counts;
        uint32_t samples = 1;
        std::vector<uint32_t> extra_samples;

        for (uint16_t i = 0; i < nfields; ++i) {
            size_t f = ifd + 2 + (size_t)i * 12;
            uint16_t tag = u16(f), type = u16(f + 2);
            uint32_t count = u32(f + 4);
            switch (tag) {
                case 256: width = field_u(f, type); break;
                case 257: height = field_u(f, type); break;
                case 258: bits = field_array(f, type, count); break;
                case 259: compression = field_u(f, type); break;
                case 262: photometric = field_u(f, type); break;
                case 273: strip_offs = field_array(f, type, count); break;
                case 277: samples = field_u(f, type); break;
                case 278: rows_per_strip = field_u(f, type); break;
                case 279: strip_counts = field_array(f, type, count); break;
                case 284: planar = field_u(f, type); break;
                case 317: predictor = field_u(f, type); break;
                case 338: extra_samples = field_array(f, type, count); break;
                default: break;
            }
        }

        if (!width || !height) { err = "TIFF missing dimensions"; return false; }
        if (planar != 1) { err = "planar TIFF not supported"; return false; }
        if (photometric > 2) { err = "unsupported TIFF photometric"; return false; }
        if (strip_offs.empty() || strip_offs.size() != strip_counts.size()) {
            err = "TIFF strips missing"; return false;
        }
        uint32_t bps = bits.empty() ? 1 : bits[0];
        for (uint32_t b : bits) if (b != bps) { err = "mixed TIFF bit depths"; return false; }
        if (bps != 8 && bps != 16) { err = "unsupported TIFF bit depth"; return false; }
        if (samples < 1 || samples > 4) { err = "unsupported TIFF sample count"; return false; }
        if (width > 16384 || height > 16384) { err = "TIFF too large"; return false; }

        const size_t bytes_per_sample = bps / 8;
        const size_t row_bytes = (size_t)width * samples * bytes_per_sample;
        std::vector<uint8_t> raster;
        raster.reserve(row_bytes * height);

        if (rows_per_strip == 0) rows_per_strip = height;
        for (size_t s = 0; s < strip_offs.size(); ++s) {
            uint32_t rows = std::min<uint32_t>(rows_per_strip,
                (uint32_t)(height - std::min<uint64_t>((uint64_t)s * rows_per_strip, height)));
            if (!rows) break;
            size_t want = row_bytes * rows;
            std::vector<uint8_t> strip;
            if (!read_strip(strip_offs[s], strip_counts[s], compression, want, strip, err))
                return false;
            if (strip.size() < want) strip.resize(want, 0);
            raster.insert(raster.end(), strip.begin(), strip.begin() + want);
        }
        if (raster.size() < row_bytes * height) { err = "TIFF data short"; return false; }

        if (predictor == 2) {
            for (uint32_t y = 0; y < height; ++y) {
                uint8_t* row = raster.data() + (size_t)y * row_bytes;
                if (bps == 8) {
                    for (size_t x = samples; x < row_bytes; ++x)
                        row[x] = (uint8_t)(row[x] + row[x - samples]);
                } else {
                    uint16_t* r16 = (uint16_t*)row;
                    size_t nvals = row_bytes / 2;
                    for (size_t x = samples; x < nvals; ++x)
                        r16[x] = (uint16_t)(r16[x] + r16[x - samples]);
                }
            }
        }

        out.width = (int)width; out.height = (int)height;
        out.rgba.assign((size_t)width * height * 4, 0xFF);
        const bool invert = (photometric == 0);
        for (size_t p = 0, np = (size_t)width * height; p < np; ++p) {
            uint8_t v[4] = {0, 0, 0, 255};
            for (uint32_t c = 0; c < samples; ++c) {
                size_t off = (p * samples + c) * bytes_per_sample;
                uint8_t s8 = (bps == 8)
                    ? raster[off]
                    : (uint8_t)(le_ ? (raster[off + 1]) : raster[off]); 
                v[c] = s8;
            }
            uint8_t r, g, b, a = 255;
            if (samples <= 2) {
                r = g = b = invert ? (uint8_t)(255 - v[0]) : v[0];
                if (samples == 2) a = v[1];
            } else {
                r = v[0]; g = v[1]; b = v[2];
                if (samples == 4) a = v[3];
            }
            out.rgba[p * 4 + 0] = r;
            out.rgba[p * 4 + 1] = g;
            out.rgba[p * 4 + 2] = b;
            out.rgba[p * 4 + 3] = a;
        }
        finish(out);
        return true;
    }

private:
    const uint8_t* d_ = nullptr;
    size_t n_ = 0;
    bool le_ = true;

    uint16_t u16(size_t o) const {
        if (o + 2 > n_) return 0;
        return le_ ? (uint16_t)(d_[o] | (d_[o + 1] << 8))
                   : (uint16_t)((d_[o] << 8) | d_[o + 1]);
    }
    uint32_t u32(size_t o) const {
        if (o + 4 > n_) return 0;
        return le_ ? ((uint32_t)d_[o] | ((uint32_t)d_[o+1] << 8) |
                      ((uint32_t)d_[o+2] << 16) | ((uint32_t)d_[o+3] << 24))
                   : (((uint32_t)d_[o] << 24) | ((uint32_t)d_[o+1] << 16) |
                      ((uint32_t)d_[o+2] << 8) | (uint32_t)d_[o+3]);
    }
    uint32_t field_u(size_t f, uint16_t type) const {
        if (type == 3) return u16(f + 8);
        return u32(f + 8);
    }
    std::vector<uint32_t> field_array(size_t f, uint16_t type, uint32_t count) const {
        std::vector<uint32_t> v;
        const size_t esz = (type == 3) ? 2 : 4;
        size_t src;
        if (esz * count <= 4) src = f + 8;
        else src = u32(f + 8);
        for (uint32_t i = 0; i < count && src + esz * (i + 1) <= n_; ++i)
            v.push_back(type == 3 ? u16(src + i * 2) : u32(src + i * 4));
        return v;
    }

    bool read_strip(uint32_t off, uint32_t count, uint32_t compression,
                    size_t expected, std::vector<uint8_t>& out, std::string& err) {
        if ((size_t)off + count > n_) { err = "TIFF strip out of range"; return false; }
        const uint8_t* src = d_ + off;
        switch (compression) {
            case 1: 
                out.assign(src, src + count);
                return true;
            case 5: 
                return lzw_decode(src, count, expected, out, err);
            case 8: case 32946: { 
                out.assign(expected, 0);
                uLongf dl = (uLongf)expected;
                int rc = uncompress(out.data(), &dl, src, count);
                if (rc != Z_OK && rc != Z_BUF_ERROR) { err = "TIFF deflate error"; return false; }
                out.resize(dl);
                return true;
            }
            case 32773: { 
                out.clear(); out.reserve(expected);
                size_t i = 0;
                while (i < count && out.size() < expected) {
                    int8_t nb = (int8_t)src[i++];
                    if (nb >= 0) {
                        size_t take = std::min<size_t>((size_t)nb + 1, count - i);
                        out.insert(out.end(), src + i, src + i + take);
                        i += take;
                    } else if (nb != -128) {
                        if (i >= count) break;
                        out.insert(out.end(), (size_t)(-nb) + 1, src[i++]);
                    }
                }
                return true;
            }
            default:
                err = "unsupported TIFF compression " + std::to_string(compression);
                return false;
        }
    }

    
    bool lzw_decode(const uint8_t* src, size_t n, size_t expected,
                    std::vector<uint8_t>& out, std::string& err) {
        out.clear(); out.reserve(expected);
        std::vector<std::vector<uint8_t>> dict;
        auto reset = [&]() {
            dict.clear(); dict.reserve(4096);
            for (int i = 0; i < 256; ++i) dict.push_back({(uint8_t)i});
            dict.push_back({}); 
            dict.push_back({}); 
        };
        reset();
        int code_size = 9;
        uint32_t bitbuf = 0; int bits = 0; size_t i = 0;
        std::vector<uint8_t> prev;
        auto next_code = [&]() -> int {
            while (bits < code_size && i < n) {
                bitbuf = (bitbuf << 8) | src[i++];
                bits += 8;
            }
            if (bits < code_size) return -1;
            int c = (int)((bitbuf >> (bits - code_size)) & ((1u << code_size) - 1));
            bits -= code_size;
            return c;
        };
        while (out.size() < expected) {
            int code = next_code();
            if (code < 0 || code == 257) break;
            if (code == 256) {
                reset(); code_size = 9; prev.clear();
                continue;
            }
            std::vector<uint8_t> entry;
            if (code < (int)dict.size() && !dict[code].empty()) {
                entry = dict[code];
            } else if (code == (int)dict.size() && !prev.empty()) {
                entry = prev; entry.push_back(prev[0]);
            } else if ((size_t)code < dict.size() && code < 256) {
                entry = dict[code];
            } else {
                err = "corrupt TIFF LZW stream"; return false;
            }
            out.insert(out.end(), entry.begin(), entry.end());
            if (!prev.empty()) {
                std::vector<uint8_t> ne = prev; ne.push_back(entry[0]);
                dict.push_back(std::move(ne));
                
                if ((int)dict.size() >= (1 << code_size) - 1 && code_size < 12)
                    ++code_size;
            }
            prev = std::move(entry);
        }
        return true;
    }
};

}  

bool extension_supported(const std::string& path) {
    const std::string e = lower_ext(path);
    return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".dds" ||
           e == ".tif" || e == ".tiff" || e == ".bmp" || e == ".tga" ||
           e == ".psd";
}

bool load_memory(const uint8_t* bytes, size_t size,
                 const std::string& name_hint, Image& out, std::string& err)
{
    out = Image{};
    if (!bytes || size < 8) { err = "empty image data"; return false; }

    if (size >= 4 && std::memcmp(bytes, "DDS ", 4) == 0)
        return load_dds(bytes, size, out, err);

    const bool tiff_magic =
        (bytes[0] == 'I' && bytes[1] == 'I' && bytes[2] == 42 && bytes[3] == 0) ||
        (bytes[0] == 'M' && bytes[1] == 'M' && bytes[2] == 0 && bytes[3] == 42);
    if (tiff_magic) {
        TiffReader tr;
        return tr.load(bytes, size, out, err);
    }

    int w = 0, h = 0, comp = 0;
    stbi_uc* px = stbi_load_from_memory(bytes, (int)size, &w, &h, &comp, 4);
    if (!px) {
        err = std::string("could not decode ") +
              (name_hint.empty() ? "image" : name_hint) + ": " +
              (stbi_failure_reason() ? stbi_failure_reason() : "unknown error");
        return false;
    }
    out.width = w; out.height = h;
    out.rgba.assign(px, px + (size_t)w * h * 4);
    stbi_image_free(px);
    finish(out);
    return true;
}

bool load_file(const std::string& path, Image& out, std::string& err)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "could not open " + path; return false; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
    return load_memory(bytes.data(), bytes.size(), path, out, err);
}

}
