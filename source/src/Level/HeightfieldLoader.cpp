#include "HeightfieldLoader.h"

#include "../Utilities/State.h"
#include "../BNKCore.cpp"

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace Level {

namespace {

/* Big-endian read helpers. */
uint32_t be_u32(const uint8_t* p) {
    return  (uint32_t(p[0]) << 24)
          | (uint32_t(p[1]) << 16)
          | (uint32_t(p[2]) <<  8)
          |  uint32_t(p[3]);
}
float be_f32(const uint8_t* p) {
    uint32_t u = be_u32(p);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

/* String→lower + backslash→forward-slash, matches the lookup keys
   BnkCache::find_index expects.  Cheap, allocates once. */
std::string normalize_key(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    std::replace(out.begin(), out.end(), '\\', '/');
    return out;
}

/* gunzip a Fable 2 .ghf / .lmp blob into a flat vector.  The files
   start with the standard gzip magic (1F 8B 08 08 …) and contain
   the payload inside.  zlib's inflateInit2(.., 15+32) auto-detects
   gzip vs. zlib stream framing. */
bool gunzip(const std::vector<uint8_t>& in,
            std::vector<uint8_t>&       out,
            std::string&                err)
{
    out.clear();
    if (in.size() < 18 || in[0] != 0x1F || in[1] != 0x8B) {
        err = "not a gzip stream (magic mismatch)";
        return false;
    }

    z_stream zs{};
    zs.next_in   = const_cast<Bytef*>(in.data());
    zs.avail_in  = static_cast<uInt>(in.size());
    /* 15 = max window bits, +32 = auto-detect gzip OR zlib. */
    if (inflateInit2(&zs, 15 + 32) != Z_OK) {
        err = "inflateInit2 failed";
        return false;
    }

    out.resize(in.size() * 4);     
    size_t produced = 0;
    while (true) {
        zs.next_out  = out.data() + produced;
        zs.avail_out = static_cast<uInt>(out.size() - produced);
        int rc = inflate(&zs, Z_NO_FLUSH);
        produced = out.size() - zs.avail_out;
        if (rc == Z_STREAM_END) break;
        if (rc == Z_OK) {
            /* Buffer too small — grow and retry. */
            if (zs.avail_out == 0) out.resize(out.size() * 2);
            continue;
        }
        inflateEnd(&zs);
        err = std::string("inflate failed: ") + (zs.msg ? zs.msg : "?");
        out.clear();
        return false;
    }
    inflateEnd(&zs);
    out.resize(produced);
    return true;
}

bool extract_bnk_file_by_relpath(const std::string&     relative_path,
                                 std::vector<uint8_t>&  out_bytes,
                                 std::string&           err)
{
    out_bytes.clear();
    const std::string key = normalize_key(relative_path);

    /* First try the index built up in TreeBuilder — it gives us
       the BNK path + file index in O(1), no per-BNK iteration. */
    for (const auto& fe : S.all_heightfield_files) {
        if (normalize_key(fe.full_path) != key) continue;
        try {
            auto bytes = BnkCache::extract_bytes(fe.bnk_path, fe.file_index);
            out_bytes.assign(bytes.begin(), bytes.end());
            return !out_bytes.empty();
        } catch (...) {
            err = "BnkCache::extract_bytes threw for " + relative_path;
            return false;
        }
    }

    /* Not in the heightfield index — try a generic BNK scan as a
       last resort.  We don't have a global file→BNK reverse index
       yet, so walk the loaded BNK list. */
    for (const auto& bnk_path : S.bnk_paths) {
        int idx = BnkCache::find_index(bnk_path, key);
        if (idx < 0) continue;
        try {
            auto bytes = BnkCache::extract_bytes(bnk_path, idx);
            out_bytes.assign(bytes.begin(), bytes.end());
            return !out_bytes.empty();
        } catch (...) {
            err = "BnkCache::extract_bytes threw for " + relative_path;
            return false;
        }
    }
    err = "no BNK contains " + relative_path;
    return false;
}

void parse_ehf_header(const std::vector<uint8_t>& bytes,
                      HeightfieldHeader&          out)
{
    out = {};
    /* Full header is 63 bytes — see the docstring on HeightfieldHeader
       for the IDA-validated layout (`sub_82A855A8` opens the .ehf as a
       bundle and issues `bundle::read(buf, 0, 63)` once to grab the
       header, then a second `bundle::read(buf, body_offset, body_size)`
       to pull the body blob).                                          */
    static constexpr char   kMagic[]   = "HeightFieldGraphicsFile";
    static constexpr size_t kMagicLen  = sizeof(kMagic) - 1;       
    static constexpr size_t kHeaderLen = 63;

    if (bytes.size() < kHeaderLen) return;
    if (std::memcmp(bytes.data(), kMagic, kMagicLen) != 0) return;

    const uint8_t* p = bytes.data();
    out.magic.assign(kMagic);
    out.version      = be_u32(p + kMagicLen);          
    out.prefix_float = be_f32(p + kMagicLen + 4);      
    out.f0           = be_f32(p + 27);                 
    out.f1           = be_f32(p + 31);                 
    out.u0           = be_u32(p + 35);                 
    out.u1           = be_u32(p + 39);                 
    out.f2           = be_f32(p + 43);                 
    out.f3           = be_f32(p + 47);                 
    out.f4           = be_f32(p + 51);                 
    out.body_offset  = be_u32(p + 55);
    out.body_size    = be_u32(p + 59);
    out.ok           = true;
}

}  

const FlatAssetEntry* FindHeightfieldByPath(const std::string& relative_path)
{
    const std::string key = normalize_key(relative_path);
    for (const auto& fe : S.all_heightfield_files) {
        if (normalize_key(fe.full_path) == key) return &fe;
    }
    return nullptr;
}

bool LoadHeightfieldFiles(const std::string& ehf_path,
                          const std::string& ghf_path,
                          const std::string& /*hdb_path*/,
                          const std::string& /*genv_path*/,
                          HeightfieldFiles&  out)
{
    out = {};

    std::string err;

    /* .ehf — load raw, parse header. */
    if (!ehf_path.empty()) {
        if (!extract_bnk_file_by_relpath(ehf_path, out.ehf_bytes, err)) {
            out.error = ".ehf load failed: " + err;
            return false;
        }
        parse_ehf_header(out.ehf_bytes, out.ehf_header);
        if (out.ehf_header.magic.empty()) {
            out.error = ".ehf magic mismatch — not a HeightFieldGraphicsFile";
            return false;
        }
    }

    /* .ghf — load raw, gunzip into ghf_bytes_raw. */
    if (!ghf_path.empty()) {
        if (!extract_bnk_file_by_relpath(ghf_path, out.ghf_bytes_compressed, err)) {
            out.error = ".ghf load failed: " + err;
            return false;
        }
        if (!gunzip(out.ghf_bytes_compressed, out.ghf_bytes_raw, err)) {
            out.error = ".ghf gunzip failed: " + err;
            return false;
        }
    }

    /* .hdb / .genv intentionally not loaded yet — first milestone is
       just the heightfield mesh + texture mapping, which lives in
       .ehf + .ghf.  The other two files become relevant when we add
       prop placement and per-cell lighting respectively. */

    out.ok = true;
    return true;
}

bool DecodeGhfHeights(const std::vector<uint8_t>& bytes, GhfHeights& out)
{
    out = {};
    if (bytes.size() < 0x14) {
        out.error = ".ghf too small to hold header";
        return false;
    }

    out.tile_size = be_f32(bytes.data() + 0x00);
    /* offset 0x04..0x0B is zero padding in our sample; skip it. */
    out.width  = be_u32(bytes.data() + 0x0C);
    out.height = be_u32(bytes.data() + 0x10);

    if (out.width == 0 || out.height == 0 ||
        out.width > 8192 || out.height > 8192) {
        out.error = ".ghf dimensions implausible";
        return false;
    }

    constexpr size_t kCellStride = 14;
    const size_t cells = static_cast<size_t>(out.width)
                       * static_cast<size_t>(out.height);
    const size_t need  = 0x14 + cells * kCellStride;
    if (bytes.size() < need) {
        out.error = ".ghf truncated — header says " +
                    std::to_string(out.width) + "x" +
                    std::to_string(out.height) +
                    " but body is too short";
        return false;
    }

    out.heights.resize(cells);
    out.min_height =  std::numeric_limits<float>::infinity();
    out.max_height = -std::numeric_limits<float>::infinity();

    const uint8_t* p = bytes.data() + 0x14;
    for (size_t i = 0; i < cells; ++i) {
        const float h = be_f32(p + i * kCellStride);
        out.heights[i] = h;
        if (h < out.min_height) out.min_height = h;
        if (h > out.max_height) out.max_height = h;
    }

    out.ok = true;
    return true;
}

bool BuildTerrainMesh(const GhfHeights& hg, TerrainMesh& out)
{
    out = {};
    if (!hg.ok || hg.width < 2 || hg.height < 2 ||
        hg.heights.size() != size_t(hg.width) * size_t(hg.height)) {
        return false;
    }

    const uint32_t W = hg.width;
    const uint32_t H = hg.height;
    const float    tile = hg.tile_size > 0.f ? hg.tile_size : 1.f;
    const size_t   N    = size_t(W) * size_t(H);
    const size_t   tris = size_t(W - 1) * size_t(H - 1) * 2;

    out.width  = W;
    out.height = H;
    out.min_height = hg.min_height;
    out.max_height = hg.max_height;

    out.positions.resize(N * 3);
    out.normals.resize  (N * 3);
    out.uvs.resize      (N * 2);
    out.indices.resize  (tris * 3);

    /* Centre the terrain at (0, 0, 0) so the model preview's auto-
       framing picks a sensible camera distance.  X spans columns,
       Z spans rows, Y is the height (up). */
    const float cx = (float(W) - 1.f) * 0.5f * tile;
    const float cz = (float(H) - 1.f) * 0.5f * tile;

    /* Pass 1 — positions and texcoords.

       UVs cover [0, 1] across the whole heightmap so the baked
       terrain-albedo BC1 page from `.ehf` (which is sized to ~1
       texel per heightfield cell) lands each cell on its own
       pixel.  Earlier we had to tile the texture_atlas N times
       to get any visible detail because the atlas is a *source*
       material page, not the baked terrain — but now that
       Level::DecodeEhfTerrainAlbedo gives us the actual baked
       albedo, the trivial mapping is what we want.                */
    /* UVs are scaled to WORLD-SPACE / texture-repeat-size so detail
       textures bound with a REPEAT sampler tile naturally and the
       GPU can pick the right mip level based on screen-space
       derivatives.  This replaces the old trivial [0,1] mapping
       (which stretched a single composite across the whole map).

       0.125 repeats per world unit ≈ 8 world units per texture
       repeat — matches the EhfPalette tile_scale=0.125 default
       observed in chapter3.  Callers that want a 1:1 composite-
       sized lookup can divide back out in their shader.            */
    constexpr float kUvRepeatsPerWu = 0.125f;
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            const size_t i = size_t(y) * W + x;
            out.positions[i * 3 + 0] = float(x) * tile - cx;
            out.positions[i * 3 + 1] = hg.heights[i];
            out.positions[i * 3 + 2] = float(y) * tile - cz;
            out.uvs[i * 2 + 0]       = float(x) * tile * kUvRepeatsPerWu;
            out.uvs[i * 2 + 1]       = float(y) * tile * kUvRepeatsPerWu;
        }
    }

    /* Pass 2 — normals from finite-differenced height gradient.
       For an interior vertex, ∂h/∂x ≈ (h[x+1,y] - h[x-1,y]) / (2·tile)
       and ∂h/∂z ≈ (h[x,y+1] - h[x,y-1]) / (2·tile).  Normal of the
       tangent plane is (-∂h/∂x, 1, -∂h/∂z), normalized.  Edges are
       handled with one-sided differences. */
    auto h_at = [&](int xi, int yi) -> float {
        if (xi < 0) xi = 0; else if (xi >= int(W)) xi = int(W) - 1;
        if (yi < 0) yi = 0; else if (yi >= int(H)) yi = int(H) - 1;
        return hg.heights[size_t(yi) * W + size_t(xi)];
    };
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            const float hl = h_at(int(x) - 1, int(y));
            const float hr = h_at(int(x) + 1, int(y));
            const float hd = h_at(int(x),     int(y) - 1);
            const float hu = h_at(int(x),     int(y) + 1);
            float nx = (hl - hr);
            float ny = 2.f * tile;
            float nz = (hd - hu);
            float len = std::sqrt(nx*nx + ny*ny + nz*nz);
            if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }
            else             { nx = 0.f;  ny = 1.f;  nz = 0.f;  }
            const size_t i = size_t(y) * W + x;
            out.normals[i * 3 + 0] = nx;
            out.normals[i * 3 + 1] = ny;
            out.normals[i * 3 + 2] = nz;
        }
    }

    /* Pass 3 — triangle indices.  Two tris per quad, CCW when
       viewed from +Y (so the up-facing terrain faces the camera). */
    size_t k = 0;
    for (uint32_t y = 0; y + 1 < H; ++y) {
        for (uint32_t x = 0; x + 1 < W; ++x) {
            const uint32_t i00 = uint32_t(size_t(y    ) * W + (x    ));
            const uint32_t i10 = uint32_t(size_t(y    ) * W + (x + 1));
            const uint32_t i01 = uint32_t(size_t(y + 1) * W + (x    ));
            const uint32_t i11 = uint32_t(size_t(y + 1) * W + (x + 1));

            out.indices[k++] = i00;
            out.indices[k++] = i01;
            out.indices[k++] = i10;

            out.indices[k++] = i10;
            out.indices[k++] = i01;
            out.indices[k++] = i11;
        }
    }

    out.ok = true;
    return true;
}

}  
