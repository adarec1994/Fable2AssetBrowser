#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Level {
struct BeReader {
    const uint8_t* p = nullptr;
    size_t         n = 0;
    size_t         i = 0;

    bool need(size_t k) const { return i + k <= n; }

    bool u8(uint8_t& v) {
        if (!need(1)) return false;
        v = p[i++];
        return true;
    }
    bool u32(uint32_t& v) {
        if (!need(4)) return false;
        v  = (uint32_t(p[i + 0]) << 24)
           | (uint32_t(p[i + 1]) << 16)
           | (uint32_t(p[i + 2]) << 8)
           |  uint32_t(p[i + 3]);
        i += 4;
        return true;
    }
    bool u64(uint64_t& v) {
        uint32_t hi = 0;
        uint32_t lo = 0;
        if (!u32(hi) || !u32(lo)) return false;
        v = (uint64_t(hi) << 32) | uint64_t(lo);
        return true;
    }
    bool f32(float& f) {
        uint32_t u = 0;
        if (!u32(u)) return false;
        std::memcpy(&f, &u, sizeof(f));
        return true;
    }
    bool skip(size_t k) {
        if (!need(k)) return false;
        i += k;
        return true;
    }
    bool half(float& out) {
        if (!need(2)) return false;
        const uint16_t h =
            (uint16_t(p[i]) << 8) | uint16_t(p[i + 1]);
        i += 2;
        const uint32_t sign = (uint32_t(h & 0x8000)) << 16;
        const uint32_t exp_h = (h >> 10) & 0x1f;
        const uint32_t mant = h & 0x3ff;
        uint32_t bits;
        if (exp_h == 0) {
            if (mant == 0) {
                bits = sign;
            } else {
                uint32_t e = 127 - 14;
                uint32_t m = mant;
                while ((m & 0x400) == 0) { m <<= 1; --e; }
                m &= 0x3ff;
                bits = sign | (e << 23) | (m << 13);
            }
        } else if (exp_h == 31) {
            bits = sign | (0xff << 23) | (mant << 13);
        } else {
            bits = sign | ((exp_h + (127 - 15)) << 23) | (mant << 13);
        }
        std::memcpy(&out, &bits, sizeof(out));
        return true;
    }
    bool cstr(std::string& s) {
        s.clear();
        const size_t start = i;
        const size_t limit = std::min(n, start + 4096);
        while (i < limit) {
            const uint8_t c = p[i++];
            if (c == 0) return true;
            s.push_back(static_cast<char>(c));
        }
        return false;
    }
};

inline uint32_t read_be_u32_raw(const uint8_t* p)
{
    return (uint32_t(p[0]) << 24) |
           (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8)  |
            uint32_t(p[3]);
}

inline float read_be_f32_raw(const uint8_t* p)
{
    uint32_t u = read_be_u32_raw(p);
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

inline bool skip_ehf_tex_blob(BeReader& r)
{
    const size_t tex_start = r.i;
    if (!r.need(0x60)) return false;
    const uint32_t magic = read_be_u32_raw(r.p + tex_start);
    if (magic != 0xFFFFFFFEu) return false;
    const uint32_t pf = read_be_u32_raw(r.p + tex_start + 0x18);
    const uint32_t mt = read_be_u32_raw(r.p + tex_start + 0x20);
    if (mt > 0x100 || !r.need(size_t(mt) + 8)) return false;

    size_t end = 0;
    if (pf == 98u) {
        const uint32_t tw = read_be_u32_raw(r.p + tex_start + 0x10);
        const uint32_t th = read_be_u32_raw(r.p + tex_start + 0x14);
        end = tex_start + size_t(mt) + size_t(tw) * size_t(th) * 2;
    } else {
        const uint32_t comp_size =
            read_be_u32_raw(r.p + tex_start + mt + 4);
        end = tex_start + size_t(mt) + 8 + size_t(comp_size);
    }
    if (end > r.n) return false;
    r.i = end;
    return true;
}

}
