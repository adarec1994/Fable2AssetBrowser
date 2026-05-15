#include "EhfPalette.h"

#include <cstring>

namespace EhfPalette {

namespace {

inline uint32_t rd_u32_be(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) <<  8) |  uint32_t(p[3]);
}

inline float u32_to_f32(uint32_t u) {
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

/* Return the position of `needle` in [from..end) or SIZE_MAX. */
size_t find_byte(const std::vector<uint8_t>& d, size_t from, uint8_t needle) {
    for (size_t i = from; i < d.size(); ++i)
        if (d[i] == needle) return i;
    return SIZE_MAX;
}

bool starts_with(const std::vector<uint8_t>& d, size_t at, const char* lit, size_t n) {
    if (at + n > d.size()) return false;
    return std::memcmp(d.data() + at, lit, n) == 0;
}

}  

Palette Parse(const std::vector<uint8_t>& d)
{
    Palette r;
    if (d.size() < 64) return r;

    /* Find the first "art\\" preceded by a plausible u32 BE count
       (1..200).  That's the palette base. */
    size_t pal_start = SIZE_MAX;
    for (size_t i = 4; i + 4 < d.size(); ++i) {
        if (d[i] == 'a' && d[i+1] == 'r' && d[i+2] == 't' && d[i+3] == '\\') {
            const uint32_t count = rd_u32_be(d.data() + i - 4);
            if (count > 1 && count < 200) {
                pal_start = i;
                break;
            }
        }
    }
    if (pal_start == SIZE_MAX) return r;
    r.palette_offset = pal_start;

    /* Walk entries.  Each entry = diffuse_path \0 + normal_path \0 +
       13 bytes metadata, then either another entry's "art\\" prefix
       or the start of some other binary section.                   */
    size_t i = pal_start;
    while (i + 4 < d.size()) {
        if (!starts_with(d, i, "art\\", 4)) break;
        const size_t diff_end = find_byte(d, i, 0);
        if (diff_end == SIZE_MAX || diff_end - i > 256) break;
        Entry e;
        e.diffuse_path.assign(reinterpret_cast<const char*>(d.data() + i),
                              diff_end - i);
        size_t j = diff_end + 1;
        if (!starts_with(d, j, "art\\", 4)) break;
        const size_t norm_end = find_byte(d, j, 0);
        if (norm_end == SIZE_MAX || norm_end - j > 256) break;
        e.normal_path.assign(reinterpret_cast<const char*>(d.data() + j),
                             norm_end - j);

        /* 13-byte metadata block:
             byte 0:    padding (0)
             bytes 1-4: f32 BE tile_scale  (typically 0.125)
             bytes 5-8: f32 BE intensity   (typically 1.0)
             bytes 9-12: zero / reserved
           Total = 13.  Then either next entry's "art\\" or non-palette
           bytes.                                                     */
        size_t k = norm_end + 1;
        if (k + 13 > d.size()) break;
        e.tile_scale = u32_to_f32(rd_u32_be(d.data() + k + 1));
        e.intensity  = u32_to_f32(rd_u32_be(d.data() + k + 5));
        size_t next_i = k + 13;
        r.entries.push_back(std::move(e));

        i = next_i;
        if (r.entries.size() > 256) break;
    }
    r.ok = !r.entries.empty();
    return r;
}

}  
