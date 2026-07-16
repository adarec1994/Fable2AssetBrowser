#include "Level/Core/LevelLoader.h"
#include "Level/Terrain/HeightfieldLoader.h"
#include "Level/Terrain/TextureAtlasDecoder.h"
#include "Level/Terrain/EhfPalette.h"
#include "Level/Terrain/EhfChunkParser.h"
#include "Level/Terrain/TerrainTextureRegistry.h"
#include "Level/Loading/LevelBinaryReader.h"
#include "Level/Loading/LevelTerrainLoaderInternal.h"
#include "BNKCore.cpp"
#include "UI/OutputLog.h"
#include "Utilities/State.h"
#include "textures/TexParser.h"
#include "textures/LhTexCodec.h"
#include "textures/export/TextureExport.h"
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern bool decode_tex_to_rgba(const std::vector<unsigned char>& blob,
                               std::vector<uint8_t>& rgba,
                               int& out_w, int& out_h,
                               bool* out_has_alpha,
                               int mip_index);
extern const std::string& mp_last_decode_fail_reason();
extern const std::string& mp_last_decode_info();

namespace Level {
bool RenderHeightmapToRGBA(const FlatAssetEntry& entry,
                           std::vector<uint8_t>& out_rgba,
                           int&                  out_w,
                           int&                  out_h)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;

    std::filesystem::path lp = entry.full_path;
    lp.replace_extension(".list");
    std::string list_full = lp.string();
    std::string list_key  = list_full;
    std::transform(list_key.begin(), list_key.end(), list_key.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    std::replace(list_key.begin(), list_key.end(), '\\', '/');

    int list_idx = BnkCache::find_index(entry.bnk_path, list_key);
    if (list_idx < 0) {
        OutputLog::error("View Heightmap: no companion .list ("
                         + list_full + ") in BNK");
        return false;
    }

    std::vector<uint8_t> list_bytes;
    try {
        list_bytes = BnkCache::extract_bytes(entry.bnk_path, list_idx);
    } catch (...) {
        OutputLog::error("View Heightmap: failed to extract .list");
        return false;
    }
    std::string list_str(reinterpret_cast<const char*>(list_bytes.data()),
                         list_bytes.size());

    std::string ghf_path;
    size_t pos = 0;
    while (pos < list_str.size()) {
        size_t eol = list_str.find_first_of("\r\n", pos);
        std::string line = (eol == std::string::npos)
                               ? list_str.substr(pos)
                               : list_str.substr(pos, eol - pos);
        pos = (eol == std::string::npos)
                  ? list_str.size()
                  : list_str.find_first_not_of("\r\n", eol);
        if (pos == std::string::npos) pos = list_str.size();
        if (line.empty()) continue;

        std::string low = line;
        std::transform(low.begin(), low.end(), low.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        if (low.size() >= 4 && low.compare(low.size()-4, 4, ".ghf") == 0) {
            ghf_path = line;
            break;
        }
    }
    if (ghf_path.empty()) {
        OutputLog::error("View Heightmap: no .ghf entry in .list");
        return false;
    }

    HeightfieldFiles hf;
    if (!LoadHeightfieldFiles({}, ghf_path, {}, {}, hf)) {
        OutputLog::error("View Heightmap: .ghf load failed: " + hf.error);
        return false;
    }

    GhfHeights hg;
    if (!DecodeGhfHeights(hf.ghf_bytes_raw, hg)) {
        OutputLog::error("View Heightmap: .ghf decode failed: " + hg.error);
        return false;
    }

    const float lo   = hg.min_height;
    const float hi   = hg.max_height;
    const float span = (hi > lo) ? (hi - lo) : 1.f;

    out_w = static_cast<int>(hg.width);
    out_h = static_cast<int>(hg.height);
    out_rgba.resize(static_cast<size_t>(out_w) * static_cast<size_t>(out_h) * 4);

    for (size_t i = 0; i < hg.heights.size(); ++i) {
        float t = (hg.heights[i] - lo) / span;
        if (t < 0.f) t = 0.f;
        if (t > 1.f) t = 1.f;
        const uint8_t v = static_cast<uint8_t>(t * 255.0f + 0.5f);
        out_rgba[i * 4 + 0] = v;
        out_rgba[i * 4 + 1] = v;
        out_rgba[i * 4 + 2] = v;
        out_rgba[i * 4 + 3] = 0xFF;
    }

    return true;
}

bool RenderPf99ToRGBA(const FlatAssetEntry& entry,
                      std::vector<uint8_t>& out_rgba,
                      int&                  out_w,
                      int&                  out_h)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;

    std::vector<uint8_t> level_bytes;
    try {
        auto v = BnkCache::extract_bytes(entry.bnk_path, entry.file_index);
        level_bytes.assign(v.begin(), v.end());
    } catch (...) {
        OutputLog::error("Open PF99: failed to extract level");
        return false;
    }

    EngineLevelInfo info;
    if (!ParseEngineLevel(level_bytes, info)) {
        OutputLog::error("Open PF99: level parse failed: " + info.error);
        return false;
    }

    auto ends_with_ci = [](const std::string& s, const char* suffix) {
        size_t n = std::strlen(suffix);
        if (s.size() < n) return false;
        for (size_t i = 0; i < n; ++i) {
            char a = s[s.size() - n + i];
            char b = suffix[i];
            if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = char(b - 'A' + 'a');
            if (a != b) return false;
        }
        return true;
    };
    auto basename_no_ext = [](const std::string& p) {
        size_t slash = p.find_last_of("/\\");
        std::string s = slash == std::string::npos ? p : p.substr(slash + 1);
        size_t dot = s.find_last_of('.');
        if (dot != std::string::npos) s.resize(dot);
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        return s;
    };

    std::vector<std::string> ehf_refs;
    for (const auto& e : info.entries) {
        if (!e.str_a.empty() && ends_with_ci(e.str_a, ".ehf")) {
            ehf_refs.push_back(e.str_a);
        }
    }

    std::string list_ehf;
    std::string list_ghf;
    {
        std::filesystem::path lp = entry.full_path;
        lp.replace_extension(".list");
        std::string list_key = lp.string();
        std::transform(list_key.begin(), list_key.end(), list_key.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        std::replace(list_key.begin(), list_key.end(), '\\', '/');
        const int list_idx = BnkCache::find_index(entry.bnk_path, list_key);
        if (list_idx >= 0) {
            try {
                auto bytes = BnkCache::extract_bytes(entry.bnk_path, list_idx);
                std::string list_str(
                    reinterpret_cast<const char*>(bytes.data()),
                    bytes.size());
                size_t pos = 0;
                while (pos < list_str.size()) {
                    size_t eol = list_str.find_first_of("\r\n", pos);
                    std::string line = (eol == std::string::npos)
                        ? list_str.substr(pos)
                        : list_str.substr(pos, eol - pos);
                    pos = (eol == std::string::npos)
                        ? list_str.size()
                        : list_str.find_first_not_of("\r\n", eol);
                    if (pos == std::string::npos) pos = list_str.size();
                    if (ends_with_ci(line, ".ehf")) list_ehf = line;
                    else if (ends_with_ci(line, ".ghf")) list_ghf = line;
                }
            } catch (...) {}
        }
    }

    std::string ehf_path = list_ehf;
    if (ehf_path.empty() && !ehf_refs.empty()) {
        const std::string ghf_base = basename_no_ext(list_ghf);
        for (const auto& candidate : ehf_refs) {
            if (!ghf_base.empty() &&
                basename_no_ext(candidate) == ghf_base) {
                ehf_path = candidate;
                break;
            }
        }
        if (ehf_path.empty()) ehf_path = ehf_refs.front();
    }
    if (ehf_path.empty()) {
        OutputLog::error("Open PF99: no .ehf reference found");
        return false;
    }

    HeightfieldFiles hf;
    if (!LoadHeightfieldFiles(ehf_path, {}, {}, {}, hf)) {
        OutputLog::error("Open PF99: .ehf load failed: " + hf.error);
        return false;
    }

    EhfParsedBody parsed;
    if (!ParseEhfBody(hf.ehf_bytes, parsed)) {
        OutputLog::error("Open PF99: EHF parse failed: " + parsed.error);
        return false;
    }
    if (parsed.splat_indices.empty() ||
        parsed.splat_w == 0 || parsed.splat_h == 0 ||
        parsed.splat_indices.size() !=
            size_t(parsed.splat_w) * size_t(parsed.splat_h)) {
        OutputLog::error("Open PF99: no PF99 layer mask atlas");
        return false;
    }

    out_w = int(parsed.splat_w);
    out_h = int(parsed.splat_h);
    out_rgba.resize(size_t(out_w) * size_t(out_h) * 4);
    for (size_t i = 0; i < parsed.splat_indices.size(); ++i) {
        const uint8_t v = parsed.splat_indices[i];
        out_rgba[i * 4 + 0] = v;
        out_rgba[i * 4 + 1] = v;
        out_rgba[i * 4 + 2] = v;
        out_rgba[i * 4 + 3] = 255;
    }
    return true;
}

bool DecodeLevelTextureAtlas(const FlatAssetEntry& level_entry,
                             std::vector<uint8_t>& out_rgba,
                             int&                  out_w,
                             int&                  out_h)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;

    std::filesystem::path atlas_path = level_entry.full_path;
    atlas_path.replace_extension(".texture_atlas");
    const std::string atlas_full = atlas_path.string();

    std::string atlas_key = atlas_full;
    std::transform(atlas_key.begin(), atlas_key.end(), atlas_key.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    std::replace(atlas_key.begin(), atlas_key.end(), '\\', '/');

    auto try_bnk = [&](const std::string& bnk_path,
                       std::vector<uint8_t>& out_blob) -> bool {
        int idx = BnkCache::find_index(bnk_path, atlas_key);
        if (idx < 0) return false;
        try {
            auto v = BnkCache::extract_bytes(bnk_path, idx);
            if (v.empty()) return false;
            out_blob.assign(v.begin(), v.end());
            return true;
        } catch (...) {
            return false;
        }
    };

    std::vector<uint8_t> blob;
    bool found = try_bnk(level_entry.bnk_path, blob);

    if (!found) {
        const std::string base_lower = std::filesystem::path(atlas_full)
                                           .filename().string();
        std::string base_low = base_lower;
        std::transform(base_low.begin(), base_low.end(), base_low.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        for (const auto& fe : S.all_heightfield_files) {
            std::string nlow = fe.name;
            std::transform(nlow.begin(), nlow.end(), nlow.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            if (nlow != base_low) continue;
            try {
                auto v = BnkCache::extract_bytes(fe.bnk_path, fe.file_index);
                if (!v.empty()) {
                    blob.assign(v.begin(), v.end());
                    found = true;
                    break;
                }
            } catch (...) {}
        }
    }

    if (!found) {
        for (const auto& bnk_path : S.bnk_paths) {
            if (bnk_path == level_entry.bnk_path) continue;
            if (try_bnk(bnk_path, blob)) { found = true; break; }
        }
    }
    if (!found) {
        OutputLog::warn("texture_atlas: no '" + atlas_full +
                        "' found in any loaded BNK");
        return false;
    }

    TextureAtlas::DecodedAtlas dec = TextureAtlas::DecodeAtlas(blob);
    if (!dec.ok) {
        OutputLog::error("texture_atlas: " + dec.error +
                         "  (file=" + atlas_full + ")");
        return false;
    }
    out_rgba = std::move(dec.rgba);
    out_w    = dec.width;
    out_h    = dec.height;
    return true;
}

namespace {

struct EhfRenderTileDesc {
    uint32_t cell_x = 0;
    uint32_t cell_y = 0;
    uint32_t cell_w = 0;
    uint32_t cell_h = 0;
    uint32_t sub_w  = 0;
    uint32_t sub_h  = 0;
    float    min_x  = 0.0f;
    float    min_y  = 0.0f;
    float    max_x  = 0.0f;
    float    max_y  = 0.0f;
    bool     bbox_ok = false;
};

struct EhfEmbeddedBc1Mip {
    size_t   offset = 0;
    uint32_t header_w = 0;
    uint32_t header_h = 0;
    uint32_t raw_size = 0;
    uint32_t comp_size = 0;
};

static uint32_t ehf_be32(const std::vector<uint8_t>& d, size_t off)
{
    return (uint32_t(d[off + 0]) << 24) |
           (uint32_t(d[off + 1]) << 16) |
           (uint32_t(d[off + 2]) <<  8) |
            uint32_t(d[off + 3]);
}

static bool ehf_skip_tex_blob(const std::vector<uint8_t>& ehf,
                              size_t limit,
                              size_t& pos)
{
    if (pos + 0x60 > limit) return false;
    if (ehf_be32(ehf, pos) != 0xFFFFFFFEu) return false;

    const uint32_t pf = ehf_be32(ehf, pos + 0x18);
    const uint32_t mt = ehf_be32(ehf, pos + 0x20);
    if (mt < 0x54 || mt > 0x200) return false;

    const size_t table = pos + mt;
    if (table + 8 > limit) return false;
    size_t next;
    if (pf == 98u) {
        const uint32_t tw = ehf_be32(ehf, pos + 0x10);
        const uint32_t th = ehf_be32(ehf, pos + 0x14);
        next = pos + mt + size_t(tw) * size_t(th) * 2u;
    } else {
        const uint32_t comp_size = ehf_be32(ehf, table + 4);
        next = table + 8 + size_t(comp_size);
    }
    if (next > limit) return false;
    pos = next;
    return true;
}

static bool parse_ehf_render_tiles(const std::vector<uint8_t>& ehf,
                                   uint32_t terrain_cells_w,
                                   std::vector<EhfRenderTileDesc>& out)
{
    out.clear();
    if (ehf.size() < 63) return false;
    const uint32_t body_off  = ehf_be32(ehf, 55);
    const uint32_t body_size = ehf_be32(ehf, 59);
    const size_t body_end = size_t(body_off) + size_t(body_size);
    if (body_end > ehf.size()) return false;

    size_t pos = body_off;
    if (!ehf_skip_tex_blob(ehf, body_end, pos)) return false;
    if (!ehf_skip_tex_blob(ehf, body_end, pos)) return false;
    if (pos + 8 > body_end) return false;

    pos += 4;
    const uint32_t count = ehf_be32(ehf, pos);
    pos += 4;
    if (count == 0 || count > 4096) return false;

    auto ehf_bef32 = [&](size_t off) -> float {
        const uint32_t u = ehf_be32(ehf, off);
        float f = 0.0f;
        std::memcpy(&f, &u, sizeof(f));
        return f;
    };

    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (pos + 16 > body_end) return false;
        EhfRenderTileDesc t;
        t.cell_w = ehf_be32(ehf, pos + 0);
        t.cell_h = ehf_be32(ehf, pos + 4);
        t.sub_w  = ehf_be32(ehf, pos + 8);
        t.sub_h  = ehf_be32(ehf, pos + 12);
        pos += 16;
        if (t.cell_w == 0 || t.cell_h == 0 ||
            t.sub_w == 0 || t.sub_h == 0 ||
            t.sub_w > 1024 || t.sub_h > 1024)
        {
            return false;
        }
        const size_t grid_bytes =
            size_t(t.sub_w) * size_t(t.sub_h) * 160u + 24u;
        if (pos + grid_bytes > body_end) return false;
        {
            const size_t bb = pos + grid_bytes - 24u;
            t.min_x = ehf_bef32(bb + 0);
            t.min_y = ehf_bef32(bb + 4);
            t.max_x = ehf_bef32(bb + 12);
            t.max_y = ehf_bef32(bb + 16);
            t.bbox_ok = std::isfinite(t.min_x) && std::isfinite(t.min_y) &&
                        std::isfinite(t.max_x) && std::isfinite(t.max_y) &&
                        t.max_x > t.min_x && t.max_y > t.min_y &&
                        std::fabs(t.min_x) < 100000.0f &&
                        std::fabs(t.min_y) < 100000.0f;
        }
        pos += grid_bytes;
        out.push_back(t);
    }

    bool placed_exact = false;
    {
        size_t bbox_count = 0;
        std::vector<float> sx, sy;
        float gmin_x = std::numeric_limits<float>::infinity();
        float gmin_y = std::numeric_limits<float>::infinity();
        for (const EhfRenderTileDesc& t : out) {
            if (!t.bbox_ok) continue;
            ++bbox_count;
            sx.push_back(float(t.cell_w) / (t.max_x - t.min_x));
            sy.push_back(float(t.cell_h) / (t.max_y - t.min_y));
            gmin_x = std::min(gmin_x, t.min_x);
            gmin_y = std::min(gmin_y, t.min_y);
        }
        if (bbox_count == out.size() && !sx.empty()) {
            auto median = [](std::vector<float>& v) {
                std::sort(v.begin(), v.end());
                return v[v.size() / 2];
            };
            const float scale_x = median(sx);
            const float scale_y = median(sy);
            if (std::isfinite(scale_x) && std::isfinite(scale_y) &&
                scale_x > 0.01f && scale_y > 0.01f)
            {
                placed_exact = true;
                for (EhfRenderTileDesc& t : out) {
                    const float fx = (t.min_x - gmin_x) * scale_x;
                    const float fy = (t.min_y - gmin_y) * scale_y;
                    const long cx = std::lround(fx);
                    const long cy = std::lround(fy);
                    if (cx < 0 || cy < 0 || cx > 65535 || cy > 65535) {
                        placed_exact = false;
                        break;
                    }
                    t.cell_x = uint32_t(cx);
                    t.cell_y = uint32_t(cy);
                }
            }
        }
    }

    if (!placed_exact) {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t row_h = 0;
        for (EhfRenderTileDesc& t : out) {
            if (terrain_cells_w > 0 &&
                x > 0 &&
                x + t.cell_w > terrain_cells_w)
            {
                y += row_h;
                x = 0;
                row_h = 0;
            }
            t.cell_x = x;
            t.cell_y = y;
            x += t.cell_w;
            row_h = std::max(row_h, t.cell_h);
            if (terrain_cells_w > 0 && x >= terrain_cells_w) {
                y += row_h;
                x = 0;
                row_h = 0;
            }
        }
    }
    return true;
}

static std::vector<EhfEmbeddedBc1Mip>
collect_ehf_embedded_bc1_primaries(const std::vector<uint8_t>& ehf)
{
    std::vector<EhfEmbeddedBc1Mip> all;
    if (ehf.size() < 63) return all;
    const uint32_t body_off  = ehf_be32(ehf, 55);
    const uint32_t body_size = ehf_be32(ehf, 59);
    const size_t body_end = size_t(body_off) + size_t(body_size);
    if (body_end > ehf.size()) return all;

    for (size_t i = body_end; i + 0x60 < ehf.size(); ++i) {
        if (ehf[i] != 0xFF || ehf[i + 1] != 0xFF ||
            ehf[i + 2] != 0xFF || ehf[i + 3] != 0xFE) continue;
        const uint32_t w  = ehf_be32(ehf, i + 0x10);
        const uint32_t h  = ehf_be32(ehf, i + 0x14);
        const uint32_t pf = ehf_be32(ehf, i + 0x18);
        const uint32_t mt = ehf_be32(ehf, i + 0x20);
        if (pf != 35u || w == 0 || h == 0 ||
            w > 8192 || h > 8192 ||
            mt < 0x54 || mt > 0x200) {
            continue;
        }
        const size_t table = i + mt;
        if (table + 8 > ehf.size()) continue;
        const uint32_t raw_size  = ehf_be32(ehf, table);
        const uint32_t comp_size = ehf_be32(ehf, table + 4);
        const size_t zlib_at = table + 8;
        if (comp_size < 2 || zlib_at + size_t(comp_size) > ehf.size()) continue;
        if (ehf[zlib_at] != 0x78) continue;
        all.push_back({i, w, h, raw_size, comp_size});
    }

    std::vector<EhfEmbeddedBc1Mip> primaries;
    for (size_t i = 0; i < all.size();) {
        if (i + 2 < all.size() &&
            all[i + 1].header_w * 2u == all[i].header_w &&
            all[i + 2].header_w * 4u == all[i].header_w)
        {
            primaries.push_back(all[i]);
            i += 3;
        } else {
            ++i;
        }
    }
    return primaries;
}

static bool decode_ehf_embedded_bc1(const std::vector<uint8_t>& ehf,
                                    const EhfEmbeddedBc1Mip& mip,
                                    std::vector<uint8_t>& rgba,
                                    int& w,
                                    int& h)
{
    rgba.clear();
    w = 0;
    h = 0;
    const uint32_t mt = ehf_be32(ehf, mip.offset + 0x20);
    const size_t table = mip.offset + mt;
    const size_t zlib_at = table + 8;
    if (zlib_at + size_t(mip.comp_size) > ehf.size()) return false;

    std::vector<uint8_t> body(mip.raw_size);
    z_stream zs{};
    zs.next_in   = const_cast<Bytef*>(ehf.data() + zlib_at);
    zs.avail_in  = (uInt)mip.comp_size;
    zs.next_out  = body.data();
    zs.avail_out = (uInt)mip.raw_size;
    const int rc_init = inflateInit2(&zs, 15);
    const int rc = (rc_init == Z_OK) ? inflate(&zs, Z_FINISH) : Z_ERRNO;
    const size_t produced = size_t(mip.raw_size) - size_t(zs.avail_out);
    inflateEnd(&zs);
    if (rc_init != Z_OK || produced != mip.raw_size ||
        !(rc == Z_STREAM_END || rc == Z_OK || rc == Z_BUF_ERROR)) {
        return false;
    }

    std::vector<uint8_t> bc1;
    std::string err;
    if (!lh_decode_compressed_mip(body.data(), body.size(),
                                  w, h, bc1, &err,
                                  false)) {
        return false;
    }
    return TextureAtlas::DecodeRawBc1ToRgba(bc1.data(), bc1.size(),
                                            w, h, rgba);
}

static void blit_resampled_rgba(const std::vector<uint8_t>& src,
                                int src_w,
                                int src_h,
                                std::vector<uint8_t>& dst,
                                int dst_w,
                                int dst_h,
                                int dst_x,
                                int dst_y,
                                int copy_w,
                                int copy_h)
{
    if (src.empty() || src_w <= 0 || src_h <= 0 ||
        dst.empty() || dst_w <= 0 || dst_h <= 0 ||
        copy_w <= 0 || copy_h <= 0) return;

    const int clipped_w = std::min(copy_w, dst_w - dst_x);
    const int clipped_h = std::min(copy_h, dst_h - dst_y);
    if (dst_x < 0 || dst_y < 0 || clipped_w <= 0 || clipped_h <= 0) return;

    for (int y = 0; y < clipped_h; ++y) {
        const float sy = (float(y) + 0.5f) * float(src_h) / float(copy_h) - 0.5f;
        const int y0 = std::clamp(int(std::floor(sy)), 0, src_h - 1);
        const int y1 = std::min(y0 + 1, src_h - 1);
        const float fy = std::clamp(sy - float(y0), 0.0f, 1.0f);
        for (int x = 0; x < clipped_w; ++x) {
            const float sx = (float(x) + 0.5f) * float(src_w) / float(copy_w) - 0.5f;
            const int x0 = std::clamp(int(std::floor(sx)), 0, src_w - 1);
            const int x1 = std::min(x0 + 1, src_w - 1);
            const float fx = std::clamp(sx - float(x0), 0.0f, 1.0f);

            const uint8_t* p00 = src.data() + (size_t(y0) * src_w + x0) * 4;
            const uint8_t* p10 = src.data() + (size_t(y0) * src_w + x1) * 4;
            const uint8_t* p01 = src.data() + (size_t(y1) * src_w + x0) * 4;
            const uint8_t* p11 = src.data() + (size_t(y1) * src_w + x1) * 4;
            uint8_t* out = dst.data() + (size_t(dst_y + y) * dst_w + (dst_x + x)) * 4;
            const float w00 = (1.0f - fx) * (1.0f - fy);
            const float w10 = fx * (1.0f - fy);
            const float w01 = (1.0f - fx) * fy;
            const float w11 = fx * fy;
            for (int c = 0; c < 4; ++c) {
                out[c] = uint8_t(std::clamp(
                    w00 * p00[c] + w10 * p10[c] +
                    w01 * p01[c] + w11 * p11[c],
                    0.0f, 255.0f));
            }
        }
    }
}

static bool DecodeEhfEmbeddedTileComposite(const std::vector<uint8_t>& ehf,
                                           uint32_t terrain_vertices_w,
                                           uint32_t terrain_vertices_h,
                                           std::vector<uint8_t>& out_rgba,
                                           int& out_w,
                                           int& out_h)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;
    const uint32_t cells_w =
        terrain_vertices_w > 1 ? terrain_vertices_w - 1 : terrain_vertices_w;
    const uint32_t cells_h =
        terrain_vertices_h > 1 ? terrain_vertices_h - 1 : terrain_vertices_h;
    if (cells_w == 0 || cells_h == 0) return false;

    std::vector<EhfRenderTileDesc> tiles;
    if (!parse_ehf_render_tiles(ehf, cells_w, tiles) || tiles.empty()) {
        return false;
    }
    std::vector<EhfEmbeddedBc1Mip> primaries =
        collect_ehf_embedded_bc1_primaries(ehf);
    if (primaries.size() < tiles.size()) return false;

    struct DecodedTile {
        bool ok = false;
        std::vector<uint8_t> rgba;
        int w = 0;
        int h = 0;
    };
    std::vector<DecodedTile> decoded(tiles.size());
    std::vector<float> ratios_x;
    std::vector<float> ratios_y;
    size_t ok_count = 0;
    for (size_t i = 0; i < tiles.size(); ++i) {
        DecodedTile dt;
        if (!decode_ehf_embedded_bc1(ehf, primaries[i], dt.rgba, dt.w, dt.h)) {
            decoded[i] = std::move(dt);
            continue;
        }
        dt.ok = true;
        if (tiles[i].cell_w > 0 && tiles[i].cell_h > 0) {
            ratios_x.push_back(float(dt.w) / float(tiles[i].cell_w));
            ratios_y.push_back(float(dt.h) / float(tiles[i].cell_h));
        }
        decoded[i] = std::move(dt);
        ++ok_count;
    }
    if (ok_count < std::max<size_t>(4, tiles.size() / 4)) return false;

    auto median_ratio = [](std::vector<float>& v) -> float {
        if (v.empty()) return 1.0f;
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    const float median_x = median_ratio(ratios_x);
    const float median_y = median_ratio(ratios_y);
    if (median_x < 0.5f || median_y < 0.5f ||
        median_x > 8.0f || median_y > 8.0f)
    {
        std::ostringstream os;
        os << "ehf: embedded BC1 tile pages look strip-like "
           << "(median scale=" << median_x << "x" << median_y
           << "), skipping as terrain albedo";
        OutputLog::info(os.str());
        return false;
    }

    const int scale_x = std::clamp(int(std::lround(median_x)), 1, 8);
    const int scale_y = std::clamp(int(std::lround(median_y)), 1, 8);

    out_w = int(cells_w) * scale_x;
    out_h = int(cells_h) * scale_y;
    if (out_w <= 0 || out_h <= 0 || out_w > 8192 || out_h > 8192) {
        return false;
    }
    out_rgba.assign(size_t(out_w) * size_t(out_h) * 4, 0);
    for (size_t i = 3; i < out_rgba.size(); i += 4) {
        out_rgba[i] = 0xFF;
    }

    for (size_t i = 0; i < tiles.size(); ++i) {
        const DecodedTile& dt = decoded[i];
        if (!dt.ok) continue;
        const EhfRenderTileDesc& t = tiles[i];
        const int dx = int(t.cell_x) * scale_x;
        const int dy = int(t.cell_y) * scale_y;
        const int dw = int(t.cell_w) * scale_x;
        const int dh = int(t.cell_h) * scale_y;
        blit_resampled_rgba(dt.rgba, dt.w, dt.h,
                            out_rgba, out_w, out_h,
                            dx, dy, dw, dh);
    }

    std::ostringstream os;
    os << "ehf: embedded tile composite " << out_w << "x" << out_h
       << " from " << ok_count << "/" << tiles.size()
       << " tile texture pages (scale=" << scale_x << "x" << scale_y << ")";
    OutputLog::success(os.str());
    return true;
}

static bool decode_bg_page(const std::vector<uint8_t>& ehf, uint32_t off,
                           std::vector<uint8_t>& rgba, int& w, int& h)
{
    if (uint64_t(off) + 0x60 > ehf.size()) return false;
    if (ehf_be32(ehf, off) != 0xFFFFFFFEu) return false;
    if (ehf_be32(ehf, off + 0x18) != 35u) return false;
    const uint32_t mt = ehf_be32(ehf, off + 0x20);
    if (mt < 0x54 || mt > 0x200 || size_t(off) + mt + 8 > ehf.size()) {
        return false;
    }
    const uint32_t comp = ehf_be32(ehf, off + mt + 4);
    size_t blob_end = size_t(off) + mt + 8 + comp;
    if (blob_end > ehf.size()) blob_end = ehf.size();
    std::vector<uint8_t> blob(ehf.begin() + off, ehf.begin() + blob_end);
    TextureAtlas::DecodedAtlas dec = TextureAtlas::DecodeAtlas(blob);
    if (!dec.ok || dec.rgba.empty()) return false;
    rgba = std::move(dec.rgba);
    w = dec.width;
    h = dec.height;
    return true;
}

bool build_ehf_vista_patch_geoms(const std::vector<uint8_t>& ehf,
                                 std::vector<Level::VistaPatchGeom>& out_geoms,
                                 std::string* out_stats)
{
    out_geoms.clear();
    if (out_stats) out_stats->clear();

    static constexpr char kMagic[] = "HeightFieldGraphicsFile";
    static constexpr size_t kMagicLen = sizeof(kMagic) - 1;
    static constexpr size_t kHeaderLen = 63;
    if (ehf.size() < kHeaderLen ||
        std::memcmp(ehf.data(), kMagic, kMagicLen) != 0) {
        return false;
    }
    const uint32_t body_off  = read_be_u32_raw(ehf.data() + 55);
    const uint32_t body_size = read_be_u32_raw(ehf.data() + 59);
    if (uint64_t(body_off) + uint64_t(body_size) > ehf.size()) return false;

    float f2 = read_be_f32_raw(ehf.data() + 43);
    const float M = (std::isfinite(f2) && f2 > 0.0f) ? f2 * 16.0f : 8.0f;

    BeReader r;
    r.p = ehf.data() + body_off;
    r.n = body_size;
    r.i = 0;
    if (!skip_ehf_tex_blob(r) || !skip_ehf_tex_blob(r)) return false;

    float max_height_hint = 0.0f;
    uint32_t patch_count = 0;
    if (!r.f32(max_height_hint) || !r.u32(patch_count)) return false;
    if (patch_count == 0 || patch_count > 4096) return false;

    auto sane = [](float v) {
        return std::isfinite(v) && std::fabs(v) < 1000000.0f;
    };
    auto qkey = [](float x, float y) -> uint64_t {
        const int32_t qx = int32_t(std::lround(double(x) * 4.0));
        const int32_t qy = int32_t(std::lround(double(y) * 4.0));
        return (uint64_t(uint32_t(qx)) << 32) | uint32_t(qy);
    };

    struct VP { float amin[3]; float amax[3]; uint32_t W, H; };
    std::vector<VP> patches;
    patches.reserve(patch_count);
    std::unordered_map<uint64_t, float> lattice, lattice_diag;

    for (uint32_t pi = 0; pi < patch_count; ++pi) {
        float a = 0.0f, b = 0.0f;
        uint32_t W = 0, H = 0;
        if (!r.f32(a) || !r.f32(b) || !r.u32(W) || !r.u32(H)) return false;
        if (W == 0 || H == 0 || W > 1024 || H > 1024) return false;
        const uint64_t cells = uint64_t(W) * uint64_t(H);
        if (cells > 65536 ||
            uint64_t(r.i) + cells * 160ull + 24ull > uint64_t(r.n)) {
            return false;
        }
        VP p;
        p.W = W;
        p.H = H;
        const size_t cell_base = r.i;
        const uint8_t* aabb = r.p + cell_base + size_t(cells) * 160u;
        for (int k = 0; k < 3; ++k) {
            p.amin[k] = read_be_f32_raw(aabb + size_t(k) * 4u);
            p.amax[k] = read_be_f32_raw(aabb + 12u + size_t(k) * 4u);
        }
        for (uint64_t ci = 0; ci < cells; ++ci) {
            const uint8_t* bv = r.p + cell_base + size_t(ci) * 160u + 64u + 12u;
            const float bx = read_be_f32_raw(bv + 0);
            const float by = read_be_f32_raw(bv + 4);
            const float bh = read_be_f32_raw(bv + 8);
            if (sane(bx) && sane(by) && sane(bh)) lattice[qkey(bx, by)] = bh;
            const uint8_t* dv = r.p + cell_base + size_t(ci) * 160u + 64u + 48u;
            const float dx = read_be_f32_raw(dv + 0);
            const float dy = read_be_f32_raw(dv + 4);
            const float dh = read_be_f32_raw(dv + 8);
            if (sane(dx) && sane(dy) && sane(dh)) {
                lattice_diag.emplace(qkey(dx, dy), dh);
            }
        }
        patches.push_back(p);
        r.i += size_t(cells) * 160u + 24u;
    }
    if (patches.empty() || lattice.empty()) return false;

    std::vector<uint16_t> uv_u, uv_v;
    uint32_t uvt_w = 0, uvt_h = 0;
    {
        float rec_f = 0.0f;
        uint32_t rec_n = 0;
        if (r.f32(rec_f) && r.u32(rec_n) && rec_n <= 100000 &&
            r.skip(size_t(rec_n) * 18u)) {
            for (int t = 0; t < 2; ++t) {
                if (uint64_t(r.i) + 0x60 > uint64_t(r.n)) break;
                const uint8_t* tb = r.p + r.i;
                if (read_be_u32_raw(tb) != 0xFFFFFFFEu) break;
                if (read_be_u32_raw(tb + 0x18) != 98u) break;
                const uint32_t tw = read_be_u32_raw(tb + 0x10);
                const uint32_t th = read_be_u32_raw(tb + 0x14);
                const uint32_t mt = read_be_u32_raw(tb + 0x20);
                if (tw == 0 || th == 0 || tw > 65536 || th > 65536 ||
                    mt < 0x24 || mt > 0x200 ||
                    uint64_t(r.i) + mt + uint64_t(tw) * th * 2u >
                        uint64_t(r.n)) {
                    break;
                }
                std::vector<uint16_t>& dst = (t == 0) ? uv_u : uv_v;
                if (t == 1 && (tw != uvt_w || th != uvt_h)) break;
                dst.resize(size_t(tw) * th);
                const uint8_t* px = tb + mt;
                for (size_t k = 0; k < dst.size(); ++k) {
                    dst[k] = uint16_t((px[k * 2] << 8) | px[k * 2 + 1]);
                }
                uvt_w = tw;
                uvt_h = th;
                r.i += size_t(mt) + size_t(tw) * th * 2u;
            }
        }
    }
    const bool have_uv_tables =
        !uv_u.empty() && uv_v.size() == uv_u.size() &&
        uvt_w >= 17 && (uvt_w % 17u) == 0;
    const float org_x = read_be_f32_raw(ehf.data() + 27);
    const float org_y = read_be_f32_raw(ehf.data() + 31);

    EhfParsedBody parsed;
    const bool have_pages = ParseEhfBody(ehf, parsed) &&
                            parsed.bg_patches.size() == patches.size();

    out_geoms.reserve(patches.size());
    size_t textured = 0;
    for (size_t pi = 0; pi < patches.size(); ++pi) {
        const VP& p = patches[pi];
        const float ax = p.amin[0];
        const float ay = p.amin[1];
        const float sx = (p.amax[0] - p.amin[0]) / float(p.W);
        const float sy = (p.amax[1] - p.amin[1]) / float(p.H);
        if (!(sx > 0.0f) || !(sy > 0.0f) ||
            !std::isfinite(sx) || !std::isfinite(sy)) {
            continue;
        }
        const uint32_t VW = p.W + 1;
        const uint32_t VH = p.H + 1;

        std::vector<float>  gh(size_t(VW) * size_t(VH), 0.0f);
        std::vector<uint8_t> ghas(size_t(VW) * size_t(VH), 0);
        for (uint32_t j = 0; j < VH; ++j) {
            for (uint32_t i = 0; i < VW; ++i) {
                const uint64_t key = qkey(ax + sx * float(i), ay + sy * float(j));
                float hv = 0.0f;
                bool have = false;
                if (auto it = lattice.find(key); it != lattice.end()) {
                    hv = it->second; have = true;
                } else if (auto it2 = lattice_diag.find(key);
                           it2 != lattice_diag.end()) {
                    hv = it2->second; have = true;
                }
                if (have) {
                    gh[size_t(j) * VW + i]   = hv;
                    ghas[size_t(j) * VW + i] = 1;
                }
            }
        }
        for (uint32_t j = 0; j < VH; ++j) {
            for (uint32_t i = 0; i < VW; ++i) {
                const size_t gi = size_t(j) * VW + i;
                if (ghas[gi]) continue;
                float best = p.amin[2];
                int best_d = INT32_MAX;
                for (uint32_t jj = 0; jj < VH; ++jj) {
                    for (uint32_t ii = 0; ii < VW; ++ii) {
                        const size_t oi = size_t(jj) * VW + ii;
                        if (!ghas[oi]) continue;
                        const int d = std::abs(int(ii) - int(i)) +
                                      std::abs(int(jj) - int(j));
                        if (d < best_d) { best_d = d; best = gh[oi]; }
                    }
                }
                gh[gi] = best;
            }
        }

        auto h_at = [&](int i, int j) -> float {
            const int ci = std::clamp(i, 0, int(VW) - 1);
            const int cj = std::clamp(j, 0, int(VH) - 1);
            return gh[size_t(cj) * VW + size_t(ci)];
        };
        auto n_at = [&](int i, int j, float out[3]) {
            const float hl = h_at(i - 1, j);
            const float hr = h_at(i + 1, j);
            const float hd = h_at(i, j - 1);
            const float hu = h_at(i, j + 1);
            float nx = (hl - hr) * sy;
            float ny = 2.0f * sx * sy;
            float nz = (hd - hu) * sx;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }
            else { nx = 0.0f; ny = 1.0f; nz = 0.0f; }
            out[0] = nx; out[1] = ny; out[2] = nz;
        };

        Level::VistaPatchGeom vg;
        TerrainMesh& m = vg.mesh;
        m.min_height =  std::numeric_limits<float>::infinity();
        m.max_height = -std::numeric_limits<float>::infinity();

        const int file_cells_w = have_uv_tables ? int(uvt_w / 17u) : 0;
        const int cgx0 = int(std::lround((ax - org_x) / M));
        const int cgy0 = int(std::lround((ay - org_y) / M));
        const bool cells_in_table =
            have_uv_tables && cgx0 >= 0 && cgy0 >= 0 &&
            cgx0 + int(p.W) <= file_cells_w &&
            cgy0 + int(p.H) <= int(uvt_h) &&
            uint64_t(p.W) * p.H <= 4096;

        if (cells_in_table) {

            const size_t vtx_per_cell = 17u * 17u;
            m.positions.reserve(size_t(p.W) * p.H * vtx_per_cell * 3);
            m.uvs.reserve(size_t(p.W) * p.H * vtx_per_cell * 2);
            m.normals.reserve(size_t(p.W) * p.H * vtx_per_cell * 3);
            for (uint32_t cj = 0; cj < p.H; ++cj) {
                for (uint32_t ci = 0; ci < p.W; ++ci) {
                    const size_t row = size_t(cgy0 + int(cj)) * uvt_w;
                    const uint16_t* us = &uv_u[row + size_t(cgx0 + int(ci)) * 17u];
                    const uint16_t* vs = &uv_v[row + size_t(cgx0 + int(ci)) * 17u];
                    const float h00 = h_at(int(ci),     int(cj));
                    const float h10 = h_at(int(ci) + 1, int(cj));
                    const float h01 = h_at(int(ci),     int(cj) + 1);
                    const float h11 = h_at(int(ci) + 1, int(cj) + 1);
                    float n00[3], n10[3], n01[3], n11[3];
                    n_at(int(ci),     int(cj),     n00);
                    n_at(int(ci) + 1, int(cj),     n10);
                    n_at(int(ci),     int(cj) + 1, n01);
                    n_at(int(ci) + 1, int(cj) + 1, n11);
                    const uint32_t base = uint32_t(m.positions.size() / 3);
                    for (int j2 = 0; j2 <= 16; ++j2) {
                        const float fy = float(j2) / 16.0f;
                        for (int i2 = 0; i2 <= 16; ++i2) {
                            const float fx = float(i2) / 16.0f;
                            const float wx = ax + sx * (float(ci) + fx);
                            const float wy = ay + sy * (float(cj) + fy);
                            const float wh =
                                (h00 * (1.0f - fx) + h10 * fx) * (1.0f - fy) +
                                (h01 * (1.0f - fx) + h11 * fx) * fy;
                            m.positions.push_back(wx);
                            m.positions.push_back(wh);
                            m.positions.push_back(wy);
                            m.uvs.push_back(float(us[i2]) / 65535.0f);
                            m.uvs.push_back(float(vs[j2]) / 65535.0f);
                            float nv[3];
                            for (int k = 0; k < 3; ++k) {
                                nv[k] =
                                    (n00[k] * (1.0f - fx) + n10[k] * fx) *
                                        (1.0f - fy) +
                                    (n01[k] * (1.0f - fx) + n11[k] * fx) * fy;
                            }
                            const float nl = std::sqrt(nv[0] * nv[0] +
                                                       nv[1] * nv[1] +
                                                       nv[2] * nv[2]);
                            if (nl > 1e-6f) {
                                nv[0] /= nl; nv[1] /= nl; nv[2] /= nl;
                            }
                            m.normals.push_back(nv[0]);
                            m.normals.push_back(nv[1]);
                            m.normals.push_back(nv[2]);
                            m.min_height = std::min(m.min_height, wh);
                            m.max_height = std::max(m.max_height, wh);
                        }
                    }
                    for (int j2 = 0; j2 < 16; ++j2) {
                        for (int i2 = 0; i2 < 16; ++i2) {
                            const uint32_t i00 = base + uint32_t(j2 * 17 + i2);
                            const uint32_t i10 = i00 + 1;
                            const uint32_t i01 = i00 + 17;
                            const uint32_t i11 = i01 + 1;
                            m.indices.push_back(i00);
                            m.indices.push_back(i01);
                            m.indices.push_back(i10);
                            m.indices.push_back(i10);
                            m.indices.push_back(i01);
                            m.indices.push_back(i11);
                        }
                    }
                }
            }
        } else {

            const float patch_w = p.amax[0] - p.amin[0];
            const float patch_h = p.amax[1] - p.amin[1];
            const float inv_pw = patch_w > 0.0f ? 1.0f / patch_w : 0.0f;
            const float inv_ph = patch_h > 0.0f ? 1.0f / patch_h : 0.0f;
            m.positions.reserve(size_t(VW) * VH * 3);
            m.uvs.reserve(size_t(VW) * VH * 2);
            m.normals.reserve(size_t(VW) * VH * 3);
            for (uint32_t j = 0; j < VH; ++j) {
                for (uint32_t i = 0; i < VW; ++i) {
                    const float wx = ax + sx * float(i);
                    const float wy = ay + sy * float(j);
                    const float wh = gh[size_t(j) * VW + i];
                    m.positions.push_back(wx);
                    m.positions.push_back(wh);
                    m.positions.push_back(wy);
                    m.uvs.push_back((wx - ax) * inv_pw);
                    m.uvs.push_back((wy - ay) * inv_ph);
                    m.min_height = std::min(m.min_height, wh);
                    m.max_height = std::max(m.max_height, wh);
                    float nv[3];
                    n_at(int(i), int(j), nv);
                    m.normals.push_back(nv[0]);
                    m.normals.push_back(nv[1]);
                    m.normals.push_back(nv[2]);
                }
            }
            for (uint32_t j = 0; j + 1 < VH; ++j) {
                for (uint32_t i = 0; i + 1 < VW; ++i) {
                    const uint32_t i00 = uint32_t(size_t(j) * VW + i);
                    const uint32_t i10 = i00 + 1;
                    const uint32_t i01 = uint32_t(size_t(j + 1) * VW + i);
                    const uint32_t i11 = i01 + 1;
                    m.indices.push_back(i00);
                    m.indices.push_back(i01);
                    m.indices.push_back(i10);
                    m.indices.push_back(i10);
                    m.indices.push_back(i01);
                    m.indices.push_back(i11);
                }
            }
        }
        if (!std::isfinite(m.min_height)) m.min_height = 0.0f;
        if (!std::isfinite(m.max_height)) m.max_height = 0.0f;
        m.width  = VW;
        m.height = VH;
        m.ok = !m.indices.empty();
        if (!m.ok) continue;

        if (have_pages && !parsed.bg_patches[pi].pages.empty()) {
            const uint32_t off = parsed.bg_patches[pi].pages[0].first;
            if (decode_bg_page(ehf, off, vg.page_rgba, vg.page_w, vg.page_h)) {
                ++textured;
            }
        }
        out_geoms.push_back(std::move(vg));
    }

    if (out_geoms.empty()) return false;
    if (out_stats) {
        std::ostringstream ss;
        ss << out_geoms.size() << " patch mesh(es), " << textured
           << " textured, M=" << M
           << (have_uv_tables ? ", cell-atlas uv tables" : ", NO uv tables");
        *out_stats = ss.str();
    }
    return true;
}

}

bool BuildEhfVistaPatchGeoms(
    const std::vector<uint8_t>& ehf,
    std::vector<VistaPatchGeom>& out_geoms,
    std::string* out_stats)
{
    return build_ehf_vista_patch_geoms(ehf, out_geoms, out_stats);
}

bool Level::BakeEhfVistaPageComposite(const std::vector<uint8_t>& ehf,
                                      std::vector<uint8_t>& out_rgba,
                                      int&                  out_w,
                                      int&                  out_h,
                                      std::string&          out_name)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;
    out_name.clear();

    EhfParsedBody parsed;
    if (!ParseEhfBody(ehf, parsed)) {
        OutputLog::warn("vista pages: body parse failed: " + parsed.error);
        return false;
    }
    if (parsed.bg_patches.empty()) {
        OutputLog::warn("vista pages: no bg patches in body");
        return false;
    }

    float min_x =  std::numeric_limits<float>::infinity();
    float min_z =  std::numeric_limits<float>::infinity();
    float max_x = -std::numeric_limits<float>::infinity();
    float max_z = -std::numeric_limits<float>::infinity();
    for (const EhfBgPatch& p : parsed.bg_patches) {
        if (!std::isfinite(p.aabb_min[0]) || !std::isfinite(p.aabb_min[1]) ||
            !std::isfinite(p.aabb_max[0]) || !std::isfinite(p.aabb_max[1])) {
            continue;
        }
        min_x = std::min(min_x, p.aabb_min[0]);
        min_z = std::min(min_z, p.aabb_min[1]);
        max_x = std::max(max_x, p.aabb_max[0]);
        max_z = std::max(max_z, p.aabb_max[1]);
    }
    if (!std::isfinite(min_x) || !std::isfinite(min_z) ||
        !(max_x > min_x) || !(max_z > min_z)) {
        return false;
    }
    const float span_x = max_x - min_x;
    const float span_z = max_z - min_z;

    struct PatchTex {
        const EhfBgPatch*    patch = nullptr;
        std::vector<uint8_t> rgba;
        int                  w = 0, h = 0;
    };
    std::vector<PatchTex> decoded;
    decoded.reserve(parsed.bg_patches.size());
    std::vector<float> dens_x, dens_z;
    size_t n_nopages = 0, n_badhdr = 0, n_baddec = 0;
    for (const EhfBgPatch& p : parsed.bg_patches) {
        if (p.pages.empty()) { ++n_nopages; continue; }
        const uint32_t off = p.pages[0].first;
        const uint32_t len = p.pages[0].second;
        if (uint64_t(off) + len > ehf.size() || len < 0x60 ||
            ehf_be32(ehf, off) != 0xFFFFFFFEu) {
            ++n_badhdr;
            continue;
        }
        const uint32_t pf = ehf_be32(ehf, off + 0x18);
        if (pf != 35u) { ++n_badhdr; continue; }

        const uint32_t mt = ehf_be32(ehf, off + 0x20);
        if (mt < 0x54 || mt > 0x200 ||
            size_t(off) + mt + 8 > ehf.size()) { ++n_badhdr; continue; }
        const uint32_t comp_size = ehf_be32(ehf, off + mt + 4);
        size_t blob_end = size_t(off) + mt + 8 + comp_size;
        if (blob_end > ehf.size()) blob_end = ehf.size();
        std::vector<uint8_t> page(ehf.begin() + off, ehf.begin() + blob_end);
        TextureAtlas::DecodedAtlas dec = TextureAtlas::DecodeAtlas(page);
        if (!dec.ok || dec.rgba.empty()) { ++n_baddec; continue; }

        if (const char* dir = std::getenv("F2AB_DUMP_VISTA")) {
            static int s_pg = 0;
            if (s_pg < 4) {
                const std::string pp = std::string(dir) + "/page_" +
                    std::to_string(s_pg) + "_off" + std::to_string(off) +
                    "_" + std::to_string(dec.width) + "x" +
                    std::to_string(dec.height) + ".png";
                tex_export_png(pp, dec.rgba.data(), dec.width, dec.height);
                OutputLog::info("vista pages: dumped raw page " + pp +
                    " (blob " + std::to_string(blob_end - off) +
                    "B, comp " + std::to_string(comp_size) + ")");
                ++s_pg;
            }
        }

        PatchTex pt;
        pt.patch = &p;
        pt.rgba  = std::move(dec.rgba);
        pt.w     = dec.width;
        pt.h     = dec.height;
        const float sx = p.aabb_max[0] - p.aabb_min[0];
        const float sz = p.aabb_max[1] - p.aabb_min[1];
        if (sx > 0.0f && sz > 0.0f) {
            dens_x.push_back(float(pt.w) / sx);
            dens_z.push_back(float(pt.h) / sz);
        }
        decoded.push_back(std::move(pt));
    }
    if (decoded.empty() || dens_x.empty()) {
        OutputLog::warn("vista pages: no decodable pages (" +
                        std::to_string(parsed.bg_patches.size()) +
                        " patches: " + std::to_string(n_nopages) +
                        " without pages, " + std::to_string(n_badhdr) +
                        " bad header, " + std::to_string(n_baddec) +
                        " decode failed)");
        return false;
    }

    float f2 = 0.5f;
    if (ehf.size() >= 47) {
        f2 = read_be_f32_raw(ehf.data() + 43);
    }
    const float M = (std::isfinite(f2) && f2 > 0.0f) ? f2 * 16.0f : 8.0f;

    auto median = [](std::vector<float>& v) {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };

    float density_x = std::clamp(median(dens_x), 0.5f, 32.0f);
    float density_z = std::clamp(median(dens_z), 0.5f, 32.0f);
    constexpr float kMaxDim = 4096.0f;
    if (span_x * density_x > kMaxDim) density_x = kMaxDim / span_x;
    if (span_z * density_z > kMaxDim) density_z = kMaxDim / span_z;

    out_w = std::max(4, int(std::lround(span_x * density_x)));
    out_h = std::max(4, int(std::lround(span_z * density_z)));
    out_rgba.assign(size_t(out_w) * size_t(out_h) * 4, 0);
    std::vector<uint8_t> filled(size_t(out_w) * size_t(out_h), 0);

    size_t blitted = 0;
    for (const PatchTex& pt : decoded) {
        const EhfBgPatch& p = *pt.patch;
        const int dx = int(std::lround((p.aabb_min[0] - min_x) /
                                       span_x * float(out_w)));
        const int dy = int(std::lround((p.aabb_min[1] - min_z) /
                                       span_z * float(out_h)));
        const int dw = int(std::lround((p.aabb_max[0] - p.aabb_min[0]) /
                                       span_x * float(out_w)));
        const int dh = int(std::lround((p.aabb_max[1] - p.aabb_min[1]) /
                                       span_z * float(out_h)));
        if (dw <= 0 || dh <= 0 || dx < 0 || dy < 0) continue;

        const float tiles_x = std::max(1.0f, (p.aabb_max[0] - p.aabb_min[0]) / M);
        const float tiles_z = std::max(1.0f, (p.aabb_max[1] - p.aabb_min[1]) / M);
        const int nx = std::max(1, int(std::lround(tiles_x)));
        const int nz = std::max(1, int(std::lround(tiles_z)));
        for (int tz = 0; tz < nz; ++tz) {
            for (int tx = 0; tx < nx; ++tx) {
                const int tdx = dx + int(std::lround(float(tx) / float(nx)
                                                     * float(dw)));
                const int tdy = dy + int(std::lround(float(tz) / float(nz)
                                                     * float(dh)));
                const int tdw = dx + int(std::lround(float(tx + 1) / float(nx)
                                                     * float(dw))) - tdx;
                const int tdh = dy + int(std::lround(float(tz + 1) / float(nz)
                                                     * float(dh))) - tdy;
                if (tdw <= 0 || tdh <= 0) continue;
                blit_resampled_rgba(pt.rgba, pt.w, pt.h,
                                    out_rgba, out_w, out_h,
                                    tdx, tdy, tdw, tdh);
            }
        }
        const int cw = std::min(dw, out_w - dx);
        const int ch = std::min(dh, out_h - dy);
        for (int y = 0; y < ch; ++y) {
            std::memset(filled.data() + size_t(dy + y) * out_w + dx, 1, cw);
        }
        ++blitted;
    }
    if (blitted == 0) {
        out_rgba.clear();
        out_w = 0;
        out_h = 0;
        return false;
    }

    for (int pass = 0; pass < 4; ++pass) {
        std::vector<uint8_t> next = filled;
        bool changed = false;
        for (int y = 0; y < out_h; ++y) {
            for (int x = 0; x < out_w; ++x) {
                const size_t i = size_t(y) * out_w + x;
                if (filled[i]) continue;
                static const int dxs[4] = { 1, -1, 0,  0 };
                static const int dys[4] = { 0,  0, 1, -1 };
                for (int k = 0; k < 4; ++k) {
                    const int nx = x + dxs[k];
                    const int ny = y + dys[k];
                    if (nx < 0 || ny < 0 || nx >= out_w || ny >= out_h) continue;
                    const size_t ni = size_t(ny) * out_w + nx;
                    if (!filled[ni]) continue;
                    std::memcpy(out_rgba.data() + i * 4,
                                out_rgba.data() + ni * 4, 4);
                    next[i] = 1;
                    changed = true;
                    break;
                }
            }
        }
        filled.swap(next);
        if (!changed) break;
    }

    if (const char* dir = std::getenv("F2AB_DUMP_VISTA")) {
        static int s_seq = 0;
        const std::string path = std::string(dir) + "/vista_comp_" +
                                 std::to_string(s_seq++) + ".png";
        tex_export_png(path, out_rgba.data(), out_w, out_h);
        OutputLog::info("vista pages: dumped composite to " + path);
    }

    std::ostringstream os;
    os << "vista_pages[" << blitted << "/" << parsed.bg_patches.size()
       << " patches, " << out_w << "x" << out_h << "]";
    out_name = os.str();
    OutputLog::success("ehf: vista page composite " + std::to_string(out_w) +
                       "x" + std::to_string(out_h) + " from " +
                       std::to_string(blitted) + "/" +
                       std::to_string(parsed.bg_patches.size()) +
                       " bg-map pages");
    return true;
}

bool DecodeEhfTerrainAlbedoFromBytes(const std::vector<uint8_t>& ehf,
                                     uint32_t              cells_w,
                                     uint32_t              cells_h,
                                     std::vector<uint8_t>& out_rgba,
                                     int&                  out_w,
                                     int&                  out_h)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;
    if (ehf.empty() || cells_w == 0 || cells_h == 0) return false;

    if (DecodeEhfEmbeddedTileComposite(ehf, cells_w, cells_h,
                                       out_rgba, out_w, out_h)) {
        return true;
    }

    const uint8_t* eh_d = ehf.data();
    const size_t   eh_n = ehf.size();
    size_t  best_off = SIZE_MAX;
    uint32_t best_W = 0, best_H = 0;
    uint32_t best_raw = 0;

    auto u32_at = [&](size_t off) -> uint32_t {
        return (uint32_t(eh_d[off  ]) << 24) | (uint32_t(eh_d[off+1]) << 16) |
               (uint32_t(eh_d[off+2]) <<  8) |  uint32_t(eh_d[off+3]);
    };

    for (size_t i = 0; i + 84 < eh_n; ++i) {
        if (eh_d[i] != 0xFF || eh_d[i+1] != 0xFF ||
            eh_d[i+2] != 0xFF || eh_d[i+3] != 0xFE) continue;

        const uint32_t W  = u32_at(i + 16);
        const uint32_t H  = u32_at(i + 20);
        const uint32_t PF = u32_at(i + 24);
        const uint32_t mip_off = u32_at(i + 32);
        if (W == 0 || H == 0 || W > 8192 || H > 8192) continue;
        if (PF != 35u) continue;
        if (mip_off != 0x54) continue;

        if (i + mip_off + 4 > eh_n) continue;
        const uint32_t raw_size = u32_at(i + mip_off);
        if (raw_size > best_raw) {
            best_raw  = raw_size;
            best_off  = i;
            best_W = W; best_H = H;
        }
    }

    if (best_off != SIZE_MAX) {
        auto u32 = [&](size_t off) -> uint32_t {
            return (uint32_t(eh_d[off  ]) << 24) | (uint32_t(eh_d[off+1]) << 16) |
                   (uint32_t(eh_d[off+2]) <<  8) |  uint32_t(eh_d[off+3]);
        };
        const uint32_t mip_table_offset = u32(best_off + 32);
        const size_t mip_at = best_off + mip_table_offset;
        if (mip_at + 8 < eh_n) {
            const uint32_t raw_size  = u32(mip_at);
            const uint32_t comp_size = u32(mip_at + 4);
            const size_t   zlib_at   = mip_at + 8;

            if (zlib_at + comp_size <= eh_n) {
                std::vector<uint8_t> body(raw_size);
                z_stream zs{};
                zs.next_in   = const_cast<Bytef*>(eh_d + zlib_at);
                zs.avail_in  = (uInt)comp_size;
                zs.next_out  = body.data();
                zs.avail_out = (uInt)raw_size;
                int rc_init = inflateInit2(&zs, 15);
                int rc      = (rc_init == Z_OK) ? inflate(&zs, Z_FINISH) : Z_ERRNO;
                const size_t produced = raw_size - zs.avail_out;
                inflateEnd(&zs);

                if (rc_init == Z_OK && produced == raw_size) {
                    std::vector<uint8_t> bc1;
                    int dec_w = 0, dec_h = 0;
                    std::string err;
                    if (lh_decode_compressed_mip(body.data(), body.size(),
                                                 dec_w, dec_h, bc1, &err,
                                                 false))
                    {
                        std::vector<uint8_t> rgba;
                        if (TextureAtlas::DecodeRawBc1ToRgba(
                                bc1.data(), bc1.size(),
                                dec_w, dec_h, rgba))
                        {
                            const uint32_t terrain_cells_w =
                                cells_w > 1 ? cells_w - 1 : cells_w;
                            const uint32_t terrain_cells_h =
                                cells_h > 1 ? cells_h - 1 : cells_h;
                            const size_t terrain_area =
                                size_t(terrain_cells_w) *
                                size_t(terrain_cells_h);
                            const size_t decoded_area =
                                size_t(dec_w) * size_t(dec_h);
                            if (decoded_area < terrain_area / 2) {
                                std::ostringstream os;
                                os << "ehf: embedded BC1 page @0x"
                                   << std::hex << best_off << std::dec
                                   << " decoded as " << dec_w << "x" << dec_h
                                   << ", too small for full terrain";
                                OutputLog::info(os.str());
                            } else {
                                out_rgba = std::move(rgba);
                                out_w    = dec_w;
                                out_h    = dec_h;
                                std::ostringstream os;
                                os << "ehf: huffman BC1 baked albedo @0x"
                                   << std::hex << best_off << std::dec
                                   << "  header=" << best_W << "x" << best_H
                                   << "  decoded=" << dec_w << "x" << dec_h;
                                OutputLog::success(os.str());
                                return true;
                            }
                        }
                    } else {
                        OutputLog::warn("ehf: lh_decode_compressed_mip failed: "
                                        + err);
                    }
                } else {
                    std::ostringstream os;
                    os << "ehf: zlib inflate failed rc=" << rc
                       << " produced=" << produced << " of " << raw_size;
                    OutputLog::warn(os.str());
                }
            }
        }
    }

    auto round_up_pow2 = [](uint32_t n) {
        uint32_t p = 1; while (p < n) p <<= 1; return p;
    };
    const uint32_t pow2_W = round_up_pow2(cells_w);
    const uint32_t pow2_H = round_up_pow2(cells_h);

    struct Cand { uint32_t W, H; size_t bytes; };
    std::vector<Cand> cands;
    auto add = [&](uint32_t w, uint32_t h) {
        if (w == 0 || h == 0) return;
        if ((w & 3u) != 0 || (h & 3u) != 0) return;
        cands.push_back({w, h, (size_t)w * h / 2});
    };
    add(pow2_W, pow2_H);
    add(cells_w & ~3u, cells_h & ~3u);
    add(1024, 1024);
    add(1024,  768);
    add( 768, 1024);
    add(1024,  512);
    add( 512, 1024);
    add( 768,  768);
    add( 512,  512);
    add( 256,  256);
    const float terrain_aspect = (float)cells_w / (float)cells_h;
    std::sort(cands.begin(), cands.end(),
              [](const Cand& a, const Cand& b){ return a.bytes > b.bytes; });
    cands.erase(std::unique(cands.begin(), cands.end(),
        [](const Cand& a, const Cand& b){
            return a.W == b.W && a.H == b.H; }), cands.end());
    auto aspect_ok = [&](uint32_t w, uint32_t h) -> bool {
        float a = (float)w / (float)h;
        return a > terrain_aspect * 0.25f && a < terrain_aspect * 4.0f;
    };

    const size_t n = ehf.size();
    const uint8_t* d = ehf.data();
    auto u32be = [](const uint8_t* p) -> uint32_t {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
               (uint32_t(p[2]) <<  8) |  uint32_t(p[3]);
    };

    auto header_contradicts = [&](size_t zlib_at,
                                  uint32_t cand_w,
                                  uint32_t cand_h) -> bool {
        for (uint32_t mt = 0x54; mt <= 0x200; mt += 4) {
            if (zlib_at < size_t(mt) + 8) break;
            const size_t h0 = zlib_at - 8 - mt;
            if (u32be(d + h0) != 0xFFFFFFFEu) continue;
            if (u32be(d + h0 + 0x20) != mt) continue;
            const uint32_t pw = u32be(d + h0 + 0x10);
            const uint32_t ph = u32be(d + h0 + 0x14);
            if (pw == 0 || ph == 0 || pw > 8192 || ph > 8192) continue;
            return pw != cand_w || ph != cand_h;
        }
        return false;
    };

    struct Hit { uint32_t W, H; size_t bytes; size_t offset; uint32_t comp; };
    std::vector<Hit> hits;
    size_t i = 8;
    while (i + 2 < n) {
        if (d[i] == 0x78 &&
            (d[i+1] == 0xDA || d[i+1] == 0x9C ||
             d[i+1] == 0x01 || d[i+1] == 0x5E))
        {
            const uint32_t rs = u32be(d + i - 8);
            const uint32_t cs = u32be(d + i - 4);
            if (cs > 16 && (size_t)i + cs <= n) {
                for (const auto& c : cands) {
                    if (rs == (uint32_t)c.bytes &&
                        aspect_ok(c.W, c.H) &&
                        !header_contradicts(i, c.W, c.H))
                    {
                        hits.push_back({c.W, c.H, c.bytes, i, cs});
                        break;
                    }
                }
            }
        }
        ++i;
    }
    if (hits.empty()) {
        OutputLog::warn("ehf: no BC1 section matching any candidate (tried " +
                        std::to_string(cands.size()) + " sizes) found in " +
                        std::to_string(n) + "-byte .ehf");
        return false;
    }
    std::sort(hits.begin(), hits.end(),
              [](const Hit& a, const Hit& b){ return a.bytes > b.bytes; });
    const Hit& best = hits.front();

    const size_t per_cell_bytes = (size_t)(cells_w & ~3u) *
                                  (size_t)(cells_h & ~3u) / 2;
    {
        std::ostringstream os;
        os << "ehf: " << hits.size() << " BC1 candidate(s); picked "
           << best.W << "x" << best.H << " BC1 @0x" << std::hex
           << best.offset;
        OutputLog::info(os.str());
        if (best.bytes < per_cell_bytes / 2) {
            OutputLog::warn("ehf: picked page too small to be per-cell"
                            " baked albedo — falling back to atlas");
            return false;
        }
    }
    std::vector<uint8_t> rgba;
    if (!TextureAtlas::DecodeZlibBc1Page(d + best.offset, best.comp,
                                         best.bytes, (int)best.W, (int)best.H,
                                         rgba)) {
        OutputLog::warn("ehf: candidate at 0x" +
                        std::to_string((unsigned long long)best.offset) +
                        " (" + std::to_string(best.W) + "x" +
                        std::to_string(best.H) + " BC1) failed to decode");
        return false;
    }
    out_rgba = std::move(rgba);
    out_w    = (int)best.W;
    out_h    = (int)best.H;
    return true;
}

bool DecodeEhfTerrainAlbedo(const FlatAssetEntry& level_entry,
                            uint32_t              cells_w,
                            uint32_t              cells_h,
                            std::vector<uint8_t>& out_rgba,
                            int&                  out_w,
                            int&                  out_h)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;

    if (!g_pending_terrain_ehf_bytes.empty() &&
        g_pending_terrain_level_entry.full_path == level_entry.full_path)
    {
        return DecodeEhfTerrainAlbedoFromBytes(
            g_pending_terrain_ehf_bytes,
            cells_w, cells_h, out_rgba, out_w, out_h);
    }

    for (const auto& fe : S.all_heightfield_files) {
        std::string nlow = fe.name;
        std::transform(nlow.begin(), nlow.end(), nlow.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        if (nlow.size() < 4 ||
            nlow.compare(nlow.size() - 4, 4, ".ehf") != 0) continue;
        try {
            auto v = BnkCache::extract_bytes(fe.bnk_path, fe.file_index);
            if (v.empty()) continue;
            std::vector<uint8_t> blob(v.begin(), v.end());
            if (DecodeEhfTerrainAlbedoFromBytes(blob, cells_w, cells_h,
                                                out_rgba, out_w, out_h))
                return true;
        } catch (...) {}
    }
    OutputLog::warn("ehf: no usable .ehf found for level "
                    + level_entry.name);
    return false;
}

bool DecodeEhfPaletteFirstDiffuse(const std::vector<uint8_t>& ehf,
                                  std::vector<uint8_t>& out_rgba,
                                  int&                  out_w,
                                  int&                  out_h,
                                  float&                out_tile_scale,
                                  std::string&          out_picked_name)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;
    out_tile_scale = 1.0f;
    out_picked_name.clear();
    if (ehf.empty()) return false;

    EhfPalette::Palette pal = EhfPalette::Parse(ehf);
    if (!pal.ok || pal.entries.empty()) {
        OutputLog::warn("ehf palette: parse failed or empty");
        return false;
    }

    auto basename_lower = [](const std::string& path) {
        std::string base = std::filesystem::path(path).filename().string();
        std::transform(base.begin(), base.end(), base.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        return base;
    };

    OutputLog::info("ehf palette: searching " +
                    std::to_string(S.all_tex_files.size()) +
                    " indexed .tex files for " +
                    std::to_string(pal.entries.size()) +
                    " palette diffuse references...");

    for (size_t pi = 0; pi < pal.entries.size(); ++pi) {
        const auto& e = pal.entries[pi];
        const std::string want = basename_lower(e.diffuse_path);
        if (want.empty()) continue;

        const FlatAssetEntry* hit = nullptr;
        for (const auto& tex : S.all_tex_files) {
            std::string nm = std::filesystem::path(tex.name)
                                 .filename().string();
            std::transform(nm.begin(), nm.end(), nm.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            if (nm == want) { hit = &tex; break; }
        }
        if (!hit) {
            if (pi < 6) {
                OutputLog::info("  [" + std::to_string(pi) +
                                "] not found: " + want);
            }
            continue;
        }

        std::vector<uint8_t> blob;
        try {
            auto v = BnkCache::extract_bytes(hit->bnk_path, hit->file_index);
            if (!v.empty()) blob.assign(v.begin(), v.end());
        } catch (...) {}
        if (blob.empty()) {
            OutputLog::warn("  [" + std::to_string(pi) +
                            "] " + want + " found in " + hit->bnk_path +
                            " but extract returned empty");
            continue;
        }

        std::vector<unsigned char> blob_uc(blob.begin(), blob.end());
        std::vector<uint8_t> rgba;
        bool has_alpha = false;
        int w = 0, h = 0;
        if (!decode_tex_to_rgba(blob_uc, rgba, w, h, &has_alpha, -1)) {
            const std::string& reason = mp_last_decode_fail_reason();
            const std::string& info   = mp_last_decode_info();
            OutputLog::warn("  [" + std::to_string(pi) + "] " + want +
                            " decode failed: " + reason +
                            (info.empty() ? "" : " (" + info + ")"));
            continue;
        }

        out_rgba = std::move(rgba);
        out_w = w;
        out_h = h;
        out_tile_scale = e.tile_scale;
        out_picked_name = basename_lower(e.diffuse_path);
        std::ostringstream os;
        os << "ehf palette: picked entry " << pi << " '" << out_picked_name
           << "' (" << w << "x" << h
           << ", tile_scale=" << e.tile_scale << ")";
        OutputLog::success(os.str());
        return true;
    }

    OutputLog::warn("ehf palette: NONE of " +
                    std::to_string(pal.entries.size()) +
                    " palette diffuse .tex files found in the "
                    + std::to_string(S.all_tex_files.size())
                    + "-entry global .tex index");
    return false;
}

bool BakeEhfTerrainComposite(const std::vector<uint8_t>& ehf,
                             std::vector<uint8_t>&  out_rgba,
                             int&                   out_w,
                             int&                   out_h,
                             std::string&           out_picked_name)
{
    return BakeEhfTerrainCompositeWithBnk(ehf, {},
                                          out_rgba, out_w, out_h,
                                          out_picked_name);
}

namespace { bool g_capture_splat_output = false;
            std::vector<uint8_t>* g_splat_output_rgba = nullptr;
            int* g_splat_output_w = nullptr;
            int* g_splat_output_h = nullptr; }

bool BakeEhfTerrainCompositeAndSplat(
    const std::vector<uint8_t>& ehf,
    const std::string& preferred_bnk,
    std::vector<uint8_t>& out_rgba,
    int& out_w, int& out_h,
    std::string& out_picked_name,
    std::vector<uint8_t>& out_splat_rgba,
    int& out_splat_w, int& out_splat_h)
{
    g_capture_splat_output = true;
    g_splat_output_rgba    = &out_splat_rgba;
    g_splat_output_w       = &out_splat_w;
    g_splat_output_h       = &out_splat_h;
    bool ok = BakeEhfTerrainCompositeWithBnk(ehf, preferred_bnk,
                                             out_rgba, out_w, out_h,
                                             out_picked_name);
    g_capture_splat_output = false;
    g_splat_output_rgba = nullptr;
    g_splat_output_w = nullptr;
    g_splat_output_h = nullptr;
    return ok;
}

bool BakeEhfTerrainCompositeWithBnk(const std::vector<uint8_t>& ehf,
                                    const std::string& preferred_bnk,
                                    std::vector<uint8_t>&  out_rgba,
                                    int&                   out_w,
                                    int&                   out_h,
                                    std::string&           out_picked_name,
                                    bool                   allow_embedded_albedo)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;
    out_picked_name.clear();
    if (ehf.empty()) return false;

    HeightfieldHeader hdr;
    {
        static constexpr char   kMagic[]   = "HeightFieldGraphicsFile";
        static constexpr size_t kMagicLen  = sizeof(kMagic) - 1;
        static constexpr size_t kHeaderLen = 63;
        if (ehf.size() < kHeaderLen) return false;
        if (std::memcmp(ehf.data(), kMagic, kMagicLen) != 0) return false;
        auto be_u32 = [&](size_t off) -> uint32_t {
            return (uint32_t(ehf[off]) << 24) | (uint32_t(ehf[off+1]) << 16)
                 | (uint32_t(ehf[off+2]) << 8) |  uint32_t(ehf[off+3]);
        };
        hdr.magic.assign(kMagic);
        hdr.version     = be_u32(kMagicLen);
        hdr.u0          = be_u32(35);
        hdr.u1          = be_u32(39);
        hdr.body_offset = be_u32(55);
        hdr.body_size   = be_u32(59);
        hdr.ok          = (uint64_t(hdr.body_offset) + hdr.body_size <= ehf.size());
    }
    if (!hdr.ok || hdr.u0 == 0 || hdr.u1 == 0) {
        OutputLog::warn("bake composite: bad .ehf header");
        return false;
    }

    EhfParsedBody parsed;
    const bool parsed_ok = ParseEhfBody(ehf, parsed);
    if (parsed_ok) {
        std::ostringstream pos;
        pos << "ehf chunk parse: " << parsed.chunk_w << "x"
            << parsed.chunk_h << " chunks, "
            << parsed.lods.size() << " LODs"
            << "  (consumed " << parsed.bytes_consumed
            << "B, remaining " << parsed.bytes_remaining << "B)";
        OutputLog::success(pos.str());

        std::vector<TerrainTextureRegistry::LodPaletteEntry> pe;
        pe.reserve(parsed.lods.size());
        for (const auto& L : parsed.lods) {
            TerrainTextureRegistry::LodPaletteEntry e;
            e.base_diffuse   = L.strs[0];
            e.base_normal    = L.strs[1];
            e.detail_diffuse = L.strs[3];
            e.detail_normal  = L.strs[4];
            e.base_tile_scale   = L.params[0][0];
            e.base_intensity    = L.params[0][1];
            e.detail_tile_scale = L.params[1][0];
            e.detail_intensity  = L.params[1][1];
            pe.push_back(std::move(e));
        }
        TerrainTextureRegistry::SetLodPalette(std::move(pe));
    } else {
        OutputLog::warn("bake composite: chunk parse failed: " + parsed.error);
    }

    if (allow_embedded_albedo &&
        DecodeEhfTerrainAlbedoFromBytes(ehf, hdr.u0, hdr.u1,
                                        out_rgba, out_w, out_h))
    {
        out_picked_name = "embedded_tile_albedo";
        return true;
    }

    if (!parsed_ok) return false;

    std::vector<uint8_t> lm_rgba;
    int lm_w = 0, lm_h = 0;
    {
        const uint8_t* p = ehf.data() + hdr.body_offset;
        std::vector<uint8_t> body_slice(p, p + hdr.body_size);
        auto dec = TextureAtlas::DecodeAtlas(body_slice);
        if (!dec.ok || dec.pixel_format != 24u) {
            OutputLog::warn("bake composite: .ehf body decode failed: " +
                            dec.error);
            return false;
        }
        lm_rgba = std::move(dec.rgba);
        lm_w    = dec.width;
        lm_h    = dec.height;
    }

    auto basename_lower = [](const std::string& path) {
        std::string base = std::filesystem::path(path).filename().string();
        std::transform(base.begin(), base.end(), base.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        return base;
    };

    struct Mat {
        bool                 decoded = false;
        std::vector<uint8_t> rgba;
        int                  w = 0, h = 0;
        std::string          name;
        float                tile_scale = 0.125f;
    };
    EhfPalette::Palette pal = EhfPalette::Parse(ehf);
    std::vector<Mat> mats(parsed.lods.size());
    int first_decoded = -1;
    for (size_t li = 0; li < parsed.lods.size(); ++li) {
        const std::string diffuse_path = parsed.lods[li].strs[0];
        if (diffuse_path.empty()) continue;
        const std::string want = basename_lower(diffuse_path);

        std::vector<unsigned char> blob_uc;
        bool stitched = false;
        try {
            stitched = build_any_tex_buffer_for_name(want, blob_uc,
                                                    preferred_bnk);
        } catch (...) { stitched = false; }
        if (!stitched || blob_uc.empty()) {
            const FlatAssetEntry* hit = nullptr;
            for (const auto& tex : S.all_tex_files) {
                std::string nm = std::filesystem::path(tex.name)
                                     .filename().string();
                std::transform(nm.begin(), nm.end(), nm.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                if (nm == want) { hit = &tex; break; }
            }
            if (!hit) continue;
            try {
                auto v = BnkCache::extract_bytes(hit->bnk_path,
                                                 hit->file_index);
                if (!v.empty()) blob_uc.assign(v.begin(), v.end());
            } catch (...) {}
            if (blob_uc.empty()) continue;
        }

        std::vector<uint8_t> rgba;
        bool has_alpha = false;
        int w = 0, h = 0;
        if (!decode_tex_to_rgba(blob_uc, rgba, w, h, &has_alpha, -1)) continue;
        mats[li].decoded = true;
        mats[li].rgba    = std::move(rgba);
        mats[li].w       = w;
        mats[li].h       = h;
        mats[li].name    = want;
        for (const auto& pe : pal.entries) {
            std::string pn = std::filesystem::path(pe.diffuse_path)
                                 .filename().string();
            std::transform(pn.begin(), pn.end(), pn.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            if (pn == want) {
                mats[li].tile_scale = pe.tile_scale;
                break;
            }
        }
        if (first_decoded < 0) first_decoded = (int)li;
    }

    if (first_decoded < 0) {
        OutputLog::warn("bake composite: no LOD diffuse texture decoded");
        return false;
    }

    {
        int n = 0;
        for (auto& m : mats) if (m.decoded) ++n;
        std::ostringstream os;
        os << "decoded " << n << " of " << mats.size() << " LOD diffuses:";
        OutputLog::info(os.str());
        for (size_t i = 0; i < mats.size() && i < 8; ++i) {
            if (!mats[i].decoded) continue;
            OutputLog::info("  LOD[" + std::to_string(i) + "] "
                            + mats[i].name);
        }
    }
    out_picked_name = "chunkgrid["
        + std::to_string(parsed.chunk_w) + "x"
        + std::to_string(parsed.chunk_h) + " × "
        + std::to_string(mats.size()) + " LODs]";

    if (g_capture_splat_output && g_splat_output_rgba) {
        g_splat_output_rgba->clear();
        if (g_splat_output_w) *g_splat_output_w = 0;
        if (g_splat_output_h) *g_splat_output_h = 0;
    }

    const size_t pix = size_t(lm_w) * size_t(lm_h);
    out_rgba.assign(pix * 4, 0);
    out_w = lm_w;
    out_h = lm_h;

    float world_min_x =  1e30f;
    float world_min_z =  1e30f;
    float world_max_x = -1e30f;
    float world_max_z = -1e30f;
    for (const auto& c : parsed.chunks) {
        world_min_x = std::min(world_min_x, c.origin[0]);
        world_min_z = std::min(world_min_z, c.origin[1]);
        world_max_x = std::max(world_max_x, c.extent[0]);
        world_max_z = std::max(world_max_z, c.extent[1]);
    }
    const float world_span_x = std::max(1e-6f, world_max_x - world_min_x);
    const float world_span_z = std::max(1e-6f, world_max_z - world_min_z);
    const float chunk_size_x = world_span_x / std::max(1u, parsed.chunk_w);
    const float chunk_size_z = world_span_z / std::max(1u, parsed.chunk_h);
    std::vector<const EhfChunk*> chunk_grid(
        size_t(parsed.chunk_w) * size_t(parsed.chunk_h), nullptr);
    for (const auto& c : parsed.chunks) {
        const int cx = std::clamp(
            int(std::lround((c.origin[0] - world_min_x) / chunk_size_x)),
            0, int(parsed.chunk_w) - 1);
        const int cy = std::clamp(
            int(std::lround((c.origin[1] - world_min_z) / chunk_size_z)),
            0, int(parsed.chunk_h) - 1);
        chunk_grid[size_t(cy) * size_t(parsed.chunk_w) + size_t(cx)] = &c;
    }
    {
        std::ostringstream os;
        os << "ehf chunk world bounds: x=[" << world_min_x << ".."
           << world_max_x << "] z=[" << world_min_z << ".."
           << world_max_z << "] chunk=(" << chunk_size_x << ","
           << chunk_size_z << ")";
        OutputLog::info(os.str());
    }

    auto sample_mat = [&](int idx, float u_world, float v_world,
                          uint8_t out_rgb[3])
    {
        const Mat& m = (idx >= 0 && idx < (int)mats.size() && mats[idx].decoded)
            ? mats[idx] : mats[first_decoded];
        const float ts = (m.tile_scale > 0.f && m.tile_scale < 1.f)
                            ? m.tile_scale : 0.125f;
        float u = (u_world * ts);
        float v = (v_world * ts);
        u = u - std::floor(u);
        v = v - std::floor(v);
        const float fx = u * m.w;
        const float fy = v * m.h;
        const int x0 = int(fx);
        const int y0 = int(fy);
        const int x1 = (x0 + 1) % m.w;
        const int y1 = (y0 + 1) % m.h;
        const float dx = fx - float(x0);
        const float dy = fy - float(y0);
        const uint8_t* p00 = m.rgba.data() + (size_t(y0) * m.w + x0) * 4;
        const uint8_t* p10 = m.rgba.data() + (size_t(y0) * m.w + x1) * 4;
        const uint8_t* p01 = m.rgba.data() + (size_t(y1) * m.w + x0) * 4;
        const uint8_t* p11 = m.rgba.data() + (size_t(y1) * m.w + x1) * 4;
        const float w00b = (1.f - dx) * (1.f - dy);
        const float w10b =        dx  * (1.f - dy);
        const float w01b = (1.f - dx) *        dy;
        const float w11b =        dx  *        dy;
        for (int c = 0; c < 3; ++c) {
            out_rgb[c] = uint8_t(
                w00b * p00[c] + w10b * p10[c] +
                w01b * p01[c] + w11b * p11[c]);
        }
    };

    auto sample_mask = [&](const EhfChunkLayer& L,
                           float local_x, float local_z) -> float
    {
        if (parsed.splat_indices.empty() ||
            parsed.splat_w == 0 || parsed.splat_h == 0 ||
            parsed.splat_indices.size() !=
                size_t(parsed.splat_w) * size_t(parsed.splat_h))
        {
            return 1.0f;
        }

        const float scale_u = (L.mask_scale[0] > 0.0f)
            ? L.mask_scale[0]
            : 32.0f / float(parsed.splat_w);
        const float scale_v = (L.mask_scale[1] > 0.0f)
            ? L.mask_scale[1]
            : 32.0f / float(parsed.splat_h);

        const float u = L.tile_uv[0]
            + std::clamp(local_x, 0.0f, 1.0f)
            * scale_u * 2.0f;
        const float v = L.tile_uv[1]
            + std::clamp(local_z, 0.0f, 1.0f)
            * scale_v * 2.0f;

        float px = u * float(parsed.splat_w) - 0.5f;
        float py = v * float(parsed.splat_h) - 0.5f;
        px = std::clamp(px, 0.0f, float(parsed.splat_w - 1));
        py = std::clamp(py, 0.0f, float(parsed.splat_h - 1));

        const int x0 = int(px);
        const int y0 = int(py);
        const int x1 = std::min<int>(x0 + 1, int(parsed.splat_w) - 1);
        const int y1 = std::min<int>(y0 + 1, int(parsed.splat_h) - 1);
        const float dx = px - float(x0);
        const float dy = py - float(y0);
        auto at = [&](int x, int y) -> float {
            return parsed.splat_indices[
                size_t(y) * size_t(parsed.splat_w) + size_t(x)] / 255.0f;
        };
        const float w00m = (1.0f - dx) * (1.0f - dy);
        const float w10m =         dx  * (1.0f - dy);
        const float w01m = (1.0f - dx) *         dy;
        const float w11m =         dx  *         dy;
        return std::clamp(at(x0, y0) * w00m + at(x1, y0) * w10m
                        + at(x0, y1) * w01m + at(x1, y1) * w11m,
                          0.0f, 1.0f);
    };

    constexpr float kBlendMax     = 3.0f;

    for (int y = 0; y < lm_h; ++y) {
        const float v_norm = (lm_h > 1)
            ? float(y) / float(lm_h - 1)
            : 0.0f;
        const float world_z = world_min_z + v_norm * world_span_z;
        const float fy_chunk = (world_z - world_min_z) / chunk_size_z;
        const int   cy       = std::min<int>(parsed.chunk_h - 1, int(fy_chunk));
        const float fy_in    = std::clamp(fy_chunk - float(cy), 0.f, 1.f);
        for (int x = 0; x < lm_w; ++x) {
            const float u_norm = (lm_w > 1)
                ? float(x) / float(lm_w - 1)
                : 0.0f;
            const float world_x = world_min_x + u_norm * world_span_x;
            const float fx_chunk = (world_x - world_min_x) / chunk_size_x;
            const int   cx       = std::min<int>(parsed.chunk_w - 1, int(fx_chunk));
            const float fx_in    = std::clamp(fx_chunk - float(cx), 0.f, 1.f);

            const float w00 = (1.f - fx_in) * (1.f - fy_in);
            const float w10 =        fx_in  * (1.f - fy_in);
            const float w01 = (1.f - fx_in) *        fy_in;
            const float w11 =        fx_in  *        fy_in;

            const size_t chunk_index =
                size_t(cy) * size_t(parsed.chunk_w) + size_t(cx);
            const EhfChunk& chunk =
                (chunk_index < chunk_grid.size() && chunk_grid[chunk_index])
                    ? *chunk_grid[chunk_index]
                    : parsed.chunks.front();

            float accum_r = 0.f, accum_g = 0.f, accum_b = 0.f;
            float accum_a = 0.f;
            uint8_t first_rgb[3] = {0, 0, 0};
            bool have_first_rgb = false;

            const float wu = world_x;
            const float wv = world_z;

            for (const auto& L : chunk.layers) {
                auto corner_material = [&](int corner) -> int {
                    const uint32_t layer_idx = L.texture_idx[corner];
                    uint32_t mat_idx = L.material_idx;
                    if (layer_idx < chunk.layers.size()) {
                        mat_idx = chunk.layers[size_t(layer_idx)].material_idx;
                    }
                    if (mat_idx < mats.size()) return int(mat_idx);
                    if (L.material_idx < mats.size()) return int(L.material_idx);
                    return -1;
                };
                const int material_ids[4] = {
                    corner_material(0), corner_material(1),
                    corner_material(2), corner_material(3),
                };
                const float corner_alpha[4] = {
                    w00 * float(L.blend[0]) / kBlendMax,
                    w10 * float(L.blend[1]) / kBlendMax,
                    w01 * float(L.blend[2]) / kBlendMax,
                    w11 * float(L.blend[3]) / kBlendMax,
                };
                const float blend_px = corner_alpha[0] + corner_alpha[1] +
                                       corner_alpha[2] + corner_alpha[3];
                if (blend_px <= 1e-6f) continue;

                float rgb_f[3] = {0.0f, 0.0f, 0.0f};
                bool have_corner_rgb = false;
                for (int ci = 0; ci < 4; ++ci) {
                    if (material_ids[ci] < 0 || corner_alpha[ci] <= 0.0f) {
                        continue;
                    }
                    uint8_t corner_rgb[3];
                    sample_mat(material_ids[ci], wu, wv, corner_rgb);
                    const float w = corner_alpha[ci] / blend_px;
                    rgb_f[0] += float(corner_rgb[0]) * w;
                    rgb_f[1] += float(corner_rgb[1]) * w;
                    rgb_f[2] += float(corner_rgb[2]) * w;
                    have_corner_rgb = true;
                }
                if (!have_corner_rgb) continue;
                uint8_t rgb[3];
                rgb[0] = uint8_t(std::clamp(int(std::round(rgb_f[0])), 0, 255));
                rgb[1] = uint8_t(std::clamp(int(std::round(rgb_f[1])), 0, 255));
                rgb[2] = uint8_t(std::clamp(int(std::round(rgb_f[2])), 0, 255));
                if (!have_first_rgb) {
                    first_rgb[0] = rgb[0];
                    first_rgb[1] = rgb[1];
                    first_rgb[2] = rgb[2];
                    have_first_rgb = true;
                }

                const float alpha = std::clamp(blend_px, 0.f, 1.f)
                                  * sample_mask(L, fx_in, fy_in);
                if (alpha < 1.f / 255.f) continue;

                const float keep = 1.0f - alpha;
                accum_r = accum_r * keep + float(rgb[0]) * alpha;
                accum_g = accum_g * keep + float(rgb[1]) * alpha;
                accum_b = accum_b * keep + float(rgb[2]) * alpha;
                accum_a = accum_a * keep + alpha;
            }

            if (accum_a < 0.999f && have_first_rgb) {
                const float fill = 1.0f - accum_a;
                accum_r += float(first_rgb[0]) * fill;
                accum_g += float(first_rgb[1]) * fill;
                accum_b += float(first_rgb[2]) * fill;
                accum_a = 1.0f;
            }

            if (accum_a > 1e-4f && accum_a < 0.999f) {
                accum_r /= accum_a;
                accum_g /= accum_a;
                accum_b /= accum_a;
            } else {
                uint8_t base[3];
                sample_mat(first_decoded, wu, wv, base);
                if (!have_first_rgb) {
                    accum_r = base[0]; accum_g = base[1]; accum_b = base[2];
                }
            }

            uint8_t* dst = out_rgba.data() + (size_t(y) * lm_w + x) * 4;
            dst[0] = uint8_t(std::clamp(accum_r, 0.f, 255.f));
            dst[1] = uint8_t(std::clamp(accum_g, 0.f, 255.f));
            dst[2] = uint8_t(std::clamp(accum_b, 0.f, 255.f));
            dst[3] = 0xFF;
        }
    }

    std::ostringstream os;
    os << "bake composite: " << lm_w << "x" << lm_h
       << " (chunk grid " << parsed.chunk_w << "x" << parsed.chunk_h
       << " × " << parsed.lods.size() << " LODs × multi-layer)";
    OutputLog::success(os.str());
    return true;
}

}
