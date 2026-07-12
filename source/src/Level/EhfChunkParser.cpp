#include "EhfChunkParser.h"
#include "TextureAtlasDecoder.h"

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

    if (pf == 98u) {
        const uint32_t tw = be_u32(w.p + tex_start + 0x10);
        const uint32_t th = be_u32(w.p + tex_start + 0x14);
        w.pos = tex_start + mt + size_t(tw) * size_t(th) * 2;
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

    if (!skip_tex_blob(w)) { out.error = "tex[0]: " + w.err; return false; }
    if (!skip_tex_blob(w)) { out.error = "tex[1]: " + w.err; return false; }

    float dummy_f;
    if (!w.f32(dummy_f)) { out.error = "float: " + w.err; return false; }

    {
        uint32_t cnt;
        if (!w.u32(cnt)) { out.error = "850A0 count: " + w.err; return false; }
        if (cnt > 10000) { out.error = "850A0 count implausible"; return false; }
        out.bg_patches.resize(cnt);
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
            if (!w.skip(size_t(w_sub) * size_t(h_sub) * 160)) {
                out.error = "850A0 grid: " + w.err;
                return false;
            }
            EhfBgPatch& p = out.bg_patches[k];
            for (int c = 0; c < 3; ++c) {
                if (!w.f32(p.aabb_min[c])) { out.error = "850A0 aabb min"; return false; }
            }
            for (int c = 0; c < 3; ++c) {
                if (!w.f32(p.aabb_max[c])) { out.error = "850A0 aabb max"; return false; }
            }
        }
    }

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
        out.bytes_consumed = cnt;
    }
    const uint32_t cnt_860e8 = uint32_t(out.bytes_consumed);

    while (w.pos + 4 <= w.n && be_u32(w.p + w.pos) == 0xFFFFFFFEu) {
        EhfPaintResource m;
        const size_t   ts = w.pos;
        m.width        = be_u32(w.p + ts + 0x10);
        m.height       = be_u32(w.p + ts + 0x14);
        m.pixel_format = be_u32(w.p + ts + 0x18);
        const uint32_t mt = be_u32(w.p + ts + 0x20);
        if (m.pixel_format == 98u) {
            const size_t data_off = ts + mt;
            const size_t data_len = size_t(m.width) * size_t(m.height) * 2u;
            if (data_off + data_len <= w.n)
                m.data.assign(w.p + data_off, w.p + data_off + data_len);
        }
        out.weight_masks.push_back(std::move(m));
        if (!skip_tex_blob(w)) { out.error = "weight mask: " + w.err; return false; }
    }

    uint32_t lc;
    if (!w.u32(lc)) { out.error = "LOD count read: " + w.err; return false; }
    if (lc > 200) { out.error = "LOD count implausible"; return false; }
    out.lods.resize(lc);
    for (uint32_t k = 0; k < lc; ++k) {
        for (int s = 0; s < 3; ++s) {
            if (!w.null_str(out.lods[k].strs[s])) {
                out.error = "LOD[" + std::to_string(k) + "].str["
                          + std::to_string(s) + "]: " + w.err;
                return false;
            }
        }
        if (!w.f32(out.lods[k].params[0][0])) {
            out.error = "LOD base scale";
            return false;
        }
        uint32_t mid_raw = 0;
        if (!w.u32(mid_raw)) {
            out.error = "LOD material flags";
            return false;
        }
        out.lods[k].material_flags = mid_raw;
        if (!w.f32(out.lods[k].params[0][1])) {
            out.error = "LOD base weight";
            return false;
        }
        out.lods[k].params[0][2] = 0.0f;
        for (int s = 3; s < 6; ++s) {
            if (!w.null_str(out.lods[k].strs[s])) {
                out.error = "LOD[" + std::to_string(k) + "].str["
                          + std::to_string(s) + "]: " + w.err;
                return false;
            }
        }
        for (int p = 0; p < 3; ++p) {
            if (!w.f32(out.lods[k].params[1][p])) {
                out.error = "LOD trailing floats";
                return false;
            }
        }
    }

    uint32_t db0_cnt;
    if (!w.u32(db0_cnt)) { out.error = "85DB0 count"; return false; }
    if (db0_cnt > 32) { out.error = "85DB0 count implausible"; return false; }
    out.paint_resources.reserve(db0_cnt);
    for (uint32_t k = 0; k < db0_cnt; ++k) {
        if (!w.need(0x60)) {
            out.error = "85DB0 tex[" + std::to_string(k) + "]: " + w.err;
            return false;
        }
        const size_t tex_start = w.pos;
        EhfPaintResource res;
        res.width = be_u32(w.p + tex_start + 0x10);
        res.height = be_u32(w.p + tex_start + 0x14);
        const uint32_t pf = be_u32(w.p + tex_start + 0x18);
        res.pixel_format = pf;
        out.paint_resources.push_back(res);
        if (pf == 99u) {
            int sw = 0, sh = 0;
            std::string err;
            std::vector<uint8_t> decoded;
            if (TextureAtlas::DecodePF99SplatMap(
                    w.p + tex_start, w.n - tex_start,
                    decoded, sw, sh, err))
            {
                out.paint_resources.back().data   = decoded;
                out.paint_resources.back().width  = (uint32_t)sw;
                out.paint_resources.back().height = (uint32_t)sh;
                out.splat_indices = std::move(decoded);
                out.splat_w = (uint32_t)sw;
                out.splat_h = (uint32_t)sh;
            }
        }
        if (!skip_tex_blob(w)) {
            out.error = "85DB0 tex[" + std::to_string(k) + "]: " + w.err;
            return false;
        }
    }

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
            if (!w.u32(L.material_idx)) { out.error = "layer material_idx"; return false; }
            if (!w.u32(L.name_idx))   { out.error = "layer name_idx"; return false; }
            if (!w.f32(L.tile_uv[0])) { out.error = "layer uv0"; return false; }
            if (!w.f32(L.tile_uv[1])) { out.error = "layer uv1"; return false; }
            if (L.name_idx < out.paint_resources.size()) {
                const EhfPaintResource& res =
                    out.paint_resources[size_t(L.name_idx)];
                if (res.width > 0) {
                    L.mask_scale[0] = 16.0f / float(res.width);
                }
                if (res.height > 0) {
                    L.mask_scale[1] = 16.0f / float(res.height);
                }
            }
            for (int i = 0; i < 4; ++i) {
                if (!w.u8(L.texture_idx[i])) { out.error = "layer idx"; return false; }
                if (!w.u8(L.blend[i])) { out.error = "layer blend"; return false; }
            }
        }
    }

    uint8_t flag;
    if (!w.u8(flag)) { out.error = "final flag"; return false; }
    for (uint32_t k = 0; k < cnt_860e8; ++k) {
        uint32_t sub_cnt;
        if (!w.u32(sub_cnt)) { out.error = "final sub_count"; return false; }
        if (sub_cnt > 65535) { out.error = "final sub_count implausible"; return false; }
        if (!w.need(size_t(sub_cnt) * 8)) {
            out.error = "final sub data";
            return false;
        }
        if (k < out.bg_patches.size()) {
            auto& pages = out.bg_patches[k].pages;
            pages.reserve(sub_cnt);
            for (uint32_t s = 0; s < sub_cnt; ++s) {
                const uint32_t off = be_u32(w.p + w.pos + size_t(s) * 8);
                const uint32_t len = be_u32(w.p + w.pos + size_t(s) * 8 + 4);
                pages.emplace_back(off, len);
            }
        }
        w.pos += size_t(sub_cnt) * 8;
    }

    out.bytes_consumed = w.pos;
    out.bytes_remaining = w.n - w.pos;
    out.ok = true;
    return true;
}

}
