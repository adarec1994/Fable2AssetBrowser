/* Standalone "v2" MDL parser — IDA-faithful walk of the game's
   own loader. No fallback to v1.

   The chain we emulate, with corrected byte counts from full disasm:

     sub_82AB80C8       file header (16 bytes)
       └─ sub_82B88AD0  mesh-class block A (60 bytes):
            12 (vec3) + 4 (float) + 12 (vec3) + 12 (vec3)
            + 4 (u32) + 4 (u32) + 4 (u32) + 4 (u32) + 4 (u32)
          └─ sub_82B88E80  mesh-class block B:
               2 × u8 + 2 × u8 bool + 2 × float + 2 × u8 bool
               + 2 × float (= Unk2[0..1]) + 2 × u8 bool
               + (1 float if version > 34)         = 24 or 28 bytes

     sub_82AB85B8  part-loop driver:
       for each i in [0, mesh+0x50):
         if mesh+0x54+i*4 > 0:
           sub_82AB43B8        allocate part (no file reads)
           vtable[20] = sub_82AB51F0:
             sub_82AB66D0()    no file reads
             store animated_flag
             sub_82AB65B0(part, file)
               └─ sub_82B714A0  reads bone section:
                    branch on byte_834BE9E8
                    32 bytes (4 × float+u32 pairs)
                    + 4 bytes (bone_count u32)
                    + bone_count × (strz + u32 parent)
                    + 4 bytes (bone_transform_count u32)
                    + count × 44 bytes per transform
                    + tail-call sub_82B710D0(a1) (no file reads — CRC)
             Per-part header reads (matches sub_82B88AD0 pattern):
               sub_82A1BEA8 → 3 BE floats (vec3)
               4 BE bytes → 1 float (vector merge w/ above)
               sub_82A1BEA8 → 3 BE floats (vec3)
               sub_82A1BEA8 → 3 BE floats (vec3)
               4 BE bytes → u32 count → part+40 (count_A)
               4 BE bytes → u32 count → part+44 (count_B  stride-20 regular)
               4 BE bytes → u32 count → part+48 (count_C  stride-28 alt)
               4 BE bytes → u32 count → part+52 (count_D  stride-24 skinned)
               4 BE bytes → u32 count → part+56 (count_E  stride-36 breakable)
             1 byte animated flag
             4 BE bytes → float (named-flag count)
             named-flag loop (variable: strz + u8 each)
             sub_82AB6CC0      6 BE u32 + N BE floats
             4 BE u32          string list count
             N × strz          string list (texture paths)
             count_A loop      render-queue items (1 u32 each, +4 if NaN)
             count_B loop      sub_82B2BCC8 per submesh — stride 20
             count_C loop      sub_82B20180 per submesh — stride 28
             count_E loop      sub_82B30B50 per submesh — stride 36
             count_D loop      sub_82B1D7C8 per submesh — stride 24

   See .claude/MDL_FORMAT_SPEC.md for the running spec.

   Gated by S.dev_mode in ModelParser.cpp. v1 stays untouched. */

#include "ModelParserV2.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

using std::uint8_t; using std::uint16_t; using std::uint32_t;

namespace {

struct R {
    const uint8_t* p = nullptr;
    size_t n = 0;
    size_t i = 0;

    bool need(size_t k) const { return i + k <= n; }
    bool u8 (uint8_t&  v) { if (!need(1)) return false; v = p[i++]; return true; }
    bool u16be(uint16_t& v) {
        if (!need(2)) return false;
        v = (uint16_t(p[i]) << 8) | uint16_t(p[i+1]);
        i += 2;
        return true;
    }
    bool u32be(uint32_t& v) {
        if (!need(4)) return false;
        v = (uint32_t(p[i  ]) << 24) | (uint32_t(p[i+1]) << 16) |
            (uint32_t(p[i+2]) <<  8) |  uint32_t(p[i+3]);
        i += 4;
        return true;
    }
    bool f32be(float& f) { uint32_t u; if (!u32be(u)) return false; std::memcpy(&f, &u, 4); return true; }
    bool strz(std::string& s, size_t maxlen = 8192) {
        s.clear();
        size_t lim = std::min(n, i + maxlen);
        while (i < lim) {
            char c = (char)p[i++];
            if (c == 0) return true;
            s.push_back(c);
        }
        return false;
    }
    bool skip(size_t k) { if (!need(k)) return false; i += k; return true; }
};

/* Each call to sub_82A1BEA8 in the game reads 3 BE floats from the
   file (12 bytes) and assembles them into a 16-byte vector in memory.
   On the v2 side we only need to advance the file pointer past those
   12 bytes and remember the 3 floats. */
static bool read_vec3_be(R& r, float v[3]) {
    return r.f32be(v[0]) && r.f32be(v[1]) && r.f32be(v[2]);
}

/* sub_82A1BD30 — reads 2 BE floats (8 bytes). */
static bool read_2_be_floats(R& r, float v[2]) {
    return r.f32be(v[0]) && r.f32be(v[1]);
}

/* sub_82A93850 — per-bucket-A-material reader. Reads 9 strings via
   the stream's vtable[20] (= strz) plus a final 2-float pair via
   sub_82A1BD30. The materials are stored in the part object's
   material array; we just consume the bytes here. */
static bool read_bucket_a_material(R& r) {
    for (int k = 0; k < 9; ++k) {
        std::string s;
        if (!r.strz(s)) return false;
    }
    float pair[2];
    if (!read_2_be_floats(r, pair)) return false;
    return true;
}

/* sub_82B1E458 — 45 bytes of per-item fixed data:
     2 BE f32 + 1 u8 + 5 BE f32 + 1 BE f32 + sub_82A1BD30(2 BE f32) + 4 u8 */
static bool read_bucket_a_item_block(R& r) {
    float f0, f1; if (!r.f32be(f0)) return false; if (!r.f32be(f1)) return false;
    uint8_t b84; if (!r.u8(b84)) return false;
    float f88, f92, f96, f100, f104;
    if (!r.f32be(f88))  return false;
    if (!r.f32be(f92))  return false;
    if (!r.f32be(f96))  return false;
    if (!r.f32be(f100)) return false;
    if (!r.f32be(f104)) return false;
    float f72; if (!r.f32be(f72)) return false;
    float pair[2]; if (!read_2_be_floats(r, pair)) return false;
    uint8_t b11, b10, b9, b12;
    if (!r.u8(b11)) return false;
    if (!r.u8(b10)) return false;
    if (!r.u8(b9))  return false;
    if (!r.u8(b12)) return false;
    (void)f0;(void)f1;(void)b84;(void)f88;(void)f92;(void)f96;
    (void)f100;(void)f104;(void)f72;(void)b11;(void)b10;(void)b9;(void)b12;
    return true;
}

/* sub_82B1EB60 — u32 mat_count + per-material (sub_82A93850). */
static bool read_bucket_a_material_list(R& r) {
    uint32_t mat_count = 0;
    if (!r.u32be(mat_count)) return false;
    if (mat_count > 0x10000) return false;
    for (uint32_t m = 0; m < mat_count; ++m) {
        if (!read_bucket_a_material(r)) return false;
    }
    return true;
}

/* sub_82B1E2F8 (vtable[16] for case 0/3 bucket-A object):
     strz name + sub_82B1E458 + sub_82B1EB60. */
static bool read_bucket_a_item_case03(R& r, std::string& name) {
    if (!r.strz(name))             return false;
    if (!read_bucket_a_item_block(r))    return false;
    if (!read_bucket_a_material_list(r)) return false;
    return true;
}

/* sub_82B19168 (vtable[16] for case 1 bucket-A object):
     case 0/3 reader + 1 extra u8 byte. */
static bool read_bucket_a_item_case1(R& r) {
    std::string name;
    if (!read_bucket_a_item_case03(r, name)) return false;
    uint8_t b; if (!r.u8(b)) return false;
    (void)b;
    return true;
}

/* sub_82B23728 (vtable[16] for case 2 bucket-A object):
     vec3 + f32 + vec3 + 4 raw + f32 + f32 + vec3 + vec3 + f32  (68B) */
static bool read_bucket_a_item_case2(R& r) {
    float v3a[3], v3b[3], v3c[3], v3d[3];
    float f0, f1, f2, f3;
    uint8_t raw4[4];
    if (!read_vec3_be(r, v3a))   return false;
    if (!r.f32be(f0))            return false;
    if (!read_vec3_be(r, v3b))   return false;
    if (!r.u8(raw4[0]))          return false;
    if (!r.u8(raw4[1]))          return false;
    if (!r.u8(raw4[2]))          return false;
    if (!r.u8(raw4[3]))          return false;
    if (!r.f32be(f1))            return false;
    if (!r.f32be(f2))            return false;
    if (!read_vec3_be(r, v3c))   return false;
    if (!read_vec3_be(r, v3d))   return false;
    if (!r.f32be(f3))            return false;
    (void)f0;(void)f1;(void)f2;(void)f3;
    return true;
}

/* sub_82B2BCC8 — bucket B (stride 20) submesh reader. Pre-vertex
   header layout:
     strz name
     u8   flag
     5 × BE f32  (4th = index_count, 5th = vertex_count at +132)
     vec3 #1 + vec3 #2                          (24 bytes)
     vec3 #3 + 1 BE f32 (w-component)           (16 bytes)
     BE f32 sub_entry_count → N
     N × sub-entry (41 bytes: 2f + 1u8 + 2f + 2×vec3)

   Returns vertex_count, index_count, sub_entry_count so the caller
   can size subsequent buffer reads.  Buffer reads themselves
   (VB/IB/secondary VB/animated extras) are deferred until the
   per-part state from sub_82AB51F0 is fully threaded through. */
struct BucketBHeader {
    std::string name;
    uint32_t index_count  = 0;
    uint32_t vertex_count = 0;
    uint32_t sub_entry_count = 0;
};

static bool read_bucket_b_sub_entry(R& r) {
    float fa, fb; if (!r.f32be(fa)) return false; if (!r.f32be(fb)) return false;
    uint8_t bf;   if (!r.u8(bf))    return false;
    float fc, fd; if (!r.f32be(fc)) return false; if (!r.f32be(fd)) return false;
    float v3a[3]; if (!read_vec3_be(r, v3a)) return false;
    float v3b[3]; if (!read_vec3_be(r, v3b)) return false;
    (void)fa;(void)fb;(void)bf;(void)fc;(void)fd;
    return true;
}

static bool read_bucket_b_header(R& r, BucketBHeader& bh) {
    if (!r.strz(bh.name)) return false;
    uint8_t flag; if (!r.u8(flag)) return false;
    float f208, f60, f136;
    uint32_t f128_u, f132_u;
    if (!r.f32be(f208)) return false;
    if (!r.f32be(f60))  return false;
    if (!r.f32be(f136)) return false;
    if (!r.u32be(f128_u)) return false;  /* index_count */
    if (!r.u32be(f132_u)) return false;  /* vertex_count */
    float v3a[3]; if (!read_vec3_be(r, v3a)) return false;
    float v3b[3]; if (!read_vec3_be(r, v3b)) return false;
    float v3c[3]; if (!read_vec3_be(r, v3c)) return false;
    float w_comp; if (!r.f32be(w_comp)) return false;
    uint32_t sub_count_u;
    if (!r.u32be(sub_count_u)) return false;
    if (sub_count_u > 0x10000) return false;
    bh.index_count  = f128_u;
    bh.vertex_count = f132_u;
    bh.sub_entry_count = sub_count_u;
    for (uint32_t k = 0; k < sub_count_u; ++k) {
        if (!read_bucket_b_sub_entry(r)) return false;
    }
    (void)flag;(void)f208;(void)f60;(void)f136;
    return true;
}

/* sub_82B31CA0 — bucket-B / bucket-C animated trailer reader.
   Triggered when the bucket B/C "trigger" BE f32 is non-zero (which is
   how animated/skinned meshes deliver per-vertex bind-pose data — the
   per-submesh VB is *not* present in the file for animated meshes).

   Phase 1 (header):
     12 × BE u32 (counts and pointers)
     If version >= 36:  1 × BE u32 (extra count)
     7 × BE f32 (params)
     2 × u8 (flags)

   Phase 2 (per-vertex, v26 = vertex count):
     v26 × 12  vec3 positions (BE f32)
     v26 × 8   raw bytes  (UV-ish?)
     v26 × 4   raw bytes  (bone idx?)
     v26 × 16  raw bytes  (normals)
     v26 × 4   raw bytes
     2 × a1+20 bytes      (index-related)
     4 × v61              (more)
     16 × v61
     v26 × 1   raw bytes

   Phase 3 (per-struct loops):
     v79  × 32   bytes  (vec3 + vec3 + f32 + u32)
     v92  × 52   bytes  (4 × vec3 + u32)
     v104 × 56   bytes  (4 × vec3 + f32 + u32)
     v116 × 148  bytes  (3 × 48-byte memcpy + u32)
     a1+16 × 8   bytes  (pair of u32s)

   Phase 4 (secondary VB):
     If bbox+16 > 0:  v26 × 16 × bbox+16 bytes

   Returns the file offset of Phase 2's position block so the geometry
   pass can pull positions for animated meshes. */
struct B31CA0Result {
    uint32_t vertex_count        = 0;
    size_t   pos_off             = 0;  /* 12 BE f32 / vertex */
    size_t   uv_off              = 0;  /* 8 raw bytes / vertex */
    bool     ok                  = false;
};

static bool read_b31ca0_trailer(R& r, uint32_t version,
                                uint32_t bbox_secondary_units,
                                B31CA0Result& result)
{
    uint32_t u_a1_16, u_a1_20, v26, u_a1_28, u_a1_32_or_v26;
    uint32_t u_a1_24, v61, v79, v92, v104, v116, v128;

    if (!r.u32be(u_a1_16))  return false;
    if (!r.u32be(u_a1_20))  return false;
    if (!r.u32be(v26))      return false;
    if (!r.u32be(u_a1_28))  return false;
    if (version >= 36) {
        if (!r.u32be(u_a1_32_or_v26)) return false;
    } else {
        u_a1_32_or_v26 = v26;
    }
    if (!r.u32be(u_a1_24))  return false;
    if (!r.u32be(v61))      return false;
    if (!r.u32be(v79))      return false;
    if (!r.u32be(v92))      return false;
    if (!r.u32be(v104))     return false;
    if (!r.u32be(v116))     return false;
    if (!r.u32be(v128))     return false;

    /* Sanity guards — these counts can legitimately be 0 but a corrupt
       trailer might give huge values that would otherwise hang us. */
    auto sane = [](uint32_t x){ return x <= 0x100000u; };
    if (!sane(v26) || !sane(u_a1_16) || !sane(u_a1_20) || !sane(v61) ||
        !sane(v79) || !sane(v92) || !sane(v104) || !sane(v116) || !sane(v128))
        return false;

    /* 6 BE f32 params (a1+244 .. a1+264, 4 bytes each) + 2 u8 flags
       (a1+268, a1+269).  IDA shows reads of v367, v369, v374, v373,
       v371, v368 (6 floats), then v346 and v345 (2 bytes). */
    for (int k = 0; k < 6; ++k) { float f; if (!r.f32be(f)) return false; (void)f; }
    uint8_t b0, b1;
    if (!r.u8(b0)) return false;
    if (!r.u8(b1)) return false;

    /* Phase 2 — positions live here, in v26 × 12-byte BE vec3 form. */
    result.pos_off = r.i;
    if (!r.skip((size_t)v26 * 12)) return false;
    result.uv_off  = r.i;
    if (!r.skip((size_t)v26 * 8))  return false;   /* UV-ish */
    if (!r.skip((size_t)v26 * 4))  return false;
    if (!r.skip((size_t)v26 * 16)) return false;
    if (!r.skip((size_t)v26 * 4))  return false;
    if (!r.skip((size_t)u_a1_20 * 2)) return false;
    if (!r.skip((size_t)v61 * 4))  return false;
    if (!r.skip((size_t)v61 * 16)) return false;
    if (!r.skip((size_t)v26 * 1))  return false;

    /* Phase 3 — per-struct loops. */
    for (uint32_t k = 0; k < v79; ++k) {
        if (!r.skip(12 + 12 + 4 + 4)) return false;
    }
    for (uint32_t k = 0; k < v92; ++k) {
        if (!r.skip(12 * 4 + 4)) return false;
    }
    for (uint32_t k = 0; k < v104; ++k) {
        if (!r.skip(12 * 4 + 4 + 4)) return false;
    }
    for (uint32_t k = 0; k < v116; ++k) {
        if (!r.skip(48 * 3 + 4)) return false;
    }
    /* Final pair-loop: writes pairs of u32 into a vector sized to v128
       elements × 8 bytes each. IDA's `HIDWORD(v343)` is left holding
       v128 (the last Phase-1 u32 read) because nothing in Phase 2 or
       Phase 3 touches v343.  So the loop runs v128 times. */
    for (uint32_t k = 0; k < v128; ++k) {
        uint32_t a, b;
        if (!r.u32be(a)) return false;
        if (!r.u32be(b)) return false;
        (void)a; (void)b;
    }

    /* Phase 4 — secondary VB. */
    if (bbox_secondary_units > 0) {
        size_t sec = (size_t)v26 * 16u * bbox_secondary_units;
        if (!r.skip(sec)) return false;
    }

    (void)u_a1_28; (void)u_a1_32_or_v26; (void)u_a1_24; (void)v128;
    (void)b0; (void)b1;
    result.vertex_count = v26;
    result.ok = true;
    return true;
}


/* sub_82B89640 + sub_82B894B0 — bucket-C trigger2 trailer.
   Reads:
     BE u32 (→ a1[0])
     BE u32 (→ a1[4])
     BE u32 count
     for count iterations:
       BE u32  (4 bytes)
       sub_82A1C110 → 4 BE f32 (16 bytes)
       = 20 bytes / iter

   Total bytes from file = 12 + count × 20. */
static bool read_sub_82B89640_trailer(R& r) {
    uint32_t u0, u1, count;
    if (!r.u32be(u0))    return false;
    if (!r.u32be(u1))    return false;
    if (!r.u32be(count)) return false;
    if (count > 0x100000u) return false;
    for (uint32_t k = 0; k < count; ++k) {
        uint32_t hu;
        if (!r.u32be(hu)) return false;
        for (int f = 0; f < 4; ++f) {
            float fv; if (!r.f32be(fv)) return false; (void)fv;
        }
        (void)hu;
    }
    (void)u0;(void)u1;
    return true;
}

/* sub_82B1D7C8 — bucket-D (stride 24, skinned LOD) per-item reader.
   Unlike B/C/E, bucket D does NOT store a raw stride-24 VB in the file.
   The game reads per-vertex floats and CPU-encodes them into the
   24-byte target stride.  Per-item byte sequence:
     u8  flag1
     u8  flag2 → item+4
     BE u32 → item+8
     BE u32 → item+12
     BE u32 → item+16  (base "level vertex count" = idx_count)
     BE u32 → item+20  (= vertex_count for the source-vertex array)
     [per vertex (item+20 times):
        vec3 (12B)  source position
        vec3 (12B)  normal / tangent
        2 BE f32 (8B)  UV
        sub_82A1C110 vec4 (16B)  blend / skinning
       = 48 bytes / vertex ]
     base-IB: item+16 indices × u16 BE
     (additional LOD index sets are CPU-computed, not from file) */
struct BucketDHeader {
    uint32_t base_idx_count    = 0;  /* item+16 */
    uint32_t source_vert_count = 0;  /* item+20 */
};

static bool read_bucket_d_header(R& r, BucketDHeader& dh) {
    uint8_t  b0, b1;
    uint32_t u8_, u12;
    if (!r.u8(b0))  return false;
    if (!r.u8(b1))  return false;
    if (!r.u32be(u8_))                  return false;
    if (!r.u32be(u12))                  return false;
    if (!r.u32be(dh.base_idx_count))    return false;
    if (!r.u32be(dh.source_vert_count)) return false;
    (void)b0;(void)b1;(void)u8_;(void)u12;
    return true;
}

/* sub_82B30B50 — bucket-E (stride 36, breakable/tree) per-item reader.
   Per item:
     u8 flag1
     u8 flag2 (= "is NewTree" bool stored at item+224)
     BE u32  → item+60
     BE u32  → item+136
     BE u32  → item+128  (index_count)
     BE u32  → item+132  (vertex_count)
     vertex_count × 36 bytes (raw primary VB, alloc'd via sub_82B850B8
                              with stride 36)
     index_count × u16 BE   (IB)
     if bbox[+16] > 0: vertex_count × 16 × bbox[+16] bytes (secondary VB)
*/
struct BucketEHeader {
    uint32_t index_count  = 0;
    uint32_t vertex_count = 0;
};

static bool read_bucket_e_header(R& r, BucketEHeader& eh) {
    uint8_t  b0, b1;
    uint32_t u60, u136;
    if (!r.u8(b0))            return false;
    if (!r.u8(b1))            return false;
    if (!r.u32be(u60))        return false;
    if (!r.u32be(u136))       return false;
    if (!r.u32be(eh.index_count))  return false;
    if (!r.u32be(eh.vertex_count)) return false;
    (void)b0;(void)b1;(void)u60;(void)u136;
    return true;
}

/* sub_82B20180 — bucket-C (stride 28, alt) per-item reader. Common
   "v67==0" path (the alternate animated-bone path used when count_C>0
   AND mesh.a5==33 is left out for now — that's an additional
   per-sub-entry 4 bytes + a separate outer/inner u32 array). */
struct BucketCHeader {
    uint32_t index_count  = 0;
    uint32_t vertex_count = 0;
    uint32_t sub_entry_count = 0;
};

static bool read_bucket_c_sub_entry(R& r, bool a5_extra) {
    uint32_t u0, u4, u12, u16;
    uint8_t  b10;
    if (!r.u32be(u0))  return false;
    if (!r.u32be(u4))  return false;
    if (!r.u8(b10))    return false;
    if (!r.u32be(u12)) return false;
    if (!r.u32be(u16)) return false;
    float v3a[3], v3b[3];
    if (!read_vec3_be(r, v3a)) return false;
    if (!read_vec3_be(r, v3b)) return false;
    if (a5_extra) {
        /* When sub_82B20180's a5 (= v67) is set, the per-sub-entry
           has an additional BE u32 stored at item+8 (as a u16
           via WORD1). */
        uint32_t extra;
        if (!r.u32be(extra)) return false;
        (void)extra;
    }
    (void)u0;(void)u4;(void)b10;(void)u12;(void)u16;
    return true;
}

static bool read_bucket_c_header(R& r, BucketCHeader& ch, bool a5_extra) {
    uint8_t  b0;
    uint32_t u152, u60;
    uint8_t  raw4[4];
    if (!r.u8(b0))      return false;
    if (!r.u32be(u152)) return false;
    if (!r.u32be(u60))  return false;
    if (!r.u8(raw4[0])) return false;  /* 4 raw bytes (no BE swap) */
    if (!r.u8(raw4[1])) return false;
    if (!r.u8(raw4[2])) return false;
    if (!r.u8(raw4[3])) return false;
    if (!r.u32be(ch.index_count))  return false;
    if (!r.u32be(ch.vertex_count)) return false;
    if (!r.u32be(ch.sub_entry_count)) return false;
    if (ch.sub_entry_count > 0x10000) return false;
    for (uint32_t k = 0; k < ch.sub_entry_count; ++k) {
        if (!read_bucket_c_sub_entry(r, a5_extra)) return false;
    }
    /* When a5 is set, an outer/inner index array follows. */
    if (a5_extra) {
        uint32_t outer;
        if (!r.u32be(outer)) return false;
        if (outer > 0x10000) return false;
        for (uint32_t i = 0; i < outer; ++i) {
            uint32_t inner;
            if (!r.u32be(inner)) return false;
            if (inner > 0x10000) return false;
            for (uint32_t j = 0; j < inner; ++j) {
                uint32_t v;
                if (!r.u32be(v)) return false;
                (void)v;
            }
        }
    }
    (void)b0;(void)u152;(void)u60;
    return true;
}

/* sub_82B88AD0 — 60 bytes total. */
static bool read_mesh_class_a(R& r,
                              float vec1[3], float& scalar1,
                              float vec2[3], float vec3[3],
                              uint32_t& part_count,
                              uint32_t part_kind[3],
                              uint32_t& trailing_u32)
{
    if (!read_vec3_be(r, vec1)) return false;
    if (!r.f32be(scalar1))      return false;
    if (!read_vec3_be(r, vec2)) return false;
    if (!read_vec3_be(r, vec3)) return false;
    if (!r.u32be(part_count))   return false;
    if (!r.u32be(part_kind[0])) return false;
    if (!r.u32be(part_kind[1])) return false;
    if (!r.u32be(part_kind[2])) return false;
    if (!r.u32be(trailing_u32)) return false;
    return true;
}

/* sub_82B88E80 — 24 (v≤34) or 28 (v≥35) bytes. Returns Unk2[0]/Unk2[1]
   for downstream UV-format inspection. */
static bool read_mesh_class_b(R& r, uint32_t version,
                              float& unk2_0, float& unk2_1)
{
    uint8_t b76, b77, b78, b79, b88, b89, b98, b99;
    float   flt_80, flt_84, flt_100;
    if (!r.u8(b76))      return false;
    if (!r.u8(b77))      return false;
    if (!r.u8(b78))      return false;
    if (!r.u8(b79))      return false;
    if (!r.f32be(flt_80))return false;
    if (!r.f32be(flt_84))return false;
    if (!r.u8(b88))      return false;
    if (!r.u8(b89))      return false;
    if (!r.f32be(unk2_0))return false;
    if (!r.f32be(unk2_1))return false;
    if (!r.u8(b98))      return false;
    if (!r.u8(b99))      return false;
    if (version > 34) {
        if (!r.f32be(flt_100)) return false;
    }
    (void)b76;(void)b77;(void)b78;(void)b79;(void)b88;(void)b89;(void)b98;(void)b99;
    (void)flt_80;(void)flt_84;(void)flt_100;
    return true;
}

/* sub_82B714A0 — bone section reader called from sub_82AB65B0 at the
   start of every per-part load. Branches on a global flag
   (byte_834BE9E8). We emulate the FALSE-branch path (always-32-byte
   prologue) which is what the file-only flow appears to use; bones
   themselves are read after. Returns total bytes consumed for sanity. */
static bool read_bone_section(R& r, std::vector<MDLBoneInfo>& bones,
                              bool& has_transforms)
{
    /* 4 (float + u32) pairs = 32 bytes */
    for (int k = 0; k < 4; ++k) {
        float fk; uint32_t uk;
        if (!r.f32be(fk)) return false;
        if (!r.u32be(uk)) return false;
        (void)fk; (void)uk;
    }
    /* bone count */
    uint32_t bone_count = 0;
    if (!r.u32be(bone_count)) return false;
    if (bone_count > 0x10000) return false;

    bones.clear();
    bones.reserve(bone_count);
    for (uint32_t i = 0; i < bone_count; ++i) {
        std::string nm;
        if (!r.strz(nm)) return false;
        uint32_t pid = 0;
        if (!r.u32be(pid)) return false;
        MDLBoneInfo b;
        b.Name = std::move(nm);
        b.ParentID = (pid == 0xFFFFFFFFu) ? -1 : (int)pid;
        bones.push_back(std::move(b));
    }

    /* bone transform count */
    uint32_t xform_count = 0;
    if (!r.u32be(xform_count)) return false;
    has_transforms = (xform_count > 0);
    if (xform_count > 0x10000) return false;
    /* each transform = 4 quat floats + 3 trans floats + 3 scale floats
       + 1 u32 = 11 entries × 4 bytes = 44 bytes */
    for (uint32_t i = 0; i < xform_count; ++i) {
        if (!r.skip(44)) return false;
    }
    /* sub_82B710D0 tail-call is in-memory only (CRC) — no file read. */
    return true;
}

/* The fixed-size opening of sub_82AB51F0 — identical 60-byte shape to
   sub_82B88AD0 (3 vec3 + float + 5 × u32), then 1 byte animated flag,
   then 4-byte named-flag-count float. */
struct PartHeader {
    float    vec1[3]  = {0,0,0};
    float    scalar   = 0.0f;
    float    vec2[3]  = {0,0,0};
    float    vec3[3]  = {0,0,0};
    uint32_t count_A  = 0;  /* render queue items */
    uint32_t count_B  = 0;  /* stride 20 regular */
    uint32_t count_C  = 0;  /* stride 28 alt */
    uint32_t count_D  = 0;  /* stride 24 skinned */
    uint32_t count_E  = 0;  /* stride 36 breakable */
    uint8_t  animated = 0;
    float    named_count_f = 0.0f;
};

static bool read_part_header(R& r, PartHeader& ph)
{
    if (!read_vec3_be(r, ph.vec1)) return false;
    if (!r.f32be(ph.scalar))       return false;
    if (!read_vec3_be(r, ph.vec2)) return false;
    if (!read_vec3_be(r, ph.vec3)) return false;
    if (!r.u32be(ph.count_A))      return false;
    if (!r.u32be(ph.count_B))      return false;
    if (!r.u32be(ph.count_C))      return false;
    if (!r.u32be(ph.count_D))      return false;
    if (!r.u32be(ph.count_E))      return false;
    if (!r.u8(ph.animated))        return false;
    if (!r.f32be(ph.named_count_f))return false;
    return true;
}

}  /* namespace */


bool parse_mdl_info_v2(const std::vector<unsigned char>& data,
                       MDLInfo& out,
                       const std::string& file_path)
{
    out = MDLInfo{};
    R r{ data.data(), data.size(), 0 };
    if (r.n < 16) return false;

    /* ---- Section 1: file header (sub_82AB80C8 — 16 bytes) ---- */
    if (std::memcmp(r.p, "MeshFile", 8) != 0 &&
        std::memcmp(r.p, "DefMeshF", 8) != 0) {
        return false;
    }
    out.Magic.assign((const char*)r.p, 8);
    r.skip(8);
    uint32_t version = 0;
    if (!r.u32be(version))           return false;
    if (version < 33 || version > 36) return false;
    uint32_t body_offset = 0;
    if (!r.u32be(body_offset))       return false;
    out.HeaderSize = body_offset;

    /* ---- Section 2: mesh-class block (88 bytes) ---- */
    float vec_a[3], scalar_a, vec_b[3], vec_c[3];
    uint32_t mc_part_count, mc_kind[3], mc_trailing;
    if (!read_mesh_class_a(r, vec_a, scalar_a, vec_b, vec_c,
                           mc_part_count, mc_kind, mc_trailing))
        return false;
    float file_unk2_0 = 0.0f, file_unk2_1 = 0.0f;
    if (!read_mesh_class_b(r, version, file_unk2_0, file_unk2_1))
        return false;

    out.MeshCount = mc_part_count;

    if (mc_part_count == 0)         return true;
    if (mc_part_count > 256)        return false;

    /* ---- Section 3: per-part loop ----
       For each i in [0, mc_part_count): if mc_kind[i] > 0, the part
       is active and gets read; otherwise it's skipped. We capture
       only the first 3 kind slots because that's what fits in the
       mesh-class block. */

    /* Dense LOD index — incremented only when an ACTIVE part (one with
       a non-zero kind) is processed.  The raw part loop counter `pi`
       can have gaps (e.g. b_left_hand.mdl uses parts 0 and 2 with the
       middle slot inactive), so we use this counter for the
       user-facing "|lod<N>" tag.  That way the UI always sees a
       contiguous 0..lod_count-1 range and every LOD button maps to
       a real, populated LOD. */
    uint32_t lod_dense_idx = 0;

    for (uint32_t pi = 0; pi < mc_part_count; ++pi) {
        uint32_t kind = (pi < 3) ? mc_kind[pi] : 0;
        if (kind == 0) {
            /* inactive — no file bytes consumed for this slot. */
            continue;
        }

        /* Step 1: bone section (sub_82B714A0).
           For files with 0 bones this is 32 + 4 + 4 = 40 bytes.
           Bones are part-local in IDA, but we accumulate them into
           the global MDLInfo for downstream skinning consumers. */
        if (pi == 0) {
            /* Only emit bones once even if multiple parts feed them. */
            bool has_xforms = false;
            if (!read_bone_section(r, out.Bones, has_xforms))
                return false;
            out.BoneCount = (uint32_t)out.Bones.size();
            out.HasBoneTransforms = has_xforms;
        } else {
            /* For later parts, consume but discard their bone block
               so the stream alignment stays correct. */
            std::vector<MDLBoneInfo> tmp_bones;
            bool tmp_has = false;
            if (!read_bone_section(r, tmp_bones, tmp_has)) return false;
        }

        /* Step 2: part header (60 + 5 bytes) */
        PartHeader ph;
        if (!read_part_header(r, ph)) return false;

        /* Step 3: named-flag loop */
        uint32_t named_count = (uint32_t)ph.named_count_f;
        if (named_count > 0x10000) return false;
        for (uint32_t n = 0; n < named_count; ++n) {
            std::string s;
            if (!r.strz(s)) return false;
            uint8_t flg = 0;
            if (!r.u8(flg)) return false;
            (void)s; (void)flg;
        }

        /* Step 4: bbox block (sub_82AB6CC0).  6 u32 (24 bytes) then
           bbox_u[5] BE floats. bbox_u[4] (at offset +16 of the block)
           is the secondary-VB "stride / 16" used by bucket B. */
        uint32_t bbox_u[6] = {0,0,0,0,0,0};
        for (int k = 0; k < 6; ++k) {
            if (!r.u32be(bbox_u[k])) return false;
        }
        uint32_t bbox_float_count = bbox_u[5];
        if (bbox_float_count > 0x10000) return false;
        for (uint32_t k = 0; k < bbox_float_count; ++k) {
            float f; if (!r.f32be(f)) return false;
            (void)f;
        }
        const uint32_t secondary_vb_stride_units = bbox_u[4];

        /* Step 5: string list — texture paths */
        uint32_t str_count = 0;
        if (!r.u32be(str_count)) return false;
        if (str_count > 0x10000) return false;
        std::vector<std::string> strings;
        strings.reserve(str_count);
        for (uint32_t k = 0; k < str_count; ++k) {
            std::string s;
            if (!r.strz(s)) return false;
            strings.push_back(std::move(s));
        }

        /* Build the part's material pool from the string list — IDA
           shows the per-part string list is "N strings of length
           variable", with no implied grouping (groups come from the
           bucket A items). For now group in 5-tuples
           (diffuse/spec/normal/metallic/extra) when the count divides
           evenly; else 1 string per material. The pool is shared by
           every bucket-B submesh in this part. */
        std::vector<MDLMaterialInfo> part_mat_pool;
        if (str_count > 0 && (str_count % 5) == 0) {
            uint32_t mat_count = str_count / 5;
            for (uint32_t m = 0; m < mat_count; ++m) {
                MDLMaterialInfo mat;
                mat.DiffuseTexName  = strings[m*5 + 0];
                mat.SpecularTexName = strings[m*5 + 1];
                mat.NormalTexName   = strings[m*5 + 2];
                mat.MetallicTexName = strings[m*5 + 3];
                mat.ExtraTexName    = strings[m*5 + 4];
                std::memcpy(&mat.Unk2[0], &file_unk2_0, 4);
                std::memcpy(&mat.Unk2[1], &file_unk2_1, 4);
                part_mat_pool.push_back(std::move(mat));
            }
        } else {
            for (auto& s : strings) {
                MDLMaterialInfo mat;
                mat.DiffuseTexName = s;
                part_mat_pool.push_back(std::move(mat));
            }
        }

        /* The mesh-class block's part loop iterates over up to N
           active parts.  In files where the model has multiple LODs,
           each part is a separate LOD (sharing materials but with
           different polygon counts).  We tag every emitted mesh's
           name with "|lod<N>" so the preview can group and filter
           them per-LOD without inspecting the file format directly.

           Use the DENSE index so the LOD buttons in the UI are
           contiguous even when the file leaves gaps (kind == 0 in
           a middle slot). */
        const std::string lod_tag = "|lod" + std::to_string(lod_dense_idx);

        /* If this part has no bucket-B geometry, still emit one mesh
           entry so the metadata is visible to the asset browser. */
        if (ph.count_B == 0) {
            MDLMeshInfo mesh;
            mesh.MeshName = "part_" + std::to_string(pi) + lod_tag;
            mesh.Materials = part_mat_pool;
            mesh.MaterialCount = (uint32_t)part_mat_pool.size();
            out.Meshes.push_back(std::move(mesh));
        }

        /* Step 6: bucket A loop — render-queue items.

           Per-item IDA-traced byte sequence (sub_82AB51F0 inner loop):
             u32 v141                                   (4 bytes)
             if count_C == 0:
                 if v141 is NaN bit pattern:
                     u32 v210                           (4 bytes)
                     case_sel = v210, vtable_idx = 20
                 else:
                     case_sel = v141, vtable_idx = 16
                 switch (int)case_sel:
                   case 0:
                   case 3:  alloc 176, sub_82B1E210() then vtable[16]
                            = sub_82B1E2F8: strz name + 45 bytes +
                              u32 mat_count + N × (9 strz + 2 BE f32)
                   case 1:  alloc 208 / vtable off_82002B78  TODO
                   case 2:  alloc 224 / vtable from sub_82B233E0  TODO
             else (count_C > 0):
                 alloc 192 / vtable off_820029F0  TODO

           For now: handle the case-0/3 path (the standard material entry,
           which is what b_left_hand.mdl uses); bail cleanly on the rest. */
        bool bucket_a_ok = true;
        for (uint32_t a = 0; a < ph.count_A && bucket_a_ok; ++a) {
            uint32_t v141 = 0;
            if (!r.u32be(v141)) return false;

            uint32_t case_sel = v141;
            bool nan_sentinel = false;
            if (ph.count_C != 0) {
                /* count_C > 0 path: vtable[16] of off_820029F0, which
                   is just a thunk to sub_82B1E2F8 (the case 0/3
                   reader).  No switch, no NaN sentinel. */
                std::string item_name;
                if (!read_bucket_a_item_case03(r, item_name)) return false;
                (void)item_name;
                (void)case_sel; (void)nan_sentinel;
                continue;
            }

            uint32_t exp_bits = (v141 >> 23) & 0xFF;
            uint32_t mant     = v141 & 0x7FFFFF;
            nan_sentinel = (exp_bits == 0xFF && mant != 0);
            if (nan_sentinel) {
                uint32_t v210 = 0;
                if (!r.u32be(v210)) return false;
                case_sel = v210;
            }

            int32_t case_int = (int32_t)case_sel;
            if (case_int == 0 || case_int == 3) {
                std::string item_name;
                if (!read_bucket_a_item_case03(r, item_name)) return false;
                (void)item_name;
            } else if (case_int == 1) {
                if (!read_bucket_a_item_case1(r)) return false;
            } else if (case_int == 2) {
                if (!read_bucket_a_item_case2(r)) return false;
            } else {
                /* unknown case — bail rather than corrupt the stream. */
                bucket_a_ok = false;
                break;
            }
        }
        if (!bucket_a_ok) {
            /* stream is now misaligned for the remaining buckets — return
               the metadata we have rather than parse garbage. */
            return true;
        }

        /* Step 7a: bucket B (stride 20) submesh loop.

           After the header (already traced above), each submesh has:
             a) if !is_animated_with_bones:
                  vertex_count × 20 bytes  (primary VB, raw)
             b) index_count × u16 BE       (index buffer)
             c) if secondary_vb_stride_units > 0:
                  vertex_count × 16 × secondary_vb_stride_units bytes
                                            (secondary VB, raw)
             d) BE f32 trigger
             e) if trigger != 0:
                  sub_82B31CA0(...)  ← animated extras (NOT YET TRACED)

           The "is animated with bones" heuristic: mesh has bones AND
           the part is animated. This mirrors the game's
             v159 = (a4 == 0) || (bone_count == 0) ? 1 : 0
             v160 = v159  ← a4 of sub_82B2BCC8
             if (a4 of bucket B != 0) read primary VB
           where a4 of sub_82AB51F0 is the mesh-level animated flag.

           Bail cleanly on trigger != 0 — the rest of the bucket B
           body is fine to skip but the trailer cannot yet be
           consumed without sub_82B31CA0. */
        const bool is_animated_with_bones =
            (out.Bones.size() > 0) && (ph.animated != 0);

        /* The game accumulates a "v158" counter through bucket B and
           bucket E to size the per-part v68 (animated) unified VB at
           the tail of sub_82AB51F0 — `v158 = sum(B.vertex_count) +
           sum(E.vertex_count)`, then `unified_vb_bytes = v158 * 4`. */
        uint32_t v158_accum = 0;

        bool bucket_b_ok = true;
        for (uint32_t b = 0; b < ph.count_B && bucket_b_ok; ++b) {
            BucketBHeader bh;
            if (!read_bucket_b_header(r, bh)) return false;

            /* Step (a): primary VB.  IDA shows sub_82B850B8 allocates
               vertex_count × 20 bytes; the file payload is copied raw.
               Record the file offset so parse_mdl_geometry_v2 can
               decode it later. Skipped when animated-with-bones (in
               which case the VB lives in the unified tail block). */
            size_t vert_off = 0;
            if (!is_animated_with_bones) {
                vert_off = r.i;
                if (!r.skip((size_t)bh.vertex_count * 20)) return false;
            }

            /* Step (b): index buffer — index_count × u16 BE. */
            size_t face_off = r.i;
            for (uint32_t k = 0; k < bh.index_count; ++k) {
                uint16_t idx;
                if (!r.u16be(idx)) return false;
                (void)idx;
            }

            /* Step (c): secondary VB. */
            if (secondary_vb_stride_units > 0) {
                size_t sec_bytes =
                    (size_t)bh.vertex_count * 16u * secondary_vb_stride_units;
                if (!r.skip(sec_bytes)) return false;
            }

            /* Step (d): trigger. */
            float trigger = 0.0f;
            if (!r.f32be(trigger)) return false;

            /* Emit one MDLMeshInfo + one MDLMeshBufferInfo per
               bucket-B submesh, in 1:1 lockstep.  The geometry
               decoder relies on this pairing. */
            /* If trigger != 0, an animated trailer follows that holds
               the actual per-vertex bind-pose data (positions etc.).
               Read it through sub_82B31CA0; if it succeeds, use its
               position block as our VB. */
            size_t   anim_pos_off = 0;
            uint32_t anim_vert_count = 0;
            bool     anim_used = false;
            if (trigger != 0.0f) {
                B31CA0Result tr;
                if (!read_b31ca0_trailer(r, version, secondary_vb_stride_units, tr))
                    return true;
                anim_pos_off    = tr.pos_off;
                anim_vert_count = tr.vertex_count;
                anim_used       = true;
            }

            MDLMeshInfo mesh;
            std::string base_name = bh.name.empty()
                                ? ("part_" + std::to_string(pi) +
                                   "_sub_" + std::to_string(b))
                                : bh.name;
            mesh.MeshName = base_name + lod_tag;
            mesh.Materials = part_mat_pool;
            mesh.MaterialCount = (uint32_t)part_mat_pool.size();
            out.Meshes.push_back(std::move(mesh));

            MDLMeshBufferInfo mb;
            mb.SubMeshCount   = 1;
            mb.IsAltPath      = false;
            mb.IsFoliagePath  = true;
            mb.FaceCount      = bh.index_count;
            mb.FaceOffset     = face_off;
            if (anim_used) {
                /* Animated mesh: positions live in the sub_82B31CA0
                   trailer as 12-byte BE vec3 per vertex. Use stride
                   code 12 to signal "BE-float vec3 positions only". */
                mb.VertexCount         = anim_vert_count;
                mb.VertexOffset        = anim_pos_off;
                mb.FoliageVertexStride = 12;
            } else if (!is_animated_with_bones) {
                mb.VertexCount         = bh.vertex_count;
                mb.VertexOffset        = vert_off;
                mb.FoliageVertexStride = 20;
            } else {
                /* Animated bucket B without trigger — no positions
                   reachable here. Empty geometry. */
                mb.VertexCount         = bh.vertex_count;
                mb.VertexOffset        = 0;
                mb.FoliageVertexStride = 0;
            }
            mb.MeshIndex      = (uint32_t)out.MeshBuffers.size();
            out.MeshBuffers.push_back(std::move(mb));

            v158_accum += bh.vertex_count;
        }
        if (!bucket_b_ok) {
            return true;
        }

        /* Step 7b: bucket C loop (stride 28, alt path).  IDA shows
           sub_82B20180 reads header + sub-entries then optionally a
           a5-conditional block (v67==1 case — skipped here since it
           only applies when count_C>0 AND mesh-level a5==33), then
           primary VB (a4-conditional), IB, secondary VB, trigger1,
           trigger2.  Bail cleanly on either trigger. */
        /* v67 of sub_82AB51F0 = (count_C > 0 && mesh-level a5 == 33).
           mesh-level a5 is the file `version` we already parsed.
           When set, bucket C has per-sub-entry extra u32 and an
           outer/inner index array. */
        const bool bucket_c_a5_extra = (ph.count_C > 0) && (version == 33);

        bool bucket_c_ok = true;
        for (uint32_t c = 0; c < ph.count_C && bucket_c_ok; ++c) {
            BucketCHeader ch;
            if (!read_bucket_c_header(r, ch, bucket_c_a5_extra)) return false;

            size_t vert_off_c = 0;
            if (!is_animated_with_bones) {
                vert_off_c = r.i;
                if (!r.skip((size_t)ch.vertex_count * 28)) return false;
            }
            size_t face_off_c = r.i;
            for (uint32_t k = 0; k < ch.index_count; ++k) {
                uint16_t idx;
                if (!r.u16be(idx)) return false;
                (void)idx;
            }
            if (secondary_vb_stride_units > 0) {
                size_t sec_bytes =
                    (size_t)ch.vertex_count * 16u * secondary_vb_stride_units;
                if (!r.skip(sec_bytes)) return false;
            }

            MDLMeshInfo mesh;
            mesh.MeshName = "part_" + std::to_string(pi) +
                            "_c" + std::to_string(c) + lod_tag;
            mesh.Materials = part_mat_pool;
            mesh.MaterialCount = (uint32_t)part_mat_pool.size();
            out.Meshes.push_back(std::move(mesh));

            MDLMeshBufferInfo mb;
            mb.VertexCount        = ch.vertex_count;
            mb.VertexOffset       = vert_off_c;
            mb.FaceCount          = ch.index_count;
            mb.FaceOffset         = face_off_c;
            mb.SubMeshCount       = 1;
            mb.IsAltPath          = false;
            mb.IsFoliagePath      = true;
            mb.FoliageVertexStride = is_animated_with_bones ? 0u : 28u;
            mb.MeshIndex          = (uint32_t)out.MeshBuffers.size();
            out.MeshBuffers.push_back(std::move(mb));

            uint32_t trigger1 = 0;
            if (!r.u32be(trigger1)) return false;
            bool c_anim_used = false;
            size_t   c_anim_pos = 0;
            uint32_t c_anim_vc  = 0;
            if (trigger1 != 0) {
                B31CA0Result tr;
                if (!read_b31ca0_trailer(r, version,
                                         secondary_vb_stride_units, tr))
                    return true;
                c_anim_used = true;
                c_anim_pos  = tr.pos_off;
                c_anim_vc   = tr.vertex_count;
            }
            uint32_t trigger2 = 0;
            if (!r.u32be(trigger2)) return false;
            if (trigger2 != 0) {
                if (!read_sub_82B89640_trailer(r)) return true;
            }

            /* When bucket C is animated (is_animated_with_bones) the
               primary VB was skipped — swap in the trailer positions
               if we got them. */
            if (is_animated_with_bones && c_anim_used) {
                MDLMeshBufferInfo& last = out.MeshBuffers.back();
                last.VertexCount         = c_anim_vc;
                last.VertexOffset        = c_anim_pos;
                last.FoliageVertexStride = 12;
            }
        }
        if (!bucket_c_ok) {
            return true;
        }

        /* Step 7c: bucket E loop (stride 36, breakable/tree).  IDA's
           sub_82B30B50 unconditionally allocates+reads primary VB +
           IB + optional secondary VB.  No triggers. */
        for (uint32_t e = 0; e < ph.count_E; ++e) {
            BucketEHeader eh;
            if (!read_bucket_e_header(r, eh)) return false;

            size_t vert_off_e = r.i;
            if (!r.skip((size_t)eh.vertex_count * 36)) return false;

            size_t face_off_e = r.i;
            for (uint32_t k = 0; k < eh.index_count; ++k) {
                uint16_t idx;
                if (!r.u16be(idx)) return false;
                (void)idx;
            }
            if (secondary_vb_stride_units > 0) {
                size_t sec_bytes =
                    (size_t)eh.vertex_count * 16u * secondary_vb_stride_units;
                if (!r.skip(sec_bytes)) return false;
            }

            MDLMeshInfo mesh;
            mesh.MeshName = "part_" + std::to_string(pi) +
                            "_e" + std::to_string(e) + lod_tag;
            mesh.Materials = part_mat_pool;
            mesh.MaterialCount = (uint32_t)part_mat_pool.size();
            out.Meshes.push_back(std::move(mesh));

            MDLMeshBufferInfo mb;
            mb.VertexCount        = eh.vertex_count;
            mb.VertexOffset       = vert_off_e;
            mb.FaceCount          = eh.index_count;
            mb.FaceOffset         = face_off_e;
            mb.SubMeshCount       = 1;
            mb.IsAltPath          = false;
            mb.IsFoliagePath      = true;
            mb.FoliageVertexStride = 36;
            mb.MeshIndex          = (uint32_t)out.MeshBuffers.size();
            out.MeshBuffers.push_back(std::move(mb));

            v158_accum += eh.vertex_count;
        }

        /* Step 7d: bucket D loop (stride 24, skinned LOD via
           sub_82B1D7C8).  File stores 48 bytes per source vertex
           (vec3 pos + vec3 nrm/tan + 2 BE f32 UV + vec4 blend) and
           item+16 u16 BE indices.  We capture the raw 48-byte
           source-vertex array; the geometry decoder pulls position +
           UV out of it. */
        for (uint32_t d = 0; d < ph.count_D; ++d) {
            BucketDHeader dh;
            if (!read_bucket_d_header(r, dh)) return false;

            size_t vert_off_d = r.i;
            if (!r.skip((size_t)dh.source_vert_count * 48)) return false;

            size_t face_off_d = r.i;
            for (uint32_t k = 0; k < dh.base_idx_count; ++k) {
                uint16_t idx;
                if (!r.u16be(idx)) return false;
                (void)idx;
            }

            MDLMeshInfo mesh;
            mesh.MeshName = "part_" + std::to_string(pi) +
                            "_d" + std::to_string(d) + lod_tag;
            mesh.Materials = part_mat_pool;
            mesh.MaterialCount = (uint32_t)part_mat_pool.size();
            out.Meshes.push_back(std::move(mesh));

            MDLMeshBufferInfo mb;
            mb.VertexCount        = dh.source_vert_count;
            mb.VertexOffset       = vert_off_d;
            mb.FaceCount          = dh.base_idx_count;
            mb.FaceOffset         = face_off_d;
            mb.SubMeshCount       = 1;
            mb.IsAltPath          = false;
            mb.IsFoliagePath      = true;
            /* Special stride code 48 = "bucket D source-vertex array":
                 bytes 0..11   = position (3 BE f32, NOT half)
                 bytes 24..31  = UV     (2 BE f32, NOT half) */
            mb.FoliageVertexStride = 48;
            mb.MeshIndex          = (uint32_t)out.MeshBuffers.size();
            out.MeshBuffers.push_back(std::move(mb));
        }

        /* Step 8: per-part v68 trailer (unified animated VB).
           sub_82AB51F0 reads `v158 * 4` raw bytes — supplemental
           skinning data (4 bytes / source vertex, summed over the
           part's bucket B and bucket E vertex counts). */
        if (ph.animated && v158_accum > 0) {
            if (!r.skip((size_t)v158_accum * 4u)) return false;
        }

        /* This part was active — advance the dense LOD index so the
           next active part gets the next contiguous LOD number. */
        ++lod_dense_idx;
    }

    /* End of part loop. */
    return true;
}


/* ---------------------------------------------------------------- */
/* V2 geometry decoder.                                              */
/*                                                                   */
/* Inputs come from parse_mdl_info_v2 which populated MeshBuffers    */
/* in 1:1 lockstep with Meshes.  Each MeshBuffer's VertexOffset /    */
/* FaceOffset point directly into the file's raw VB / IB bytes the   */
/* game's sub_82B850B8 allocation would have copied to GPU memory.   */
/*                                                                   */
/* The CPU side of the game never decodes vertices — the GPU does    */
/* it via vfetch_full in the bound vertex shader.  For our preview   */
/* we need a CPU decode.  The byte-layout we apply per stride is     */
/* the standard Xbox 360 compact format used across Fable 2 meshes:  */
/*                                                                   */
/*   stride 20 (bucket B, regular):                                  */
/*     0..5   3 × half (BE)         position.xyz                     */
/*     6..11  packed bone/weight/normal data                         */
/*     12..15 2 × half (BE)         uv.uv                            */
/*     16..19 packed tangent / extra                                 */
/*                                                                   */
/* Indices are u16 BE, written sequentially after the VB.  An entry  */
/* of 0xFFFF marks a strip-restart in a triangle-strip primitive.    */
/* ---------------------------------------------------------------- */

namespace {

static float half_be_to_float(const uint8_t* p) {
    uint16_t h = (uint16_t(p[0]) << 8) | uint16_t(p[1]);
    uint32_t sign = (h & 0x8000u) << 16;
    uint32_t exp  = (h & 0x7C00u) >> 10;
    uint32_t mant = (h & 0x03FFu);
    uint32_t fbits;
    if (exp == 0) {
        if (mant == 0) {
            fbits = sign;
        } else {
            /* denormal — normalise */
            while ((mant & 0x0400u) == 0) { mant <<= 1; exp = uint32_t(-1); --exp; }
            ++exp;
            mant &= ~0x0400u;
            fbits = sign | ((exp + 112u) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        fbits = sign | 0x7F800000u | (mant << 13);
    } else {
        fbits = sign | ((exp + 112u) << 23) | (mant << 13);
    }
    float f;
    std::memcpy(&f, &fbits, 4);
    return f;
}

static void build_tris_from_strip(const std::vector<uint16_t>& strip,
                                  std::vector<uint32_t>& out_idx)
{
    out_idx.clear();
    bool cw = true;
    for (size_t i = 2; i < strip.size(); ++i) {
        uint16_t a = strip[i - 2], b = strip[i - 1], c = strip[i];
        if (a == 0xFFFF || b == 0xFFFF || c == 0xFFFF) {
            /* primitive restart — reset the strip "phase" */
            cw = ((i + 1) & 1) == 0;
            continue;
        }
        if (a == b || b == c || a == c) { cw = !cw; continue; }
        if (cw) { out_idx.push_back(a); out_idx.push_back(b); out_idx.push_back(c); }
        else    { out_idx.push_back(b); out_idx.push_back(a); out_idx.push_back(c); }
        cw = !cw;
    }
}

static void compute_smooth_normals(uint32_t vcount,
                                   const std::vector<uint32_t>& idx,
                                   const std::vector<float>& pos,
                                   std::vector<float>& out_n)
{
    out_n.assign((size_t)vcount * 3, 0.0f);
    size_t tcount = idx.size() / 3;
    for (size_t t = 0; t < tcount; ++t) {
        uint32_t ia = idx[t*3+0], ib = idx[t*3+1], ic = idx[t*3+2];
        if (ia >= vcount || ib >= vcount || ic >= vcount) continue;
        float ax=pos[ia*3+0], ay=pos[ia*3+1], az=pos[ia*3+2];
        float bx=pos[ib*3+0], by=pos[ib*3+1], bz=pos[ib*3+2];
        float cx=pos[ic*3+0], cy=pos[ic*3+1], cz=pos[ic*3+2];
        float ux=bx-ax, uy=by-ay, uz=bz-az;
        float vx=cx-ax, vy=cy-ay, vz=cz-az;
        float nx=uy*vz - uz*vy;
        float ny=uz*vx - ux*vz;
        float nz=ux*vy - uy*vx;
        for (uint32_t v : {ia, ib, ic}) {
            out_n[v*3+0] += nx;
            out_n[v*3+1] += ny;
            out_n[v*3+2] += nz;
        }
    }
    for (uint32_t v = 0; v < vcount; ++v) {
        float x=out_n[v*3+0], y=out_n[v*3+1], z=out_n[v*3+2];
        float l = std::sqrt(x*x + y*y + z*z);
        if (l > 1e-6f) {
            out_n[v*3+0] = x / l;
            out_n[v*3+1] = y / l;
            out_n[v*3+2] = z / l;
        } else {
            out_n[v*3+0] = 0.0f; out_n[v*3+1] = 1.0f; out_n[v*3+2] = 0.0f;
        }
    }
}

} /* namespace */


bool parse_mdl_geometry_v2(const std::vector<unsigned char>& data,
                           const MDLInfo& info,
                           std::vector<MDLMeshGeom>& out)
{
    out.clear();
    const uint8_t* p_base = data.data();
    const size_t   p_size = data.size();

    for (size_t mi = 0; mi < info.MeshBuffers.size(); ++mi) {
        const auto& mb = info.MeshBuffers[mi];
        MDLMeshGeom g;

        /* Pull the displayed name + textures from the paired
           MDLMeshInfo (1:1 with MeshBuffers, set by parse_mdl_info_v2). */
        if (mi < info.Meshes.size()) {
            const auto& mesh = info.Meshes[mi];
            g.name = mesh.MeshName.empty()
                         ? ("mesh_" + std::to_string(mi))
                         : mesh.MeshName;
            if (!mesh.Materials.empty()) {
                const auto& m0 = mesh.Materials[0];
                g.diffuse_tex_name  = m0.DiffuseTexName;
                g.normal_tex_name   = m0.NormalTexName;
                g.specular_tex_name = m0.SpecularTexName;
                g.metallic_tex_name = m0.MetallicTexName;
                g.extra_tex_name    = m0.ExtraTexName;
            }
        } else {
            g.name = "mesh_" + std::to_string(mi);
        }
        g.MeshIndex = (uint32_t)mi;
        g.SubMeshIndex = 0;

        /* Supported strides from the IDA-traced bucket readers:
              12  = animated bucket B (sub_82B31CA0 trailer pos block;
                                       3 × BE f32 position per vertex,
                                       no UV available yet)
              20  = bucket B regular  (half BE pos/uv)
              28  = bucket C alt      (half BE pos, UV @ +20)
              36  = bucket E tree     (half BE pos, UV @ +12)
              48  = bucket D skinned  (BE float pos @0..11, UV @ 24..31)
              0   = unhandled — empty geometry */
        const uint32_t stride = mb.FoliageVertexStride;
        if ((stride != 12 && stride != 20 && stride != 28 &&
             stride != 36 && stride != 48) ||
            mb.VertexCount == 0 || mb.FaceCount == 0)
        {
            out.push_back(std::move(g));
            continue;
        }

        const size_t vb_end = mb.VertexOffset + (size_t)mb.VertexCount * stride;
        const size_t ib_end = mb.FaceOffset   + (size_t)mb.FaceCount   * 2;
        if (vb_end > p_size || ib_end > p_size) {
            out.push_back(std::move(g));
            continue;
        }

        g.positions.resize((size_t)mb.VertexCount * 3);
        g.uvs      .resize((size_t)mb.VertexCount * 2);
        const uint8_t* vp = p_base + mb.VertexOffset;

        auto be_f32 = [](const uint8_t* p) {
            uint32_t u = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
                         (uint32_t(p[2]) <<  8) |  uint32_t(p[3]);
            float f; std::memcpy(&f, &u, 4); return f;
        };

        if (stride == 12) {
            /* Animated bucket B: positions in sub_82B31CA0's first
               per-vertex block (12 BE f32 / vertex at VertexOffset).
               The trailer's per-vertex layout in the file (IDA order):

                 0   12*N    positions (3 BE f32)
                 12*N 8*N    weights   (8 bytes / vertex)
                 20*N 4*N    bone idx  (4 bytes / vertex)
                 24*N 16*N   UV+nrm+tan (16 bytes / vertex)
                 40*N 4*N    extra     (4 bytes / vertex)

               UVs are the first 4 bytes (2 BE half) of the 16-byte
               block. */
            const size_t uv_off = mb.VertexOffset + (size_t)mb.VertexCount * 24;
            const bool uv_ok    = uv_off + (size_t)mb.VertexCount * 16 <= p_size;
            for (uint32_t v = 0; v < mb.VertexCount; ++v) {
                const uint8_t* vrec = vp + v * 12;
                g.positions[v*3+0] = be_f32(vrec + 0);
                g.positions[v*3+1] = be_f32(vrec + 4);
                g.positions[v*3+2] = be_f32(vrec + 8);
                if (uv_ok) {
                    const uint8_t* urec = p_base + uv_off + v * 16;
                    g.uvs[v*2+0] = half_be_to_float(urec + 0);
                    g.uvs[v*2+1] = half_be_to_float(urec + 2);
                } else {
                    g.uvs[v*2+0] = 0.0f;
                    g.uvs[v*2+1] = 0.0f;
                }
            }
        } else if (stride == 48) {
            /* Bucket D: per-vertex 48 bytes
                  0..11   3 × BE f32 position
                  12..23  3 × BE f32 normal/tangent
                  24..31  2 × BE f32 uv
                  32..47  4 × BE f32 blend / skinning */
            for (uint32_t v = 0; v < mb.VertexCount; ++v) {
                const uint8_t* vrec = vp + v * 48;
                g.positions[v*3+0] = be_f32(vrec + 0);
                g.positions[v*3+1] = be_f32(vrec + 4);
                g.positions[v*3+2] = be_f32(vrec + 8);
                g.uvs[v*2+0]       = be_f32(vrec + 24);
                g.uvs[v*2+1]       = be_f32(vrec + 28);
            }
        } else {
            const uint8_t uv_off =
                (stride == 28) ? 20 : 12;
            for (uint32_t v = 0; v < mb.VertexCount; ++v) {
                const uint8_t* vrec = vp + v * stride;
                g.positions[v*3+0] = half_be_to_float(vrec + 0);
                g.positions[v*3+1] = half_be_to_float(vrec + 2);
                g.positions[v*3+2] = half_be_to_float(vrec + 4);
                g.uvs[v*2+0]       = half_be_to_float(vrec + uv_off);
                g.uvs[v*2+1]       = half_be_to_float(vrec + uv_off + 2);
            }
        }

        /* Indices: u16 BE.  Detect strip vs triangle list by the
           presence of a 0xFFFF restart sentinel. */
        std::vector<uint16_t> strip(mb.FaceCount);
        const uint8_t* fp = p_base + mb.FaceOffset;
        bool has_restart = false;
        for (uint32_t i = 0; i < mb.FaceCount; ++i) {
            uint16_t w = (uint16_t(fp[i*2 + 0]) << 8) | uint16_t(fp[i*2 + 1]);
            strip[i] = w;
            if (w == 0xFFFFu) has_restart = true;
        }
        if (has_restart || (strip.size() % 3) != 0) {
            std::vector<uint32_t> tris;
            build_tris_from_strip(strip, tris);
            g.indices = std::move(tris);
        } else {
            g.indices.resize(strip.size());
            for (size_t i = 0; i < strip.size(); ++i) {
                g.indices[i] = strip[i];
            }
        }

        compute_smooth_normals(mb.VertexCount, g.indices, g.positions, g.normals);

        out.push_back(std::move(g));
    }
    return true;
}
