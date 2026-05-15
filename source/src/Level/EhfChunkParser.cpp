#include "EhfChunkParser.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace Level {

namespace {

constexpr char   kMagic[]   = "HeightFieldGraphicsFile";
constexpr size_t kMagicLen  = sizeof(kMagic) - 1;   
constexpr size_t kHeaderLen = 63;

inline uint32_t be_u32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16)
         | (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
}
inline float be_f32(const uint8_t* p) {
    uint32_t u = be_u32(p);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

/* The body is a stream we walk forwards through.  Tracks position +
   provides typed reads with bounds checks.                          */
struct Walker {
    const uint8_t* p;
    size_t         n;
    size_t         pos = 0;
    std::string    err;

    bool need(size_t k) {
        if (pos + k > n) {
            std::ostringstream os;
            os << "out of body at 0x" << std::hex << pos
               << " (needed " << std::dec << k << ", have "
               << (n - pos) << ")";
            err = os.str();
            return false;
        }
        return true;
    }
    bool u32(uint32_t& v) {
        if (!need(4)) return false;
        v = be_u32(p + pos); pos += 4; return true;
    }
    bool f32(float& v) {
        if (!need(4)) return false;
        v = be_f32(p + pos); pos += 4; return true;
    }
    bool u8(uint8_t& v) {
        if (!need(1)) return false;
        v = p[pos++]; return true;
    }
    bool skip(size_t k) {
        if (!need(k)) return false;
        pos += k; return true;
    }
    bool null_str(std::string& s, size_t max_len = 256) {
        size_t start = pos;
        size_t end = pos;
        while (end < n && end < start + max_len && p[end] != 0) ++end;
        if (end >= n || end == start + max_len) {
            std::ostringstream os;
            os << "null string overrun at 0x" << std::hex << pos;
            err = os.str();
            return false;
        }
        s.assign(reinterpret_cast<const char*>(p + start), end - start);
        pos = end + 1;
        return true;
    }
};

/* Skip past a .tex blob in the stream.  Handles both zlib-compressed
   blobs (PF=24/35/39/40/99 — header 92B + comp_size zlib bytes) and
   uncompressed blobs (PF=98 — header 88B + raw_size raw bytes).     */
bool skip_tex_blob(Walker& w) {
    const size_t tex_start = w.pos;
    if (!w.need(0x60)) return false;
    uint32_t magic = be_u32(w.p + w.pos);
    if (magic != 0xFFFFFFFEu) {
        std::ostringstream os;
        os << "bad .tex magic 0x" << std::hex << magic
           << " at body 0x" << w.pos;
        w.err = os.str();
        return false;
    }
    uint32_t pf = be_u32(w.p + w.pos + 0x18);
    uint32_t mt = be_u32(w.p + w.pos + 0x20);
    if (mt > 0x100) {
        std::ostringstream os;
        os << "implausible mt 0x" << std::hex << mt
           << " in .tex at body 0x" << w.pos;
        w.err = os.str();
        return false;
    }
    
    if (!w.need(mt + 8)) return false;
    uint32_t raw_size = be_u32(w.p + tex_start + mt);

    if (pf == 98u) {
        
        
        w.pos = tex_start + mt + 4 + raw_size;
    } else {
        
        
        uint32_t comp_size = be_u32(w.p + tex_start + mt + 4);
        w.pos = tex_start + mt + 8 + comp_size;
    }
    if (w.pos > w.n) {
        std::ostringstream os;
        os << "tex blob overruns body (pf=" << pf
           << " end=" << w.pos << " body=" << w.n << ")";
        w.err = os.str();
        return false;
    }
    return true;
}

}  


bool ParseEhfBody(const std::vector<uint8_t>& ehf, EhfParsedBody& out)
{
    out = {};

    if (ehf.size() < kHeaderLen ||
        std::memcmp(ehf.data(), kMagic, kMagicLen) != 0)
    {
        out.error = "bad .ehf header";
        return false;
    }
    uint32_t body_off  = be_u32(ehf.data() + 55);
    uint32_t body_size = be_u32(ehf.data() + 59);
    if (uint64_t(body_off) + body_size > ehf.size()) {
        out.error = "body extent past end of file";
        return false;
    }

    Walker w;
    w.p = ehf.data() + body_off;
    w.n = body_size;

    /* Step 1: 2 textures (lightmap + BC5 normal). */
    if (!skip_tex_blob(w)) { out.error = "tex[0]: " + w.err; return false; }
    if (!skip_tex_blob(w)) { out.error = "tex[1]: " + w.err; return false; }

    /* Step 2: 1 float → state[+176]. */
    float dummy_f;
    if (!w.f32(dummy_f)) { out.error = "float: " + w.err; return false; }

    /* Step 3: sub_82A850A0 vector — count + N entries.  Each entry:
       4 × u32 reads (f_a, f_b, w_sub, h_sub), then w_sub*h_sub*160 grid
       stream, then 24B trailer.                                       */
    {
        uint32_t cnt;
        if (!w.u32(cnt)) { out.error = "850A0 count: " + w.err; return false; }
        if (cnt > 10000) { out.error = "850A0 count implausible"; return false; }
        for (uint32_t k = 0; k < cnt; ++k) {
            float f_a, f_b;
            uint32_t w_sub, h_sub;
            if (!w.f32(f_a) || !w.f32(f_b) ||
                !w.u32(w_sub) || !w.u32(h_sub))
            {
                out.error = "850A0 entry header: " + w.err;
                return false;
            }
            if (w_sub > 1024 || h_sub > 1024) {
                out.error = "850A0 sub dims implausible";
                return false;
            }
            if (!w.skip(size_t(w_sub) * size_t(h_sub) * 160 + 24)) {
                out.error = "850A0 grid: " + w.err;
                return false;
            }
        }
    }

    /* Step 4: sub_82A860E8 — 1 float + count + N × 18B entries. */
    {
        float f;
        uint32_t cnt;
        if (!w.f32(f))   { out.error = "860E8 float: " + w.err; return false; }
        if (!w.u32(cnt)) { out.error = "860E8 count: " + w.err; return false; }
        if (cnt > 10000) { out.error = "860E8 count implausible"; return false; }
        if (!w.skip(size_t(cnt) * 18)) {
            out.error = "860E8 entries: " + w.err;
            return false;
        }
        /* Stash count so the final pass below knows how many sub-vectors. */
        out.bytes_consumed = cnt;  
    }
    const uint32_t cnt_860e8 = uint32_t(out.bytes_consumed);

    /* Step 5: jump past textures[2..3] via the ANCHOR — search forwards
       in the body for the first "art\\" and read the u32 just before.
       That bypasses the textures[2..3] section which has variable byte
       budgets we haven't fully decoded.                              */
    size_t anchor = SIZE_MAX;
    for (size_t i = w.pos; i + 4 < w.n; ++i) {
        if (w.p[i] == 'a' && w.p[i+1] == 'r' &&
            w.p[i+2] == 't' && w.p[i+3] == '\\')
        {
            anchor = i;
            break;
        }
    }
    if (anchor == SIZE_MAX || anchor < 4) {
        out.error = "no 'art\\' anchor found in body";
        return false;
    }
    uint32_t lod_count = be_u32(w.p + anchor - 4);
    if (lod_count == 0 || lod_count > 200) {
        out.error = "LOD count implausible";
        return false;
    }
    w.pos = anchor - 4;

    /* Step 6: LOD vector — count + N × (3 strings + 12B + 3 strings + 12B). */
    uint32_t lc;
    if (!w.u32(lc) || lc != lod_count) { out.error = "LOD count read"; return false; }
    out.lods.resize(lc);
    for (uint32_t k = 0; k < lc; ++k) {
        for (int s = 0; s < 3; ++s) {
            if (!w.null_str(out.lods[k].strs[s])) {
                out.error = "LOD[" + std::to_string(k) + "].str["
                          + std::to_string(s) + "]: " + w.err;
                return false;
            }
        }
        if (!w.skip(12)) { out.error = "LOD mid floats"; return false; }
        for (int s = 3; s < 6; ++s) {
            if (!w.null_str(out.lods[k].strs[s])) {
                out.error = "LOD[" + std::to_string(k) + "].str["
                          + std::to_string(s) + "]: " + w.err;
                return false;
            }
        }
        if (!w.skip(12)) { out.error = "LOD trailing floats"; return false; }
    }

    /* Step 7: sub_82A85DB0 — count + N textures. */
    uint32_t db0_cnt;
    if (!w.u32(db0_cnt)) { out.error = "85DB0 count"; return false; }
    if (db0_cnt > 32) { out.error = "85DB0 count implausible"; return false; }
    for (uint32_t k = 0; k < db0_cnt; ++k) {
        if (!skip_tex_blob(w)) {
            out.error = "85DB0 tex[" + std::to_string(k) + "]: " + w.err;
            return false;
        }
    }

    /* Step 8: u32 W, u32 H. */
    if (!w.u32(out.chunk_w) || !w.u32(out.chunk_h)) {
        out.error = "chunk grid W/H: " + w.err;
        return false;
    }
    if (out.chunk_w == 0 || out.chunk_h == 0 ||
        out.chunk_w > 256 || out.chunk_h > 256)
    {
        std::ostringstream os;
        os << "chunk grid implausible: " << out.chunk_w << "x" << out.chunk_h;
        out.error = os.str();
        return false;
    }

    /* Step 9: W × H chunks. */
    const size_t total_chunks =
        size_t(out.chunk_w) * size_t(out.chunk_h);
    out.chunks.resize(total_chunks);
    for (size_t ci = 0; ci < total_chunks; ++ci) {
        EhfChunk& c = out.chunks[ci];
        if (!w.f32(c.origin[0]) || !w.f32(c.origin[1]) || !w.f32(c.origin[2])) {
            out.error = "chunk origin: " + w.err;
            return false;
        }
        if (!w.f32(c.extent[0]) || !w.f32(c.extent[1]) || !w.f32(c.extent[2])) {
            out.error = "chunk extent: " + w.err;
            return false;
        }
        uint32_t lcount;
        if (!w.u32(lcount)) { out.error = "chunk layer count: " + w.err; return false; }
        if (lcount > 32) {
            std::ostringstream os;
            os << "chunk[" << ci << "] layer count implausible: " << lcount;
            out.error = os.str();
            return false;
        }
        c.layers.resize(lcount);
        for (uint32_t li = 0; li < lcount; ++li) {
            EhfChunkLayer& L = c.layers[li];
            uint32_t v0;
            if (!w.u32(v0))           { out.error = "layer v0"; return false; }
            if (!w.u32(L.name_idx))   { out.error = "layer name_idx"; return false; }
            if (!w.f32(L.tile_uv[0])) { out.error = "layer uv0"; return false; }
            if (!w.f32(L.tile_uv[1])) { out.error = "layer uv1"; return false; }
            /* 4 pairs of (u8 idx, u8 blend) — INTERLEAVED in stream.
               Per IDA's sub_82B25930 loop, the stream reads as:
                 idx[0], blend[0], idx[1], blend[1], ...
               Each pair represents a CORNER of the chunk (4 corners).
               Different corners can have different texture indices,
               enabling material gradients within a single chunk.    */
            for (int i = 0; i < 4; ++i) {
                if (!w.u8(L.texture_idx[i])) { out.error = "layer idx"; return false; }
                if (!w.u8(L.blend[i])) { out.error = "layer blend"; return false; }
            }
            (void)v0;  
        }
    }

    /* Step 10: final pass — u8 flag + cnt_860e8 × (u32 sub_count + 8B each). */
    uint8_t flag;
    if (!w.u8(flag)) { out.error = "final flag"; return false; }
    for (uint32_t k = 0; k < cnt_860e8; ++k) {
        uint32_t sub_cnt;
        if (!w.u32(sub_cnt)) { out.error = "final sub_count"; return false; }
        if (sub_cnt > 65535) { out.error = "final sub_count implausible"; return false; }
        if (!w.skip(size_t(sub_cnt) * 8)) {
            out.error = "final sub data";
            return false;
        }
    }

    out.bytes_consumed = w.pos;
    out.bytes_remaining = w.n - w.pos;
    out.ok = true;
    return true;
}

}  
