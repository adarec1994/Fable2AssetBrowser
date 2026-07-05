#include "LevelLoader.h"
#include "HeightfieldLoader.h"
#include "TextureAtlasDecoder.h"
#include "EhfPalette.h"
#include "EhfChunkParser.h"
#include "TerrainTextureRegistry.h"
#include "VfsConfig.h"
#include "GdbModelHashlist.h"
#include "GdbParser.h"
#include "../Havok/HavokPackfileReader.h"
#include "../ISO/IsoMount.h"

#include "../Utilities/State.h"
#include "../Utilities/Utils.h"
#include "../Utilities/Progress.h"
#include "../BNKCore.cpp"
#include "../UI/OutputLog.h"
#include "../textures/TexParser.h"
#include "../textures/LhTexCodec.h"
#include "../textures/export/TextureExport.h"
#include <zlib.h>

#include <vector>
#include <cstdint>
extern bool decode_tex_to_rgba(const std::vector<unsigned char>& blob,
                               std::vector<uint8_t>& rgba,
                               int& out_w, int& out_h,
                               bool* out_has_alpha,
                               int mip_index = -1);
extern const std::string& mp_last_decode_fail_reason();
extern const std::string& mp_last_decode_info();

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>
#include <iomanip>
#include <iterator>
#include <climits>
#include <limits>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

std::atomic<bool>   g_pending_terrain_load{false};
std::atomic<bool>   g_level_export_only_load{false};
Level::TerrainMesh  g_pending_terrain_mesh;
std::string         g_pending_terrain_label;
FlatAssetEntry      g_pending_terrain_level_entry;
std::vector<uint8_t> g_pending_terrain_ehf_bytes;
std::vector<Level::PendingAdjacentTerrain> g_pending_adjacent_terrain_meshes;

std::vector<uint8_t>  g_pending_terrain_ghf_payload;
std::vector<float>    g_pending_terrain_ghf_heights;
float                 g_pending_terrain_ghf_tile_size = 1.f;
int                   g_pending_terrain_ghf_width = 0;
int                   g_pending_terrain_ghf_height = 0;
FlatAssetEntry        g_pending_terrain_ghf_entry;
std::vector<Level::PropBlock> g_pending_level_prop_blocks;
std::string                   g_pending_level_model_body_bnk;
Level::WaterScene             g_pending_level_water_scene;
bool                          g_pending_level_water_present = false;
Gdb::WaterTheme               g_pending_level_water_theme;
Gdb::SkyTheme                 g_pending_level_sky_theme;
Gdb::CloudTheme               g_pending_level_cloud_theme;
Gdb::EnvironmentThemeTimeline g_pending_level_environment_timeline;
std::vector<std::string>           g_level_vfs_texture_body_bnks;
std::vector<std::string>           g_level_vfs_model_bnks;
std::vector<std::string>           g_level_vfs_streaming_bnks;
std::vector<HavokCollisionMesh>    g_level_havok_collision;
std::vector<GdbWorldPlacement>     g_level_gdb_placements;
static std::atomic<bool>      g_level_async_loading{false};

namespace Level {

bool IsAsyncLoadInProgress()
{
    return g_level_async_loading.load() ||
           g_pending_terrain_load.load() ||
           S.show_progress.load();
}

void OpenAsync(const FlatAssetEntry& entry)
{
    bool expected = false;
    if (!g_level_async_loading.compare_exchange_strong(expected, true)) {
        OutputLog::warn("level load already in progress");
        return;
    }

    S.cancel_requested.store(false);

    progress_open(100, "Loading level...");
    std::thread([entry]() {
        progress_update(5, 100, "Extracting level...");
        const bool ok = Open(entry);
        const bool cancelled = S.cancel_requested.load();
        if (cancelled) {
            g_pending_terrain_load.store(false);
            g_pending_level_prop_blocks.clear();
            g_pending_adjacent_terrain_meshes.clear();
            g_pending_terrain_mesh = Level::TerrainMesh{};
            g_pending_terrain_ehf_bytes.clear();
            g_pending_terrain_ghf_heights.clear();
            g_pending_terrain_ghf_payload.clear();
            g_pending_terrain_ghf_width = 0;
            g_pending_terrain_ghf_height = 0;
            g_pending_terrain_ghf_tile_size = 1.0f;
            g_pending_terrain_ghf_entry = FlatAssetEntry{};
            g_pending_terrain_level_entry = FlatAssetEntry{};
            g_pending_terrain_label.clear();
            g_pending_level_model_body_bnk.clear();
            g_pending_level_water_present = false;
            g_pending_level_water_scene = Level::WaterScene{};
            g_pending_level_water_theme = Gdb::WaterTheme{};
            g_pending_level_sky_theme = Gdb::SkyTheme{};
            g_pending_level_cloud_theme = Gdb::CloudTheme{};
            g_pending_level_environment_timeline =
                Gdb::EnvironmentThemeTimeline{};
            g_level_havok_collision.clear();
            g_level_gdb_placements.clear();
            g_level_vfs_texture_body_bnks.clear();
            g_level_vfs_model_bnks.clear();
            g_level_vfs_streaming_bnks.clear();
            progress_done();
            OutputLog::warn("Level load cancelled.");
            S.cancel_requested.store(false);
        } else if (!ok) {
            progress_done();
        } else {
            progress_update(70, 100, "Preparing render...");
            if (!g_pending_terrain_load.load()) {
                progress_done();
            }
        }
        g_level_async_loading.store(false);
    }).detach();
}

namespace {

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

uint32_t read_be_u32_raw(const uint8_t* p)
{
    return (uint32_t(p[0]) << 24) |
           (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8)  |
            uint32_t(p[3]);
}

float read_be_f32_raw(const uint8_t* p)
{
    uint32_t u = read_be_u32_raw(p);
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

bool skip_ehf_tex_blob(BeReader& r)
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
        // 16-bit paint/splat mask: header(mt) + width*height*2 (dims @0x10/0x14).
        // (The old raw_size@(mt) reading was pixel data, not a size — byte-exact
        // verified across all HeightFieldGraphicsFile in streaming.bnk.)
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

bool build_ehf_render_strip_mesh(const std::vector<uint8_t>& ehf,
                                 TerrainMesh& out,
                                 std::string* out_stats = nullptr)
{
    out = {};
    if (out_stats) out_stats->clear();

    static constexpr char kMagic[] = "HeightFieldGraphicsFile";
    static constexpr size_t kMagicLen = sizeof(kMagic) - 1;
    static constexpr size_t kHeaderLen = 63;
    if (ehf.size() < kHeaderLen ||
        std::memcmp(ehf.data(), kMagic, kMagicLen) != 0) {
        return false;
    }

    const uint32_t body_off = read_be_u32_raw(ehf.data() + 55);
    const uint32_t body_size = read_be_u32_raw(ehf.data() + 59);
    if (uint64_t(body_off) + uint64_t(body_size) > ehf.size()) {
        return false;
    }

    BeReader r;
    r.p = ehf.data() + body_off;
    r.n = body_size;
    r.i = 0;

    if (!skip_ehf_tex_blob(r) || !skip_ehf_tex_blob(r)) return false;

    float max_height_hint = 0.0f;
    uint32_t render_tile_count = 0;
    if (!r.f32(max_height_hint) || !r.u32(render_tile_count)) return false;
    if (render_tile_count == 0 || render_tile_count > 4096) return false;

    struct Sample {
        float y = -std::numeric_limits<float>::infinity();
        uint32_t count = 0;
    };
    std::unordered_map<uint64_t, Sample> samples;
    samples.reserve(size_t(render_tile_count) * 128);
    std::vector<int32_t> qxs;
    std::vector<int32_t> qzs;
    uint64_t cell_count_total = 0;
    uint64_t vertex_sample_total = 0;

    auto sane_coord = [](float v) {
        return std::isfinite(v) && std::fabs(v) < 100000.0f;
    };
    auto quant = [](float v) -> int32_t {
        return int32_t(std::lround(v * 2.0f));
    };
    auto make_key = [](int32_t qx, int32_t qz) -> uint64_t {
        return (uint64_t(uint32_t(qx)) << 32) | uint32_t(qz);
    };
    auto add_sample = [&](float x, float z, float y) {
        const int32_t qx = quant(x);
        const int32_t qz = quant(z);
        const uint64_t key = make_key(qx, qz);
        auto [it, inserted] = samples.try_emplace(key);
        if (inserted) {
            qxs.push_back(qx);
            qzs.push_back(qz);
        }



        Sample& s = it->second;
        s.y = (s.count == 0) ? y : std::max(s.y, y);
        ++s.count;
        ++vertex_sample_total;
    };

    for (uint32_t ti = 0; ti < render_tile_count; ++ti) {
        uint32_t sample_w = 0, sample_h = 0, cell_w = 0, cell_h = 0;
        if (!r.u32(sample_w) || !r.u32(sample_h) ||
            !r.u32(cell_w)  || !r.u32(cell_h)) {
            return false;
        }
        if (sample_w == 0 || sample_h == 0 ||
            cell_w == 0 || cell_h == 0 ||
            cell_w > 256 || cell_h > 256) {
            return false;
        }

        const uint64_t cell_count =
            uint64_t(cell_w) * uint64_t(cell_h);
        if (cell_count > 65536 ||
            uint64_t(r.i) + cell_count * 160ull + 24ull >
                uint64_t(r.n)) {
            return false;
        }

        const size_t cell_base = r.i;
        cell_count_total += cell_count;
        for (uint64_t ci = 0; ci < cell_count; ++ci) {
            const uint8_t* cell =
                r.p + cell_base + size_t(ci) * 160u;
            for (int vi = 0; vi < 8; ++vi) {
                const uint8_t* src = cell + 64 + size_t(vi) * 12u;
                const float x = read_be_f32_raw(src + 0);
                const float z = read_be_f32_raw(src + 4);
                const float y = read_be_f32_raw(src + 8);
                if (!sane_coord(x) || !sane_coord(y) || !sane_coord(z)) {
                    continue;
                }
                add_sample(x, z, y);
            }
        }

        r.i += size_t(cell_count) * 160u + 24u;
    }

    if (samples.size() < 4) {
        return false;
    }

    std::sort(qxs.begin(), qxs.end());
    qxs.erase(std::unique(qxs.begin(), qxs.end()), qxs.end());
    std::sort(qzs.begin(), qzs.end());
    qzs.erase(std::unique(qzs.begin(), qzs.end()), qzs.end());
    if (qxs.size() < 2 || qzs.size() < 2 ||
        qxs.size() > 2048 || qzs.size() > 2048) {
        return false;
    }

    const uint32_t W = uint32_t(qxs.size());
    const uint32_t H = uint32_t(qzs.size());
    const size_t N = size_t(W) * size_t(H);
    std::vector<float> heights(N, 0.0f);
    std::vector<uint8_t> has_height(N, 0);

    auto grid_index = [W](uint32_t x, uint32_t z) {
        return size_t(z) * size_t(W) + size_t(x);
    };
    for (uint32_t z = 0; z < H; ++z) {
        for (uint32_t x = 0; x < W; ++x) {
            const auto it = samples.find(make_key(qxs[x], qzs[z]));
            if (it == samples.end() || it->second.count == 0) continue;
            const size_t gi = grid_index(x, z);
            heights[gi] = it->second.y;
            has_height[gi] = 1;
        }
    }

    uint32_t filled_holes = 0;
    for (uint32_t z = 0; z < H; ++z) {
        for (uint32_t x = 0; x < W; ++x) {
            const size_t gi = grid_index(x, z);
            if (has_height[gi]) continue;

            float best_dist = std::numeric_limits<float>::infinity();
            float best_h = 0.0f;
            bool found = false;
            for (uint32_t zz = 0; zz < H; ++zz) {
                for (uint32_t xx = 0; xx < W; ++xx) {
                    const size_t oi = grid_index(xx, zz);
                    if (!has_height[oi]) continue;
                    const float dx = float(qxs[xx] - qxs[x]);
                    const float dz = float(qzs[zz] - qzs[z]);
                    const float d2 = dx * dx + dz * dz;
                    if (d2 < best_dist) {
                        best_dist = d2;
                        best_h = heights[oi];
                        found = true;
                    }
                }
            }
            if (!found) return false;
            heights[gi] = best_h;
            has_height[gi] = 1;
            ++filled_holes;
        }
    }

    out.width = W;
    out.height = H;
    out.positions.resize(N * 3);
    out.normals.assign(N * 3, 0.0f);
    out.uvs.resize(N * 2);
    out.min_height = std::numeric_limits<float>::infinity();
    out.max_height = -std::numeric_limits<float>::infinity();

    float tex_min_x = float(qxs.front()) * 0.5f;
    float tex_max_x = float(qxs.back()) * 0.5f;
    float tex_min_z = float(qzs.front()) * 0.5f;
    float tex_max_z = float(qzs.back()) * 0.5f;
    EhfParsedBody parsed_body;
    if (ParseEhfBody(ehf, parsed_body) && !parsed_body.chunks.empty()) {
        float chunk_min_x = std::numeric_limits<float>::infinity();
        float chunk_min_z = std::numeric_limits<float>::infinity();
        float chunk_max_x = -std::numeric_limits<float>::infinity();
        float chunk_max_z = -std::numeric_limits<float>::infinity();
        for (const auto& c : parsed_body.chunks) {
            if (!std::isfinite(c.origin[0]) ||
                !std::isfinite(c.origin[1]) ||
                !std::isfinite(c.extent[0]) ||
                !std::isfinite(c.extent[1])) {
                continue;
            }
            chunk_min_x = std::min(chunk_min_x, c.origin[0]);
            chunk_min_z = std::min(chunk_min_z, c.origin[1]);
            chunk_max_x = std::max(chunk_max_x, c.extent[0]);
            chunk_max_z = std::max(chunk_max_z, c.extent[1]);
        }
        if (std::isfinite(chunk_min_x) && std::isfinite(chunk_min_z) &&
            std::isfinite(chunk_max_x) && std::isfinite(chunk_max_z) &&
            chunk_max_x > chunk_min_x && chunk_max_z > chunk_min_z) {
            tex_min_x = chunk_min_x;
            tex_min_z = chunk_min_z;
            tex_max_x = chunk_max_x;
            tex_max_z = chunk_max_z;
        }
    }
    const float span_x = std::max(tex_max_x - tex_min_x, 1e-6f);
    const float span_z = std::max(tex_max_z - tex_min_z, 1e-6f);

    for (uint32_t z = 0; z < H; ++z) {
        for (uint32_t x = 0; x < W; ++x) {
            const size_t gi = grid_index(x, z);
            const float wx = float(qxs[x]) * 0.5f;
            const float wz = float(qzs[z]) * 0.5f;
            const float wy = heights[gi];
            out.positions[gi * 3 + 0] = wx;
            out.positions[gi * 3 + 1] = wy;
            out.positions[gi * 3 + 2] = wz;
            out.uvs[gi * 2 + 0] = (wx - tex_min_x) / span_x;
            out.uvs[gi * 2 + 1] = (wz - tex_min_z) / span_z;
            out.min_height = std::min(out.min_height, wy);
            out.max_height = std::max(out.max_height, wy);
        }
    }

    auto height_at = [&](int x, int z) {
        x = std::clamp(x, 0, int(W) - 1);
        z = std::clamp(z, 0, int(H) - 1);
        return heights[grid_index(uint32_t(x), uint32_t(z))];
    };
    auto world_x_at = [&](int x) {
        x = std::clamp(x, 0, int(W) - 1);
        return float(qxs[uint32_t(x)]) * 0.5f;
    };
    auto world_z_at = [&](int z) {
        z = std::clamp(z, 0, int(H) - 1);
        return float(qzs[uint32_t(z)]) * 0.5f;
    };
    for (uint32_t z = 0; z < H; ++z) {
        for (uint32_t x = 0; x < W; ++x) {
            const float dx =
                std::max(world_x_at(int(x) + 1) - world_x_at(int(x) - 1),
                         1e-6f);
            const float dz =
                std::max(world_z_at(int(z) + 1) - world_z_at(int(z) - 1),
                         1e-6f);
            float nx = -(height_at(int(x) + 1, int(z)) -
                         height_at(int(x) - 1, int(z))) / dx;
            float ny = 1.0f;
            float nz = -(height_at(int(x), int(z) + 1) -
                         height_at(int(x), int(z) - 1)) / dz;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-6f) {
                nx /= len;
                ny /= len;
                nz /= len;
            }
            const size_t gi = grid_index(x, z);
            out.normals[gi * 3 + 0] = nx;
            out.normals[gi * 3 + 1] = ny;
            out.normals[gi * 3 + 2] = nz;
        }
    }

    out.indices.reserve(size_t(W - 1) * size_t(H - 1) * 6);
    for (uint32_t z = 0; z + 1 < H; ++z) {
        for (uint32_t x = 0; x + 1 < W; ++x) {
            const uint32_t i00 = uint32_t(grid_index(x, z));
            const uint32_t i10 = uint32_t(grid_index(x + 1, z));
            const uint32_t i01 = uint32_t(grid_index(x, z + 1));
            const uint32_t i11 = uint32_t(grid_index(x + 1, z + 1));
            out.indices.push_back(i00);
            out.indices.push_back(i01);
            out.indices.push_back(i10);
            out.indices.push_back(i10);
            out.indices.push_back(i01);
            out.indices.push_back(i11);
        }
    }

    out.ok = !out.indices.empty();
    if (out_stats) {
        std::ostringstream ss;
        ss << render_tile_count << " render tile(s), "
           << cell_count_total << " strip cell(s), "
           << vertex_sample_total << " vertex sample(s), grid "
           << W << "x" << H
           << (filled_holes ? ", filled " + std::to_string(filled_holes)
                            : std::string{})
           << ", " << (out.indices.size() / 3) << " tri(s)";
        *out_stats = ss.str();
    }
    return out.ok;
}

struct StreamingModelCandidate {
    std::string hint_path;
    std::string resolved_path;
    std::string key;
    std::string path_key;
    std::string hint_lower;
    std::string resolved_lower;
    std::string display_name;
    const FlatAssetEntry* entry = nullptr;
    bool from_gmd = false;
    std::string gmd_bnk_path;
    int gmd_file_index = -1;
};

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Mat3f {
    float m[9] = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f,
    };
};

struct Xform3f {
    Mat3f r;
    Vec3f t;
};

struct GmdLayoutChild {
    std::string raw_path;
    std::string asset_key;
    std::string resolved_path;
    std::string resolved_key;
    Xform3f local;
    size_t offset = 0;
};

void mat3_mul(const float a[9], const float b[9], float out[9])
{
    float r[9] = {};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            r[row * 3 + col] =
                a[row * 3 + 0] * b[0 * 3 + col] +
                a[row * 3 + 1] * b[1 * 3 + col] +
                a[row * 3 + 2] * b[2 * 3 + col];
        }
    }
    for (int i = 0; i < 9; ++i) {
        out[i] = r[i];
    }
}

Vec3f vec3_add(const Vec3f& a, const Vec3f& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3f vec3_sub(const Vec3f& a, const Vec3f& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

float vec3_len2(const Vec3f& v)
{
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

Vec3f mat3_apply(const Mat3f& a, const Vec3f& v)
{
    return {
        a.m[0] * v.x + a.m[1] * v.y + a.m[2] * v.z,
        a.m[3] * v.x + a.m[4] * v.y + a.m[5] * v.z,
        a.m[6] * v.x + a.m[7] * v.y + a.m[8] * v.z,
    };
}

Mat3f mat3_mul3(const Mat3f& a, const Mat3f& b)
{
    Mat3f out;
    mat3_mul(a.m, b.m, out.m);
    return out;
}

bool mat3_inverse(const Mat3f& a, Mat3f& out)
{
    const float* m = a.m;
    const float c00 =  m[4] * m[8] - m[5] * m[7];
    const float c01 = -m[3] * m[8] + m[5] * m[6];
    const float c02 =  m[3] * m[7] - m[4] * m[6];
    const float c10 = -m[1] * m[8] + m[2] * m[7];
    const float c11 =  m[0] * m[8] - m[2] * m[6];
    const float c12 = -m[0] * m[7] + m[1] * m[6];
    const float c20 =  m[1] * m[5] - m[2] * m[4];
    const float c21 = -m[0] * m[5] + m[2] * m[3];
    const float c22 =  m[0] * m[4] - m[1] * m[3];
    const float det = m[0] * c00 + m[1] * c01 + m[2] * c02;
    if (!std::isfinite(det) || std::fabs(det) < 1e-8f) {
        return false;
    }
    const float inv_det = 1.0f / det;
    out.m[0] = c00 * inv_det;
    out.m[1] = c10 * inv_det;
    out.m[2] = c20 * inv_det;
    out.m[3] = c01 * inv_det;
    out.m[4] = c11 * inv_det;
    out.m[5] = c21 * inv_det;
    out.m[6] = c02 * inv_det;
    out.m[7] = c12 * inv_det;
    out.m[8] = c22 * inv_det;
    return true;
}

Xform3f xform_compose(const Xform3f& a, const Xform3f& b)
{
    Xform3f out;
    out.r = mat3_mul3(a.r, b.r);
    out.t = vec3_add(a.t, mat3_apply(a.r, b.t));
    return out;
}

bool xform_inverse(const Xform3f& a, Xform3f& out)
{
    if (!mat3_inverse(a.r, out.r)) return false;
    out.t = mat3_apply(out.r, {-a.t.x, -a.t.y, -a.t.z});
    return true;
}

Vec3f xform_apply_point(const Xform3f& a, const Vec3f& p)
{
    return vec3_add(a.t, mat3_apply(a.r, p));
}

Mat3f mat3_from_quat(float qx, float qy, float qz, float qw)
{
    const float len =
        std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    Mat3f out;
    if (!std::isfinite(len) || len < 1e-6f) {
        return out;
    }
    qx /= len;
    qy /= len;
    qz /= len;
    qw /= len;

    const float xx = qx * qx;
    const float yy = qy * qy;
    const float zz = qz * qz;
    const float xy = qx * qy;
    const float xz = qx * qz;
    const float yz = qy * qz;
    const float wx = qw * qx;
    const float wy = qw * qy;
    const float wz = qw * qz;

    out.m[0] = 1.0f - 2.0f * (yy + zz);
    out.m[1] = 2.0f * (xy - wz);
    out.m[2] = 2.0f * (xz + wy);
    out.m[3] = 2.0f * (xy + wz);
    out.m[4] = 1.0f - 2.0f * (xx + zz);
    out.m[5] = 2.0f * (yz - wx);
    out.m[6] = 2.0f * (xz - wy);
    out.m[7] = 2.0f * (yz + wx);
    out.m[8] = 1.0f - 2.0f * (xx + yy);
    return out;
}

Vec3f game_vec_to_xform_axes(float x, float y, float z)
{
    return {x, z, y};
}

Mat3f game_mat_to_xform_axes(const Mat3f& game)
{
    Mat3f out;
    const int axis_map[3] = {0, 2, 1};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            out.m[row * 3 + col] =
                game.m[axis_map[row] * 3 + axis_map[col]];
        }
    }
    return out;
}

Xform3f prop_instance_xform(const Level::PropInstance& inst)
{
    Xform3f out;
    out.t = game_vec_to_xform_axes(
        inst.values[0], inst.values[1], inst.values[2]);
    if (inst.has_full_transform) {
        float scale = inst.values[12];
        if (!std::isfinite(scale) || scale == 0.0f) scale = 1.0f;
        for (int i = 0; i < 9; ++i) {
            out.r.m[i] = inst.values[3 + i] * scale;
        }
        return out;
    }

    const float s  = inst.values[6];
    const float c  = inst.values[7];
    const float sx = inst.values[9]  == 0.0f ? 1.0f : inst.values[9];
    const float sy = inst.values[10] == 0.0f ? sx   : inst.values[10];
    const float sz = inst.values[11] == 0.0f ? sx   : inst.values[11];
    out.r.m[0] = c * sx;
    out.r.m[1] = 0.0f;
    out.r.m[2] = s * sy;
    out.r.m[3] = 0.0f;
    out.r.m[4] = sz;
    out.r.m[5] = 0.0f;
    out.r.m[6] = -s * sx;
    out.r.m[7] = 0.0f;
    out.r.m[8] = c * sy;
    return out;
}

Level::PropInstance prop_instance_from_xform(const Xform3f& xf,
                                             uint32_t hash = 0)
{
    Level::PropInstance pi;
    pi.hash = hash;
    pi.values[0] = xf.t.x;
    pi.values[1] = xf.t.z;
    pi.values[2] = xf.t.y;
    for (int i = 0; i < 9; ++i) {
        pi.values[3 + i] = xf.r.m[i];
    }
    pi.values[12] = 1.0f;
    pi.has_full_transform = true;
    return pi;
}

bool is_gdb_pi_pair_yaw_rotation(float ry, float rz)
{
    constexpr float kPi = 3.14159265358979323846f;
    return std::isfinite(ry) && std::isfinite(rz) &&
           std::fabs(std::fabs(ry) - kPi) < 1e-4f &&
           std::fabs(std::fabs(rz) - kPi) < 1e-4f;
}

void fill_gdb_rotation_matrix(Level::PropInstance& pi,
                              float rx,
                              float ry,
                              float rz,
                              float scale)
{
    if (!std::isfinite(rx)) rx = 0.0f;
    if (!std::isfinite(ry)) ry = 0.0f;
    if (!std::isfinite(rz)) rz = 0.0f;
    if (!std::isfinite(scale) || scale <= 0.01f || scale >= 100.0f) {
        scale = 1.0f;
    }

    const float sx = std::sin(rx);
    const float cx = std::cos(rx);
    const float sy = std::sin(ry);
    const float cy = std::cos(ry);
    const float sz = std::sin(rz);
    const float cz = std::cos(rz);



    float game[9] = {};
    game[0] = cy * cx;
    game[1] = sx;
    game[2] = -sy * cx;
    game[3] = sy * sz - cy * cz * sx;
    game[4] = cz * cx;
    game[5] = sy * cz * sx + cy * sz;
    game[6] = cy * sz * sx + sy * cz;
    game[7] = -sz * cx;
    game[8] = cy * cz - sy * sz * sx;

    const int axis_map[3] = {0, 2, 1};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            pi.values[3 + row * 3 + col] =
                game[axis_map[row] * 3 + axis_map[col]];
        }
    }
    pi.values[12] = scale;
    pi.has_full_transform = true;
}

const StreamingModelCandidate*
choose_streaming_model_for_gdb(const std::string& entity_name,
                               const std::vector<StreamingModelCandidate>& candidates,
                               int* out_score = nullptr,
                               uint32_t parent_hash = 0);
std::vector<StreamingModelCandidate>
collect_streaming_model_candidates(const std::vector<std::string>& streaming_bnks);

std::string lower_slash(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

uint32_t fnv1_model_path_hash(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    std::replace(s.begin(), s.end(), '/', '\\');

    uint32_t h = 0x811C9DC5u;
    for (unsigned char c : s) {
        h *= 0x01000193u;
        h ^= uint32_t(c);
    }
    return h;
}

std::string strip_model_suffixes(std::string s)
{
    auto strip = [](std::string& v, const char* suffix) {
        const size_t n = std::strlen(suffix);
        if (v.size() >= n && v.compare(v.size() - n, n, suffix) == 0) {
            v.resize(v.size() - n);
        }
    };
    strip(s, ".gmd");
    strip(s, ".mdl");
    return s;
}

std::string compact_match_key(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (std::isalnum(c)) {
            out.push_back(char(std::tolower(c)));
        }
    }
    return out;
}

std::string model_name_from_path(const std::string& path)
{
    std::string p = path;
    std::replace(p.begin(), p.end(), '\\', '/');
    p = strip_model_suffixes(p);
    const size_t slash = p.find_last_of('/');
    return (slash == std::string::npos) ? p : p.substr(slash + 1);
}

bool read_be_f32_at(const std::vector<uint8_t>& bytes,
                    size_t off,
                    float& out)
{
    if (off + 4 > bytes.size()) return false;
    const uint32_t u =
        (uint32_t(bytes[off + 0]) << 24) |
        (uint32_t(bytes[off + 1]) << 16) |
        (uint32_t(bytes[off + 2]) << 8) |
         uint32_t(bytes[off + 3]);
    std::memcpy(&out, &u, sizeof(out));
    return std::isfinite(out);
}

std::string gmd_asset_key_from_raw_path(const std::string& raw_path)
{
    std::string p = lower_slash(raw_path);
    const std::string marker = "layout.instance.";
    if (const size_t pos = p.find(marker); pos != std::string::npos) {
        p.erase(0, pos + marker.size());
    }
    const size_t art = p.find("art/");
    if (art != std::string::npos) {
        p = p.substr(art);
    }
    const size_t slash = p.find_last_of('/');
    std::string name = (slash == std::string::npos) ? p : p.substr(slash + 1);
    auto strip_suffix = [](std::string& s, const char* suffix) {
        const size_t n = std::strlen(suffix);
        if (s.size() >= n && s.compare(s.size() - n, n, suffix) == 0) {
            s.resize(s.size() - n);
        }
    };
    strip_suffix(name, ".emdl");
    strip_suffix(name, ".mdl");
    strip_suffix(name, "_asset");
    strip_suffix(name, "asset");
    return compact_match_key(name);
}

bool parse_gmd_payload_transform(const std::vector<uint8_t>& bytes,
                                 size_t payload_start,
                                 size_t payload_end,
                                 Xform3f& out)
{
    struct Candidate {
        float score = std::numeric_limits<float>::infinity();
        float qx = 0.0f;
        float qy = 0.0f;
        float qz = 0.0f;
        float qw = 1.0f;
        float tx = 0.0f;
        float ty = 0.0f;
        float tz = 0.0f;
    };
    Candidate best;

    for (size_t align = 0; align < 4; ++align) {
        std::vector<float> floats;
        for (size_t off = payload_start + align;
             off + 4 <= payload_end;
             off += 4)
        {
            float f = 0.0f;
            if (!read_be_f32_at(bytes, off, f)) {
                floats.push_back(std::numeric_limits<float>::quiet_NaN());
            } else {
                floats.push_back(f);
            }
        }
        if (floats.size() < 7) continue;

        for (size_t i = 0; i + 7 <= floats.size(); ++i) {
            const float qx = floats[i + 0];
            const float qy = floats[i + 1];
            const float qz = floats[i + 2];
            const float qw = floats[i + 3];
            const float tx = floats[i + 4];
            const float ty = floats[i + 5];
            const float tz = floats[i + 6];
            const float vals[] = {qx, qy, qz, qw, tx, ty, tz};
            bool finite = true;
            for (float v : vals) {
                if (!std::isfinite(v)) {
                    finite = false;
                    break;
                }
            }
            if (!finite) continue;
            if (std::fabs(tx) > 512.0f || std::fabs(ty) > 512.0f ||
                std::fabs(tz) > 512.0f)
            {
                continue;
            }
            const float qmag =
                std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
            if (!std::isfinite(qmag) || qmag < 0.6f || qmag > 1.4f) {
                continue;
            }
            const float pos_mag =
                std::sqrt(tx * tx + ty * ty + tz * tz);
            const float score =
                std::fabs(qmag - 1.0f) * 100.0f +
                (pos_mag < 1e-4f ? 20.0f : 0.0f) +
                float(i) * 0.01f + float(align) * 0.001f;
            if (score < best.score) {
                best = {score, qx, qy, qz, qw, tx, ty, tz};
            }
        }
    }

    if (!std::isfinite(best.score)) {
        return false;
    }
    out.r = game_mat_to_xform_axes(
        mat3_from_quat(best.qx, best.qy, best.qz, best.qw));
    out.t = game_vec_to_xform_axes(best.tx, best.ty, best.tz);
    return true;
}

std::vector<GmdLayoutChild>
parse_gmd_layout_children(const std::vector<uint8_t>& bytes)
{
    std::vector<GmdLayoutChild> out;
    static constexpr const char* kMarkers[] = {
        "Prop.Layout.Instance.",
        "Light.Layout.Instance.",
        "Environment.Layout.Instance.",
    };
    size_t pos = 0;
    while (pos < bytes.size()) {
        auto best_it = bytes.end();
        const char* best_marker = nullptr;
        for (const char* marker : kMarkers) {
            const size_t marker_len = std::strlen(marker);
            if (pos + marker_len >= bytes.size()) continue;
            const auto it = std::search(
                bytes.begin() +
                    static_cast<std::vector<uint8_t>::difference_type>(pos),
                bytes.end(),
                marker,
                marker + marker_len);
            if (it != bytes.end() &&
                (best_it == bytes.end() || it < best_it))
            {
                best_it = it;
                best_marker = marker;
            }
        }
        const auto it = best_it;
        if (it == bytes.end()) break;
        (void)best_marker;
        const size_t start =
            static_cast<size_t>(std::distance(bytes.begin(), it));
        size_t str_end = start;
        while (str_end < bytes.size() && bytes[str_end] != 0) {
            ++str_end;
        }
        if (str_end >= bytes.size()) break;

        std::string raw(reinterpret_cast<const char*>(&bytes[start]),
                        str_end - start);
        size_t payload_start = str_end + 1;
        size_t payload_end = std::min(bytes.size(), payload_start + 160);
        for (size_t s = payload_start; s + 4 <= payload_end; ++s) {
            if (bytes[s + 0] == 0xff && bytes[s + 1] == 0xff &&
                bytes[s + 2] == 0xff && bytes[s + 3] == 0xff)
            {
                payload_end = s;
                break;
            }
        }

        GmdLayoutChild child;
        child.raw_path = raw;
        child.asset_key = gmd_asset_key_from_raw_path(raw);
        child.offset = start;
        if (!child.asset_key.empty() &&
            parse_gmd_payload_transform(
                bytes, payload_start, payload_end, child.local))
        {
            out.push_back(std::move(child));
        }
        pos = str_end + 1;
    }
    return out;
}

bool is_gdb_authored_level_shell_model(
    const std::string& model_path,
    const std::unordered_set<std::string>& authored_level_model_paths)
{
    const std::string p = lower_slash(model_path);
    if (authored_level_model_paths.find(p) ==
        authored_level_model_paths.end())
    {
        return false;
    }

    return p.find("/buildings/") != std::string::npos ||
           p.find("/structures/") != std::string::npos;
}

bool is_gdb_shell_audit_model(const std::string& model_path)
{
    const std::string p = lower_slash(model_path);
    return p.find("/buildings/") != std::string::npos ||
           p.find("/structures/") != std::string::npos;
}

bool is_gdb_static_prop_reject_model(const std::string& model_path)
{
    const std::string p = lower_slash(model_path);
    return p.find("art/characters/") == 0 ||
           p.find("/art/characters/") != std::string::npos ||
           p.find("/characters/heros/") != std::string::npos;
}

bool is_gdb_unique_entity_shell_model(const std::string& model_path)
{
    const std::string p = lower_slash(model_path);
    if (p.find("/buildings/") == std::string::npos &&
        p.find("/structures/") == std::string::npos)
    {
        return false;
    }

    return p.find("/exterior.mdl") != std::string::npos ||
           p.find("/interior.mdl") != std::string::npos ||
           p.find("bs_market_gatehouse") != std::string::npos ||
           p.find("bs_market_clocktower") != std::string::npos ||
           p.find("bs_market_platform") != std::string::npos ||
           p.find("bs_market_largeshop") != std::string::npos ||
           p.find("bs_market_smallshop") != std::string::npos ||
           p.find("bs_market_generalshop") != std::string::npos ||
           p.find("bs_market_tavern") != std::string::npos ||
           p.find("bs_market_tarotstall") != std::string::npos ||
           p.find("bs_townhouse") != std::string::npos;
}

int bwsmarket_shell_instance_limit(const std::string& model_path)
{
    const std::string p = lower_slash(model_path);
    if (p.find("bs_market_tarotstall/") != std::string::npos &&
        p.find("bs_market_tarotstall_doors") == std::string::npos)
    {
        return 1;
    }
    const bool shell =
        p.find("/exterior.mdl") != std::string::npos ||
        p.find("/interior.mdl") != std::string::npos;
    if (!shell) return -1;
    if (p.find("bs_market_tavern/") != std::string::npos) {
        return 1;
    }
    if (p.find("bs_market_generalshop/") != std::string::npos) {
        return 3;
    }
    return -1;
}

bool is_lowpoly_house_proxy_model(const std::string& model_path)
{
    const std::string p = lower_slash(model_path);
    return p.find("/structures/dotxsi/bs_market_lowpoly_house") !=
           std::string::npos;
}

bool is_market_bridge_facade_proxy_model(const std::string& model_path)
{
    const std::string p = lower_slash(model_path);
    return p.find("bs_market_bridge_facade") != std::string::npos;
}

bool is_bwsmarket_clocktower_authored_model(const std::string& model_path)
{
    const std::string p = lower_slash(model_path);
    return p.find("bs_market_clocktower/bs_market_clocktower.mdl") !=
               std::string::npos ||
           p.find("bs_market_clocktower_cogs/") != std::string::npos ||
           p.find("bs_market_clocktower_hourhand/") != std::string::npos ||
           p.find("bs_market_clocktower_minutehand/") != std::string::npos;
}

bool compact_key_is_or_numbered(const std::string& key, const char* base)
{
    const size_t n = std::strlen(base);
    if (key == base) return true;
    if (key.size() <= n || key.compare(0, n, base) != 0) {
        return false;
    }
    for (size_t i = n; i < key.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(key[i]))) {
            return false;
        }
    }
    return true;
}

bool compact_key_is_or_variant(const std::string& key, const char* base)
{
    if (compact_key_is_or_numbered(key, base)) return true;
    const size_t n = std::strlen(base);
    if (key.size() <= n + 1 || key.compare(0, n, base) != 0 ||
        key[n] != 'v')
    {
        return false;
    }
    for (size_t i = n + 1; i < key.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(key[i]))) {
            return false;
        }
    }
    return true;
}

bool is_bad_market_helper_substitution(const std::string& entity_key,
                                       const std::string& raw_key,
                                       const std::string& model_path)
{
    const std::string model_key = compact_match_key(model_path);

    const bool sign_entity =
        entity_key.find("sign") != std::string::npos ||
        raw_key.find("sign") != std::string::npos;
    const bool door_entity =
        entity_key.find("door") != std::string::npos ||
        raw_key.find("door") != std::string::npos;

    const bool general_store_building =
        compact_key_is_or_numbered(entity_key, "generalstore") ||
        compact_key_is_or_numbered(raw_key, "objectbuildinggeneralstore") ||
        compact_key_is_or_numbered(raw_key, "newobjectbuildinggeneralstore");
    if (model_key.find("bsmarketgeneralshop") != std::string::npos &&
        (model_key.find("exterior") != std::string::npos ||
         model_key.find("interior") != std::string::npos) &&
        !general_store_building)
    {
        return true;
    }
    if (general_store_building && !sign_entity &&
        model_key.find("signgeneralstore") != std::string::npos)
    {
        return true;
    }

    const bool tavern_building =
        entity_key.find("bsmarkettavern") != std::string::npos ||
        entity_key.find("markettavern") != std::string::npos ||
        raw_key.find("objectbuildingbsmarkettavern") != std::string::npos ||
        raw_key.find("newobjectbuildingbsmarkettavern") != std::string::npos;
    if (model_key.find("bsmarkettavern") != std::string::npos &&
        !tavern_building)
    {
        return true;
    }
    if (tavern_building && !sign_entity &&
        (model_key.find("botavernsign") != std::string::npos ||
         model_key.find("tavernsign") != std::string::npos))
    {
        return true;
    }

    const bool tarot_stall_building =
        entity_key.find("tarotstall") != std::string::npos ||
        raw_key.find("tarotstall") != std::string::npos;
    if (tarot_stall_building && !door_entity &&
        model_key.find("tarotstalldoors") != std::string::npos)
    {
        return true;
    }

    const bool market_stall_building =
        compact_key_is_or_variant(entity_key, "bsopenstall") ||
        compact_key_is_or_variant(entity_key, "bsmarketopenstall") ||
        compact_key_is_or_variant(entity_key, "bsmarketstall") ||
        raw_key.find("objectbuildingbsopenstall") != std::string::npos ||
        raw_key.find("objectbuildingbsmarketstall") != std::string::npos ||
        raw_key.find("newobjectbuildingbsmarketstall") != std::string::npos;
    const bool bs_market_stall_shell =
        (model_key.find("bsmarketmarketstall") != std::string::npos ||
         model_key.find("bsmarketopenstall") != std::string::npos ||
         model_key.find("bsopenstall") != std::string::npos) &&
        model_key.find("esashopmarketstall") == std::string::npos;
    if (bs_market_stall_shell && !market_stall_building) {
        return true;
    }
    if ((general_store_building || tavern_building ||
         market_stall_building || tarot_stall_building) &&
        (model_key.find("bstownhouse") != std::string::npos ||
         model_key.find("townhouse") != std::string::npos))
    {
        return true;
    }

    return false;
}

bool is_unindexed_shell_fallback_entity(const std::string& entity_key,
                                        const std::string& raw_key)
{
    const std::string text = entity_key + " " + raw_key;
    auto has = [&](const char* needle) {
        return text.find(needle) != std::string::npos;
    };

    if (has("canopy") || has("counter") || has("stairsfloor") ||
        has("door") || has("sign"))
    {
        return false;
    }

    return has("objectbuilding") ||
           has("newobjectbuilding") ||
           has("bsmarkettavern") ||
           has("generalstore") ||
           has("generalshop") ||
           has("largeshop") ||
           has("smallshop") ||
           has("townhouse") ||
           has("slumstreethouse") ||
           has("gatehouse") ||
           has("clocktower");
}

std::string hex_u32(uint32_t v)
{
    std::ostringstream os;
    os << "0x" << std::uppercase << std::hex
       << std::setw(8) << std::setfill('0') << v;
    return os.str();
}

std::string gdb_shell_sample_text(
    const Gdb::Placement& p,
    const std::string& model_path)
{
    std::ostringstream os;
    os << (p.entity_name.empty() ? "<unnamed>" : p.entity_name)
       << " parent=" << hex_u32(p.parent_hash);
    if (p.model_path_hash != 0) {
        os << " modelHash=" << hex_u32(p.model_path_hash);
    }
    os << " pos=(" << p.x << ", " << p.y << ", " << p.z << ")"
       << " model=" << model_path;
    return os.str();
}

std::string gdb_instance_key(
    const Gdb::Placement& p,
    const std::string& model_path)
{
    auto q = [](float v) -> long long {
        if (!std::isfinite(v)) return 0;
        return static_cast<long long>(std::llround(v * 100.0f));
    };
    std::ostringstream os;
    os << lower_slash(model_path) << '|'
       << std::hex << p.hash_a << '|' << p.parent_hash << std::dec << '|'
       << p.entity_name << '|'
       << q(p.x) << ',' << q(p.y) << ',' << q(p.z);
    return os.str();
}

std::string prop_instance_transform_key(
    const Level::PropInstance& inst,
    const std::string& model_path)
{
    auto q = [](float v) -> long long {
        if (!std::isfinite(v)) return 0;
        return static_cast<long long>(std::llround(v * 100.0f));
    };
    std::ostringstream os;
    os << lower_slash(model_path);
    for (int i = 0; i < 12; ++i) {
        os << '|' << q(inst.values[i]);
    }
    return os.str();
}

std::string companion_interior_path(const std::string& model_path)
{
    std::string p = lower_slash(model_path);
    const std::string suffix = "/exterior.mdl";
    if (p.size() < suffix.size() ||
        p.compare(p.size() - suffix.size(), suffix.size(), suffix) != 0)
    {
        return {};
    }
    p.replace(p.size() - suffix.size(), suffix.size(), "/interior.mdl");
    return p;
}

std::string companion_exterior_path(const std::string& model_path)
{
    std::string p = lower_slash(model_path);
    const std::string suffix = "/interior.mdl";
    if (p.size() < suffix.size() ||
        p.compare(p.size() - suffix.size(), suffix.size(), suffix) != 0)
    {
        return {};
    }
    p.replace(p.size() - suffix.size(), suffix.size(), "/exterior.mdl");
    return p;
}

std::string house_facade_companion_exterior_path(const std::string& model_path)
{
    std::string p = lower_slash(model_path);
    struct Map {
        const char* facade;
        const char* shell;
    };
    static const Map maps[] = {
        { "bs_townhouse_basic_facade_mid", "bs_townhouse_basic" },
        { "bs_townhouse_basic_facade",     "bs_townhouse_basic" },
        { "bs_townhouse_basic_facade_snow_v2", "bs_townhouse_basic_snow_v2" },
        { "bs_townhouse_v1_facade_mid",    "bs_townhouse_v1" },
        { "bs_townhouse_v1_facade",        "bs_townhouse_v1" },
        { "bs_townhouse_v1_facade_snow",   "bs_townhouse_v1_snow" },
        { "bs_townhouse_v2_facade_mid",    "bs_townhouse_v2" },
        { "bs_townhouse_v2_facade",        "bs_townhouse_v2" },
        { "bs_townhouse_v2_facade_snow",   "bs_townhouse_v2_snow" },
        { "bs_townhouse_v3_facade_snow",   "bs_townhouse_v3_snow" },
        { "bs_townhouse_v1_snow",           "bs_townhouse_v1_snow" },
        { "bs_townhouse_v2_snow",           "bs_townhouse_v2_snow" },
        { "bs_townhouse_v3_snow",           "bs_townhouse_v3_snow" },
    };
    for (const Map& map : maps) {
        const std::string needle =
            std::string("/buildings/dotxsi/") + map.facade + "/" +
            map.facade + ".mdl";
        const size_t pos = p.find(needle);
        if (pos == std::string::npos) continue;

        const std::string exterior =
            std::string("/buildings/dotxsi/") + map.shell + "/" +
            map.shell + "/exterior.mdl";
        p.replace(pos, needle.size(), exterior);
        return p;
    }
    return {};
}

std::string shop_facade_companion_exterior_path(const std::string& model_path)
{
    (void)model_path;
    return {};
}

std::string gdb_representative_name(const std::vector<std::string>& examples)
{
    if (examples.empty()) return {};
    std::string s = examples.front();
    const size_t us = s.find_last_of('_');
    if (us != std::string::npos && us + 1 < s.size()) {
        bool digits = true;
        for (size_t i = us + 1; i < s.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
                digits = false;
                break;
            }
        }
        if (digits) s.resize(us);
    }
    return s;
}

std::string gdb_entity_key(std::string s)
{
    static const char* prefixes[] = {
        "NewObjectBuilding", "ObjectBuilding",
        "NewObjectFurniture", "ObjectFurniture",
        "NewObjectStatic", "ObjectStatic",
        "NewObject", "Object",
        "New"
    };
    for (const char* pfx : prefixes) {
        const size_t n = std::strlen(pfx);
        if (s.size() > n && s.compare(0, n, pfx) == 0) {
            s = s.substr(n);
            break;
        }
    }
    return compact_match_key(s);
}

bool is_gdb_landmark_name(const std::string& entity_name)
{
    const std::string key = gdb_entity_key(entity_name);
    if (key.empty()) return false;
    const char* needles[] = {
        "bridge",
        "clocktower",
        "grandfatherclock",
        "wallclock",
        "dockarch",
        "gatehouse",
        "lockgate",
        "walltower",
        "wallgate",
        "archway",
        "guardpost",
        "marketstairs",
        "scaffoldingstairs",
        "castlearch",
        "dockswall",
        "oilamp",
        "oillantern",
        "statue",
    };
    for (const char* needle : needles) {
        if (key.find(needle) != std::string::npos) return true;
    }
    return false;
}

bool bytes_contain_be_u32(const std::vector<uint8_t>& bytes, uint32_t value)
{
    const uint8_t a = uint8_t(value >> 24);
    const uint8_t b = uint8_t(value >> 16);
    const uint8_t c = uint8_t(value >> 8);
    const uint8_t d = uint8_t(value);
    for (size_t i = 0; i + 4 <= bytes.size(); ++i) {
        if (bytes[i] == a && bytes[i + 1] == b &&
            bytes[i + 2] == c && bytes[i + 3] == d) {
            return true;
        }
    }
    return false;
}

std::string hex32_for_log(uint32_t value)
{
    std::ostringstream os;
    os << "0x" << std::hex << std::uppercase
       << std::setw(8) << std::setfill('0') << value;
    return os.str();
}

void log_curated_hashlist_miss(const std::string& entity_name,
                               uint32_t parent_hash,
                               const char* target_model_path)
{
    static std::mutex logged_mutex;
    static std::unordered_set<std::string> logged_keys;

    std::string key = hex32_for_log(parent_hash) + "|" +
                      gdb_entity_key(entity_name) + "|" +
                      (target_model_path ? target_model_path : "");
    {
        std::lock_guard<std::mutex> lock(logged_mutex);
        if (!logged_keys.insert(key).second) return;
    }

    OutputLog::warn(
        "GDB hashlist: curated model target missing in streaming candidates; "
        "parent=" + hex32_for_log(parent_hash) +
        " entity='" + entity_name +
        "' target='" + (target_model_path ? target_model_path : "") + "'");
}

std::string resolve_streaming_bnk_path(const std::string& vfs_stream_path)
{
    std::string wanted_leaf =
        std::filesystem::path(vfs_stream_path).filename().string();
    std::transform(wanted_leaf.begin(), wanted_leaf.end(),
                   wanted_leaf.begin(), ::tolower);

    auto leaf_matches = [&](const std::string& mounted_leaf_lower) {
        if (mounted_leaf_lower == wanted_leaf) return true;
        if (mounted_leaf_lower.size() <= wanted_leaf.size() + 1) return false;
        const size_t off = mounted_leaf_lower.size() - wanted_leaf.size();
        if (mounted_leaf_lower.compare(off, wanted_leaf.size(),
                                       wanted_leaf) != 0) return false;
        return mounted_leaf_lower[off - 1] == '_';
    };

    if (auto resolved = find_bnk_by_virtual_path(vfs_stream_path)) {
        return *resolved;
    }
    for (const auto& p : S.bnk_paths) {
        std::string leaf = std::filesystem::path(p).filename().string();
        std::transform(leaf.begin(), leaf.end(), leaf.begin(), ::tolower);
        if (leaf_matches(leaf)) return p;
    }
    for (const auto& p : S.nested_bnk_paths) {
        std::string leaf = std::filesystem::path(p).filename().string();
        std::transform(leaf.begin(), leaf.end(), leaf.begin(), ::tolower);
        if (leaf_matches(leaf)) return p;
    }
    return {};
}

std::vector<StreamingModelCandidate>
collect_streaming_model_candidates(const std::vector<std::string>& streaming_bnks)
{
    std::unordered_map<std::string, const FlatAssetEntry*> mdl_by_path;
    mdl_by_path.reserve(S.all_mdl_files.size());
    std::unordered_map<std::string, std::vector<const FlatAssetEntry*>> mdl_by_key;
    mdl_by_key.reserve(S.all_mdl_files.size());
    for (const auto& e : S.all_mdl_files) {
        mdl_by_path.emplace(lower_slash(e.full_path), &e);
        mdl_by_key[compact_match_key(model_name_from_path(e.full_path))]
            .push_back(&e);
    }
    auto choose_global_model = [&](const std::string& hint_path) {
        const std::string hint_lower = lower_slash(hint_path);
        if (auto exact = mdl_by_path.find(hint_lower); exact != mdl_by_path.end()) {
            return exact->second;
        }
        for (const auto& kv : mdl_by_path) {
            const std::string& model_path = kv.first;
            if (model_path.size() >= hint_lower.size() &&
                model_path.compare(model_path.size() - hint_lower.size(),
                                   hint_lower.size(),
                                   hint_lower) == 0) {
                return kv.second;
            }
        }

        const std::string hint_key =
            compact_match_key(model_name_from_path(hint_path));
        if (hint_key.empty()) return static_cast<const FlatAssetEntry*>(nullptr);

        auto choose_best = [](const std::vector<const FlatAssetEntry*>& hits) {
            const FlatAssetEntry* best = nullptr;
            int best_score = INT_MIN;
            for (const FlatAssetEntry* e : hits) {
                if (!e) continue;
                int score = 0;
                const std::string lower = lower_slash(e->full_path);
                if (lower.find("/globals_models.bnk") == std::string::npos) {
                    score += 500;
                }
                if (e->from_nested) score += 250;
                score -= int(std::min<size_t>(e->full_path.size(), 240));
                if (!best || score > best_score) {
                    best = e;
                    best_score = score;
                }
            }
            return best;
        };

        if (auto it = mdl_by_key.find(hint_key); it != mdl_by_key.end()) {
            return choose_best(it->second);
        }

        std::vector<const FlatAssetEntry*> fuzzy;
        for (const auto& kv : mdl_by_key) {
            const std::string& model_key = kv.first;
            if (model_key.size() < 5) continue;
            const bool related =
                model_key.find(hint_key) != std::string::npos ||
                hint_key.find(model_key) != std::string::npos;
            if (!related) continue;
            fuzzy.insert(fuzzy.end(), kv.second.begin(), kv.second.end());
        }
        return choose_best(fuzzy);
    };

    std::vector<StreamingModelCandidate> out;
    std::unordered_set<std::string> seen;
    for (const auto& vfs_path : streaming_bnks) {
        const std::string mounted = resolve_streaming_bnk_path(vfs_path);
        if (mounted.empty()) continue;
        try {
            BnkCache::Entry& bnk = BnkCache::get(mounted);
            const auto& files = bnk.reader->list_files();
            auto add_candidate = [&](std::string hint,
                                      bool from_gmd,
                                      int gmd_index) {
                std::string norm = lower_slash(hint);
                auto [seen_it, inserted] = seen.insert(norm);
                if (!inserted) {
                    if (from_gmd) {
                        for (auto& existing : out) {
                            if (lower_slash(existing.hint_path) == norm) {
                                existing.from_gmd = true;
                                existing.gmd_bnk_path = mounted;
                                existing.gmd_file_index = gmd_index;
                                break;
                            }
                        }
                    }
                    return;
                }

                StreamingModelCandidate c;
                c.hint_path = std::move(hint);
                c.hint_lower = norm;
                c.display_name = model_name_from_path(c.hint_path);
                c.key = compact_match_key(c.display_name);
                c.from_gmd = from_gmd;
                if (from_gmd) {
                    c.gmd_bnk_path = mounted;
                    c.gmd_file_index = gmd_index;
                }
                c.entry = choose_global_model(c.hint_path);
                if (c.entry) {
                    c.resolved_path = c.entry->full_path;
                }
                c.resolved_lower = lower_slash(c.resolved_path);
                c.path_key =
                    compact_match_key(c.hint_path + " " + c.resolved_path);
                out.push_back(std::move(c));
            };

            for (size_t file_i = 0; file_i < files.size(); ++file_i) {
                const auto& f = files[file_i];
                std::string lower = lower_slash(f.name);
                if (lower.size() >= 8 &&
                    lower.compare(lower.size() - 8, 8, ".mdl.gmd") == 0) {
                    std::string mdl = f.name;
                    mdl.resize(mdl.size() - 4);
                    add_candidate(std::move(mdl), true, int(file_i));
                    continue;
                }
                if (lower.size() >= 4 &&
                    lower.compare(lower.size() - 4, 4, ".hkx") == 0) {
                    std::string mdl = f.name;
                    mdl.resize(mdl.size() - 4);
                    mdl += ".mdl";
                    add_candidate(std::move(mdl), false, -1);
                }
            }
        } catch (...) {
        }
    }
    return out;
}

int streaming_model_score(const std::string& entity_name,
                          const StreamingModelCandidate& c)
{
    const std::string entity_key = gdb_entity_key(entity_name);
    if (entity_key.empty() || c.key.empty()) return INT_MIN;

    auto has = [&](const char* needle) {
        return entity_key.find(needle) != std::string::npos;
    };
    auto cand_has = [&](const char* needle) {
        return c.key.find(needle) != std::string::npos;
    };
    const std::string& path_key = c.path_key;
    auto cand_path_has = [&](const char* needle) {
        return path_key.find(needle) != std::string::npos;
    };
    auto key_is_or_numbered = [&](const char* base) {
        const size_t n = std::strlen(base);
        if (entity_key == base) return true;
        if (entity_key.size() <= n ||
            entity_key.compare(0, n, base) != 0)
        {
            return false;
        }
        for (size_t i = n; i < entity_key.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(entity_key[i]))) {
                return false;
            }
        }
        return true;
    };
    auto key_is_or_variant = [&](const char* base) {
        if (key_is_or_numbered(base)) return true;
        const size_t n = std::strlen(base);
        if (entity_key.size() <= n + 1 ||
            entity_key.compare(0, n, base) != 0 ||
            entity_key[n] != 'v')
        {
            return false;
        }
        for (size_t i = n + 1; i < entity_key.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(entity_key[i]))) {
                return false;
            }
        }
        return true;
    };

    if (key_is_or_numbered("generalstore") &&
        (cand_has("signgeneralstore") || cand_path_has("signgeneralstore")))
    {
        return INT_MIN;
    }

    int score = INT_MIN;

    const bool bare_general_store = key_is_or_numbered("generalstore");
    const bool market_tavern_shell =
        key_is_or_numbered("bsmarkettavern") ||
        key_is_or_numbered("markettavern");
    const bool market_openstall_shell =
        key_is_or_variant("bsopenstall") ||
        key_is_or_variant("bsmarketopenstall");
    const bool market_stall_shell =
        key_is_or_variant("bsmarketstall") ||
        key_is_or_variant("marketstall") ||
        key_is_or_numbered("bstarotstall") ||
        key_is_or_numbered("tarotstall");

    auto path_has_any = [&](std::initializer_list<const char*> needles) {
        for (const char* needle : needles) {
            if (cand_path_has(needle)) return true;
        }
        return false;
    };
    if ((bare_general_store || market_tavern_shell ||
         market_openstall_shell || market_stall_shell) &&
        path_has_any({"bstownhouse", "townhouse"}))
    {
        return INT_MIN;
    }
    auto same_variant_bonus = [&](int base_score) {
        int adjusted = base_score;
        for (const char* v :
             {"v1", "v2", "v3", "v4", "v5", "v6"})
        {
            if (entity_key.find(v) != std::string::npos &&
                path_key.find(v) != std::string::npos)
            {
                adjusted += 500;
            }
        }
        return adjusted;
    };

    if (bare_general_store) {
        return INT_MIN;
    }
    if (market_tavern_shell) {
        return INT_MIN;
    }
    if (market_openstall_shell) {
        if (cand_path_has("esashopmarketstall") ||
            cand_path_has("signstall"))
        {
            return INT_MIN;
        }
        if (cand_path_has("bsopenstall") ||
            cand_path_has("bsmarketopenstall") ||
            cand_path_has("openstall"))
        {
            score = std::max(score, same_variant_bonus(18500));
        }
    }
    if (market_stall_shell) {
        if (cand_path_has("esashopmarketstall") ||
            cand_path_has("signstall"))
        {
            return INT_MIN;
        }
        if (entity_key.find("tarotstall") != std::string::npos &&
            cand_path_has("tarotstall"))
        {
            score = std::max(score, same_variant_bonus(19000));
        } else if ((cand_path_has("bsmarketstall") ||
                    cand_path_has("marketstall")) &&
                   !cand_path_has("esashopmarketstall"))
        {
            score = std::max(score, same_variant_bonus(18500));
        }
    }

    if (entity_key == c.key) {
        score = 12000;
    } else if (c.key.find(entity_key) != std::string::npos) {
        score = 9000 + int(entity_key.size());
    } else if (entity_key.find(c.key) != std::string::npos) {
        score = 7000 + int(c.key.size());
    }

    struct Alias { const char* entity; const char* model; int score; };
    static const Alias aliases[] = {
        { "smallwallpost",         "stonewallmediumpostspiked",     15000 },
        { "wallpost",              "stonewallmediumpostspiked",     14500 },
        { "smallwallstraight",     "stonewallmediumstraightspiked", 15000 },
        { "smallwallcurved",       "stonewallmediumcurvedspiked",   15000 },
        { "smallwallcorner",       "stonewallmediumcurvedspiked",   14500 },
        { "smallwallbroken",       "stonewallmediumbrokenspiked",   15000 },
        { "shelflong",             "esashelflong",                  15000 },
        { "woodenbucket",          "esabucketwooden",               15000 },
        { "lightsceiling",         "bslightceiling",                15000 },
        { "lightfixingceiling",    "bslightceiling",                15000 },
        { "candleholder",          "bscandleholder",                14000 },
        { "grainsack",             "esasackgrain",                  14500 },
        { "shippingcrate",         "esashippingcrate",              14500 },
        { "weaponrackwallmulti",   "esashopweaponswallrackmulti",   15000 },
        { "weaponrackwallsingle",  "esashopweaponswallracksingle",  15000 },
        { "weaponrack",            "esashopweaponrack",             13500 },
        { "booksgroup",            "esabooksblock",                 13000 },
        { "pubtable",              "esatabletavern",                14500 },
        { "largesquareultradecorative","esaftableultradecorative",  13800 },
        { "largesquareupgradeable","esaftabledecorative",           12500 },
        { "standardultradecorative","esaftableultradecorative",     13600 },
        { "standardupgradeable",   "esaftabledecorative",           12300 },
        { "bookcaseultradecorative","esafbookcaseultradecorative",  15000 },
        { "bookcaseworn",          "esafbookcaseworn",              14500 },
        { "dresserupgradeable",    "esafdresserultradecorative",    13000 },
        { "kitchensinkupgradeable", "esakitchensink",               12000 },
        { "buildingsalesign",      "buildingsalesign",              14000 },
        { "bsmarketbridge",        "bsmarketbridge",                16000 },
        { "marketbridge",          "bsmarketbridge",                15800 },
        { "bridge",                "bsmarketbridge",                12000 },
        { "bsmarketclocktower",    "bsmarketclocktower",            16000 },
        { "marketclocktower",      "bsmarketclocktower",            15800 },
        { "clocktower",            "bsmarketclocktower",            14500 },
        { "grandfatherclock",      "bsgrandfatherclock",            15500 },
        { "wallclock",             "bswallclock",                   15500 },
        { "bsmarketdockarch",      "bsmarketdocksarch",             15000 },
        { "dockarch",              "bsmarketdocksarch",             14500 },
        { "bsmarketarchway",       "bsmarketarchway",               15000 },
        { "archway",               "bsmarketarchway",               13000 },
        { "bsmarketgatehouse",     "bsmarketgatehouse",             15000 },
        { "bsgatehouse",           "bsmarketgatehouse",             14500 },
        { "bsmarketlockgate",      "bsmarketlockgates",             15000 },
        { "lockgate",              "bsmarketlockgates",             14000 },
        { "bsmarketwalltower",     "bsmarketwalltower",             15000 },
        { "walltower",             "bsmarketwalltower",             13500 },
        { "bsmarketwallgate",      "bsmarketwallgate",              15000 },
        { "closedgate",            "bsmarketwallgate",              13000 },
        { "guardpost",             "bsmarketguardpost",             14500 },
        { "marketstairs",          "bsmarketstairs",                14500 },
        { "generalstorestairsfloor","bsmarketgeneralshopstairsfloor",14500 },
        { "generalshopstairsfloor", "bsmarketgeneralshopstairsfloor",14500 },
        { "bsopenstall",           "openstall",                     14500 },
        { "openstall",             "openstall",                     14000 },
        { "bsmarketstall",         "bsmarketstall",                 14500 },
        { "marketstall",           "marketstall",                   14000 },
        { "tarotstall",            "tarotstall",                    15000 },
        { "scaffoldingstairs",     "bsmarketscaffoldingstairs",     14500 },
        { "scaffoldstairs",        "bsmarketscaffoldingstairs",     14500 },
        { "scaffoldstraight",      "bsmarketscaffoldingstraight",   14000 },
        { "marketwalljoiner",      "bsmarketwallbuffer",            13500 },
        { "walljoiner",            "bsmarketwallbuffer",            13000 },
        { "castlearch",            "bsmarketcastlearch",            14500 },
        { "dockswall",             "bsmarketdockswall",             14500 },
        { "dockwall",              "bsmarketdockswall",             14500 },
        { "bsdockwall",            "bsmarketdockswall",             14500 },
        { "slumswall",             "bsslumsthinwallv1",             14000 },
        { "slumsthinwall",         "bsslumsthinwallv1",             14500 },
        { "windowsmallarched",     "esasmarchedwin",                14000 },
        { "smallarchedwin",        "esasmarchedwin",                14000 },
        { "smarchedwin",           "esasmarchedwin",                14000 },
        { "marketdocksjetty",      "bsmarketdocksjetty",            14000 },
        { "docksjetty",            "bsmarketdocksjetty",            13500 },
        { "docksplatform",         "bsmarketdocksplatform",         13500 },
        { "dockscrane",            "bsmarketdockscrane",            13500 },
        { "oillanternsingle",      "bscemetaryoillampsingle",       13000 },
        { "oillampsingle",         "bscemetaryoillampsingle",       13000 },
        { "statue",                "okstatuedolphinv1",             12000 },
        { "cellarlargeroom",       "cellarlargeroom",               15000 },
        { "cellarsmallroom",       "cellarsmallroom",               15000 },
        { "bsmarkettownhousesmall", "bstownhousebasicfacademid",    13500 },
        { "bwsmarkettownhousesmall","bstownhousebasicfacademid",    13500 },
        { "townhousev1",           "bstownhousev1facademid",        14000 },
        { "townhousev2",           "bstownhousev2facademid",        14000 },
        { "townhousev3",           "bstownhousev3exterior",         14500 },
    };
    for (const auto& a : aliases) {
        if (has(a.entity) && (cand_has(a.model) || cand_path_has(a.model))) {
            score = std::max(score, a.score);
        }
    }

    if (score == INT_MIN) return score;
    if (c.entry) score += 500;
    if (c.from_gmd) score += 150;
    if (has("facademid") && path_key.find("facademid") != std::string::npos) {
        score += 500;
    }
    if (has("facade") && path_key.find("facade") != std::string::npos) {
        score += 150;
    }
    return score - int(std::min<size_t>(c.hint_path.size(), 200));
}

const StreamingModelCandidate*
choose_streaming_model_for_gdb(const std::string& entity_name,
                               const std::vector<StreamingModelCandidate>& candidates,
                               int* out_score,
                               uint32_t parent_hash)
{
    auto path_suffix_matches = [](const std::string& path,
                                  const std::string& target) {
        if (path.empty() || target.empty()) return false;
        if (path == target) return true;
        return path.size() > target.size() &&
               path.compare(path.size() - target.size(),
                            target.size(), target) == 0 &&
               (path[path.size() - target.size() - 1] == '/' ||
                path[path.size() - target.size() - 1] == '\\');
    };

    auto choose_curated_override =
        [&](const char* target_model_path, int* score_out) {
            if (!target_model_path || !*target_model_path) {
                return static_cast<const StreamingModelCandidate*>(nullptr);
            }
            const std::string target_lower = lower_slash(target_model_path);
            const std::string target_key =
                compact_match_key(model_name_from_path(target_model_path));
            const bool generic_shell_target =
                target_key == "exterior" || target_key == "interior";
            const StreamingModelCandidate* best = nullptr;
            int best_score = INT_MIN;
            for (const auto& c : candidates) {
                int score = INT_MIN;
                const std::string& resolved_lower = c.resolved_lower;
                const std::string& hint_lower = c.hint_lower;
                if (resolved_lower == target_lower) {
                    score = std::max(score, 50000);
                } else if (path_suffix_matches(resolved_lower, target_lower)) {
                    score = std::max(score, 49250);
                }
                if (hint_lower == target_lower) {
                    score = std::max(score, 49000);
                } else if (path_suffix_matches(hint_lower, target_lower)) {
                    score = std::max(score, 48250);
                }
                if (!target_key.empty() && !generic_shell_target) {
                    if (c.key == target_key) {
                        score = std::max(score, 46000);
                    }
                }
                if (score == INT_MIN) continue;
                if (c.entry) score += 500;
                if (c.from_gmd) score += 150;
                score -= int(std::min<size_t>(c.hint_path.size(), 200));
                if (!best || score > best_score) {
                    best = &c;
                    best_score = score;
                }
            }
            if (score_out) *score_out = best_score;
            return best;
        };

    const std::string entity_key = gdb_entity_key(entity_name);
    const char* curated_model =
        GdbModelHashlist::LookupParentHash(parent_hash);
    if (!curated_model) {
        curated_model = GdbModelHashlist::LookupEntityKey(entity_key);
    }
    if (curated_model && *curated_model) {
        if (const StreamingModelCandidate* curated =
                choose_curated_override(curated_model, out_score)) {
            return curated;
        }
        log_curated_hashlist_miss(entity_name, parent_hash, curated_model);
        if (out_score) *out_score = INT_MIN;
        return nullptr;
    }

    const StreamingModelCandidate* best = nullptr;
    int best_score = INT_MIN;
    for (const auto& c : candidates) {
        const int score = streaming_model_score(entity_name, c);
        if (!best || score > best_score) {
            best = &c;
            best_score = score;
        }
    }
    if (out_score) *out_score = best_score;
    return (best_score >= 6500) ? best : nullptr;
}

constexpr char kEngineLevelMagic[]  = "LevelGraphicsFile";
constexpr size_t kEngineLevelMagicLen = sizeof(kEngineLevelMagic) - 1;

}

bool ParseEngineLevel(const std::vector<uint8_t>& bytes,
                      EngineLevelInfo&            out)
{
    out = {};
    if (bytes.size() < kEngineLevelMagicLen + 8) {
        out.error = "file too small for header";
        return false;
    }

    BeReader r{bytes.data(), bytes.size(), 0};

    if (std::memcmp(r.p, kEngineLevelMagic, kEngineLevelMagicLen) != 0) {
        out.error = "magic mismatch (expected \"LevelGraphicsFile\")";
        return false;
    }
    if (!r.skip(kEngineLevelMagicLen)) {
        out.error = "truncated reading magic";
        return false;
    }

    if (!r.u32(out.version)) {
        out.error = "truncated reading version";
        return false;
    }
    if (out.version < 11 || out.version > 12) {
        std::ostringstream os;
        os << "unsupported version " << out.version
           << " (engine accepts 11..12)";
        out.error = os.str();
        return false;
    }

    if (!r.u32(out.entry_count)) {
        out.error = "truncated reading entry_count";
        return false;
    }
    if (out.entry_count > (1u << 20)) {
        out.error = "entry_count looks corrupt";
        return false;
    }
    out.entries.reserve(out.entry_count);

    for (uint32_t mi = 0; mi < out.entry_count; ++mi) {
        EngineLevelEntry e;
        e.offset = r.i;

        if (!r.u32(e.type)) {
            std::ostringstream os;
            os << "truncated at entry " << mi << " of " << out.entry_count;
            out.error = os.str();
            out.ok = false;
            return false;
        }

        switch (e.type) {
            case 2: {
                PropBlock block;
                block.offset = e.offset;
                block.type = e.type;
                if (!r.cstr(block.model_path) ||
                    !r.cstr(block.shadow_model_path) ||
                    !r.cstr(block.lod_model_path) ||
                    !r.cstr(block.extra_model_path)) {
                    out.error = "truncated reading type-2 model paths";
                    return false;
                }

                e.str_a = block.model_path;
                e.str_b = block.lod_model_path;

                uint32_t instance_count = 0;
                if (!r.u32(instance_count)) {
                    out.error = "truncated reading type-2 instance count";
                    return false;
                }
                if (instance_count > 100000) {
                    out.error = "type-2 instance count looks corrupt";
                    return false;
                }

                block.instances.reserve(instance_count);
                for (uint32_t pi = 0; pi < instance_count; ++pi) {
                    PropInstance inst;
                    if (!r.u8(inst.flags[0]) ||
                        !r.u8(inst.flags[1]) ||
                        !r.u8(inst.flags[2]) ||
                        !r.u64(inst.hash)) {
                        out.error = "truncated reading type-2 instance header";
                        return false;
                    }
                    for (float& v : inst.values) {
                        if (!r.f32(v)) {
                            out.error = "truncated reading type-2 instance floats";
                            return false;
                        }
                    }
                    block.instances.push_back(inst);
                }

                out.prop_blocks.push_back(std::move(block));
                break;
            }
            case 4:
            case 5:
            case 32: {
                if (!r.cstr(e.str_a)) {
                    out.error = "truncated reading string for type "
                              + std::to_string(e.type);
                    return false;
                }
                if (e.type == 4) {
                    if (!r.skip(8)) {
                        out.error = "truncated reading type-4 tail";
                        return false;
                    }
                }
                break;
            }
            case 21: {
                e.str_b.clear();
                if (!r.cstr(e.str_a)) {
                    out.error = "truncated reading string A for type 21";
                    return false;
                }
                if (!r.cstr(e.str_b)) {
                    out.error = "truncated reading string B for type 21";
                    return false;
                }
                if (!r.skip(8 + 1 + 1)) {
                    out.error = "truncated reading type-21 hash+flags";
                    return false;
                }

                uint32_t loop1_count = 0;
                if (!r.u32(loop1_count)) {
                    out.error = "truncated type-21 ext header";
                    return false;
                }
                if (!r.skip(7 * 4 + 12 + 4 + 24)) {
                    out.error = "truncated type-21 ext header (mid)";
                    return false;
                }
                if (loop1_count > 100000) {
                    out.error = "type-21 loop1 count looks corrupt";
                    return false;
                }

                PropBlock t21_block;
                t21_block.offset = e.offset;
                t21_block.type = e.type;
                t21_block.model_path = e.str_a;
                t21_block.lod_model_path = e.str_b;
                t21_block.instances.reserve(loop1_count);

                if (out.version == 11) {
                    for (uint32_t k = 0; k < loop1_count; ++k) {
                        PropInstance inst;
                        for (int j = 0; j < 4; ++j) {
                            if (!r.f32(inst.values[j])) {
                                out.error = "truncated type-21 v11 loop1 body";
                                return false;
                            }
                        }
                        inst.values[7] = 1.0f;
                        inst.values[9] = inst.values[10] = inst.values[11] = 1.0f;
                        t21_block.instances.push_back(inst);
                    }
                } else {
                    for (uint32_t k = 0; k < loop1_count; ++k) {
                        float pos[3];
                        if (!r.f32(pos[0]) || !r.f32(pos[1]) || !r.f32(pos[2])) {
                            out.error = "truncated type-21 v12 instance vec3";
                            return false;
                        }
                        float qx, qy, qz, qw, scale;
                        if (!r.half(qx) || !r.half(qy) ||
                            !r.half(qz) || !r.half(qw) ||
                            !r.half(scale)) {
                            out.error = "truncated type-21 v12 instance quat/scale";
                            return false;
                        }
                        PropInstance inst;
                        inst.values[0] = pos[0];
                        inst.values[1] = pos[1];
                        inst.values[2] = pos[2];

                        const float num = 2.0f * (qw * qz + qx * qy);
                        const float den = 1.0f - 2.0f * (qy * qy + qz * qz);
                        const float mag = std::sqrt(num * num + den * den);
                        if (mag > 1e-6f) {
                            inst.values[6] = num / mag;
                            inst.values[7] = den / mag;
                        } else {
                            inst.values[6] = 0.0f;
                            inst.values[7] = 1.0f;
                        }

                        const float s = (scale > 0.0f) ? scale : 1.0f;
                        inst.values[9]  = s;
                        inst.values[10] = s;
                        inst.values[11] = s;
                        t21_block.instances.push_back(inst);
                    }
                }

                uint32_t loop2_count = 0;
                if (!r.u32(loop2_count)) {
                    out.error = "truncated type-21 loop2 count";
                    return false;
                }
                if (loop2_count > 100000) {
                    out.error = "type-21 loop2 count looks corrupt";
                    return false;
                }

                size_t loop2_emitted = 0;
                size_t loop2_skipped = 0;
                for (uint32_t k = 0; k < loop2_count; ++k) {
                    float a_val, b_val;
                    float p1[3], p2[3];
                    if (!r.f32(a_val) || !r.f32(b_val) ||
                        !r.f32(p1[0]) || !r.f32(p1[1]) || !r.f32(p1[2]) ||
                        !r.f32(p2[0]) || !r.f32(p2[1]) || !r.f32(p2[2]))
                    {
                        out.error = "truncated type-21 loop2 body";
                        return false;
                    }

                    auto in_bounds = [](float v, float lo, float hi) {
                        return std::isfinite(v) && v >= lo && v <= hi;
                    };
                    const bool plausible =
                        in_bounds(p1[0], -2048.0f, 2048.0f) &&
                        in_bounds(p1[1], -2048.0f, 2048.0f) &&
                        in_bounds(p1[2], -512.0f,   512.0f) &&
                        (std::fabs(p1[0]) + std::fabs(p1[1]) + std::fabs(p1[2]) > 0.5f);
                    if (!plausible) {
                        ++loop2_skipped;
                        continue;
                    }

                    PropInstance inst;
                    inst.values[0] = p1[0];
                    inst.values[1] = p1[1];
                    inst.values[2] = p1[2];
                    const float fxy =
                        std::sqrt(p2[0] * p2[0] + p2[1] * p2[1]);
                    if (std::isfinite(fxy) && fxy > 0.001f && fxy < 100.0f) {
                        inst.values[6] = p2[1] / fxy;
                        inst.values[7] = p2[0] / fxy;
                    } else {
                        inst.values[6] = 0.0f;
                        inst.values[7] = 1.0f;
                    }
                    const float s = (std::isfinite(a_val) &&
                                     a_val > 0.05f && a_val < 100.0f)
                                        ? a_val : 1.0f;
                    inst.values[9]  = s;
                    inst.values[10] = s;
                    inst.values[11] = s;
                    t21_block.instances.push_back(inst);
                    ++loop2_emitted;
                }
                (void)loop2_emitted;
                (void)loop2_skipped;

                if (!t21_block.instances.empty()) {
                    out.prop_blocks.push_back(std::move(t21_block));
                }
                break;
            }
            default: {
                std::ostringstream uos;
                uos << "  unknown entry type 0x"
                    << std::hex << e.type << std::dec
                    << " (" << e.type << ") at offset 0x"
                    << std::hex << e.offset << std::dec
                    << " — last 6 entries:";
                OutputLog::warn(uos.str());
                const size_t n = out.entries.size();
                for (size_t k = (n > 6 ? n - 6 : 0); k < n; ++k) {
                    const auto& pe = out.entries[k];
                    std::ostringstream ros;
                    ros << "    [" << k << "] type=" << pe.type
                        << " @ 0x" << std::hex << pe.offset
                        << "  size=" << std::dec << pe.size;
                    if (!pe.str_a.empty()) ros << "  a=" << pe.str_a;
                    if (!pe.str_b.empty()) ros << "  b=" << pe.str_b;
                    OutputLog::info(ros.str());
                }
                e.size = 0;
                out.entries.push_back(e);
                out.ok = true;
                return true;
            }
        }
        e.size = r.i - e.offset;
        out.entries.push_back(std::move(e));
    }

    out.ok = true;
    return true;
}

static void loader_progress_update(int current,
                                   int total,
                                   const std::string& text)
{
    if (!g_level_export_only_load.load()) {
        progress_update(current, total, text);
    }
}

bool Open(const FlatAssetEntry& entry)
{
    if (!g_level_export_only_load.load()) {
        OutputLog::info("loading level '" + entry.name + "' …");
    }
    loader_progress_update(8, 100, "Extracting " + entry.name);

    auto bail_if_cancelled = [&](const char* where) -> bool {
        if (!S.cancel_requested.load()) return false;
        OutputLog::warn(std::string("level load cancelled at ") + where);
        return true;
    };

    g_pending_level_prop_blocks.clear();
    g_pending_adjacent_terrain_meshes.clear();
    g_pending_level_water_theme = Gdb::WaterTheme{};
    g_pending_level_sky_theme = Gdb::SkyTheme{};
    g_pending_level_cloud_theme = Gdb::CloudTheme{};
    g_pending_level_environment_timeline =
        Gdb::EnvironmentThemeTimeline{};

    if (bail_if_cancelled("entry")) return false;

    std::vector<uint8_t> bytes;
    try {
        bytes = BnkCache::extract_bytes(entry.bnk_path, entry.file_index);
    } catch (const std::exception& ex) {
        OutputLog::error("level extract failed: " + std::string(ex.what()));
        return false;
    } catch (...) {
        OutputLog::error("level extract failed (unknown exception)");
        return false;
    }
    if (bytes.empty()) {
        OutputLog::error("level extract produced 0 bytes");
        return false;
    }
    if (bail_if_cancelled("after-extract")) return false;

    loader_progress_update(18, 100, "Parsing level entries...");
    EngineLevelInfo info;
    if (!ParseEngineLevel(bytes, info)) {
        OutputLog::error("parse failed for '" + entry.name + "': "
                         + info.error);
        return false;
    }
    if (bail_if_cancelled("after-parse")) return false;

    info.source_path = entry.full_path;
    const bool is_bwsmarket_engine_level =
        lower_slash(entry.full_path).find("bwsmarket") != std::string::npos;

    size_t lowpoly_house_proxy_blocks = 0;
    size_t lowpoly_house_proxy_instances = 0;
    size_t authored_clocktower_blocks = 0;
    size_t authored_clocktower_instances = 0;
    info.prop_blocks.erase(
        std::remove_if(
            info.prop_blocks.begin(),
            info.prop_blocks.end(),
            [&](const Level::PropBlock& block) {
                if (is_lowpoly_house_proxy_model(block.model_path)) {
                    ++lowpoly_house_proxy_blocks;
                    lowpoly_house_proxy_instances += block.instances.size();
                    return true;
                }
                if (is_market_bridge_facade_proxy_model(block.model_path)) {
                    return true;
                }
                if (is_bwsmarket_engine_level &&
                    is_bwsmarket_clocktower_authored_model(block.model_path))
                {
                    ++authored_clocktower_blocks;
                    authored_clocktower_instances += block.instances.size();
                    return true;
                }
                return false;
            }),
        info.prop_blocks.end());
    if (lowpoly_house_proxy_blocks > 0) {
        OutputLog::info(
            "level props: culled " +
            std::to_string(lowpoly_house_proxy_instances) +
            " low-poly market house proxy instance(s) across " +
            std::to_string(lowpoly_house_proxy_blocks) +
            " block(s)");
    }
    if (authored_clocktower_blocks > 0) {
        OutputLog::info(
            "level props: culled " +
            std::to_string(authored_clocktower_instances) +
            " authored clocktower part instance(s) across " +
            std::to_string(authored_clocktower_blocks) +
            " block(s)");
    }

    int n_t2 = 0, n_t4 = 0, n_t5 = 0, n_t21 = 0, n_t32 = 0, n_other = 0;
    for (const auto& e : info.entries) {
        switch (e.type) {
            case 2:  ++n_t2;  break;
            case 4:  ++n_t4;  break;
            case 5:  ++n_t5;  break;
            case 21: ++n_t21; break;
            case 32: ++n_t32; break;
            default: ++n_other; break;
        }
    }

    std::ostringstream os;
    os << "level OK  ver=" << info.version
       << "  entries=" << info.entries.size()
       << "/" << info.entry_count
       << "  (t2=" << n_t2
       << " t4=" << n_t4
       << " t5=" << n_t5
       << " t21=" << n_t21
       << " t32=" << n_t32
       << " other=" << n_other << ")";
    OutputLog::success(os.str());

    size_t t2_prop_blocks = 0, t2_prop_instances = 0;
    size_t t21_prop_blocks = 0, t21_prop_instances = 0;
    size_t other_prop_blocks = 0, other_prop_instances = 0;
    for (const auto& b : info.prop_blocks) {
        if (b.type == 2) {
            ++t2_prop_blocks;
            t2_prop_instances += b.instances.size();
        } else if (b.type == 21) {
            ++t21_prop_blocks;
            t21_prop_instances += b.instances.size();
        } else {
            ++other_prop_blocks;
            other_prop_instances += b.instances.size();
        }
    }
    std::ostringstream ps;
    ps << "level prop placements: "
       << "t2=" << t2_prop_blocks << " blocks / " << t2_prop_instances << " instances, "
       << "t21=" << t21_prop_blocks << " blocks / " << t21_prop_instances << " instances";
    if (other_prop_blocks > 0) {
        ps << ", other=" << other_prop_blocks << " blocks / "
           << other_prop_instances << " instances";
    }
    OutputLog::info(ps.str());

    std::unordered_set<std::string> authored_level_model_paths;
    authored_level_model_paths.reserve(info.prop_blocks.size() * 2);
    for (const auto& block : info.prop_blocks) {
        if (!block.model_path.empty()) {
            authored_level_model_paths.insert(lower_slash(block.model_path));
        }
        if (!block.lod_model_path.empty()) {
            authored_level_model_paths.insert(
                lower_slash(block.lod_model_path));
        }
    }

    {
        const std::vector<std::string> wanted = {
            "bridge", "lamp", "lantern", "fence", "bench", "post",
            "archway", "gate", "stall", "shop", "wall"
        };
        std::map<std::string, std::pair<size_t, size_t>> match_counts;
        std::map<std::string, std::vector<std::string>> match_paths;
        for (const auto& pb : info.prop_blocks) {
            std::string p = pb.model_path;
            std::transform(p.begin(), p.end(), p.begin(), ::tolower);
            for (const auto& kw : wanted) {
                if (p.find(kw) != std::string::npos) {
                    match_counts[kw].first += 1;
                    match_counts[kw].second += pb.instances.size();
                    if (match_paths[kw].size() < 3) {
                        match_paths[kw].push_back(pb.model_path);
                    }
                    break;
                }
            }
        }
        OutputLog::info("engine_level keyword scan (bridge/lamp/fence/...):");
        for (const auto& kw : wanted) {
            auto it = match_counts.find(kw);
            if (it == match_counts.end()) {
                OutputLog::warn("  " + kw + ":  NONE in engine_level");
            } else {
                std::ostringstream os;
                os << "  " << kw << ":  " << it->second.first
                   << " blocks / " << it->second.second << " instances";
                OutputLog::success(os.str());
                for (const auto& path : match_paths[kw]) {
                    OutputLog::info("    " + path);
                }
            }
        }
    }

    auto ends_with_ci = [](const std::string& s, const char* suffix) {
        size_t n = std::strlen(suffix);
        if (s.size() < n) return false;
        for (size_t i = 0; i < n; ++i) {
            char a = s[s.size() - n + i];
            char b = suffix[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) return false;
        }
        return true;
    };

    int n_heightfield_refs = 0;
    int n_logged           = 0;
    const int kMaxLog      = 16;

    std::vector<std::string> all_ehf_refs;
    std::vector<std::string> all_water_refs;

    for (const auto& e : info.entries) {
        if (e.str_a.empty()) continue;

        const bool is_heightfield_like =
            ends_with_ci(e.str_a, ".ehf") ||
            ends_with_ci(e.str_a, ".ghf") ||
            ends_with_ci(e.str_a, ".hdb") ||
            ends_with_ci(e.str_a, ".genv") ||
            ends_with_ci(e.str_a, ".ama")  ||
            ends_with_ci(e.str_a, ".amm")  ||
            ends_with_ci(e.str_a, ".amr")  ||
            ends_with_ci(e.str_a, ".water") ||
            (e.str_a.find("heightfield") != std::string::npos) ||
            (e.str_a.find("Heightfield") != std::string::npos);

        if (is_heightfield_like) {
            ++n_heightfield_refs;
            OutputLog::info("  heightfield ref: t" + std::to_string(e.type)
                            + "  " + e.str_a);
            if (ends_with_ci(e.str_a, ".ehf")) {
                all_ehf_refs.push_back(e.str_a);
            } else if (ends_with_ci(e.str_a, ".water")) {
                all_water_refs.push_back(e.str_a);
            }
        } else if (n_logged < kMaxLog) {
            ++n_logged;
            OutputLog::info("  ref: t" + std::to_string(e.type)
                            + "  " + e.str_a
                            + (e.str_b.empty() ? std::string()
                                               : "  | " + e.str_b));
        }
    }

    if (n_heightfield_refs == 0) {
        OutputLog::warn("level references no .ehf/.ghf/heightfield* strings — "
                        "checking sibling .list file for the heightfield "
                        "names instead.");
    }

    auto sibling_with_ext = [&](const std::string& new_ext) {
        std::filesystem::path p = entry.full_path;
        p.replace_extension(new_ext);
        return p.string();
    };

    auto load_text_sibling = [&](const std::string& sibling_full_path,
                                 std::vector<uint8_t>& out_bytes) -> bool
    {
        out_bytes.clear();
        auto normalize_asset_key = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            std::replace(s.begin(), s.end(), '\\', '/');
            return s;
        };
        auto filename_of_key = [](const std::string& s) {
            const size_t p = s.find_last_of("/\\");
            return (p == std::string::npos) ? s : s.substr(p + 1);
        };
        auto try_extract = [&](const std::string& bnk_path,
                               int idx) -> bool
        {
            if (bnk_path.empty() || idx < 0) return false;
            try {
                auto v = BnkCache::extract_bytes(bnk_path, idx);
                if (v.empty()) return false;
                out_bytes.assign(v.begin(), v.end());
                return true;
            } catch (...) {
                return false;
            }
        };
        auto try_bnk_path = [&](const std::string& bnk_path,
                                const std::string& key,
                                const std::string& leaf) -> bool
        {
            int idx = BnkCache::find_index(bnk_path, key);
            if (idx < 0 && !leaf.empty()) {
                idx = BnkCache::find_index(bnk_path, leaf);
            }
            return try_extract(bnk_path, idx);
        };
        auto try_file = [&](const std::filesystem::path& p) -> bool
        {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(p, ec)) return false;
            std::ifstream f(p, std::ios::binary);
            if (!f) return false;
            f.seekg(0, std::ios::end);
            const std::streamoff size = f.tellg();
            if (size <= 0) return false;
            f.seekg(0, std::ios::beg);
            out_bytes.resize(static_cast<size_t>(size));
            f.read(reinterpret_cast<char*>(out_bytes.data()), size);
            if (!f) {
                out_bytes.clear();
                return false;
            }
            return true;
        };

        const std::string key = normalize_asset_key(sibling_full_path);
        const std::string leaf = filename_of_key(key);

        if (try_bnk_path(entry.bnk_path, key, leaf)) {
            return true;
        }

        for (const auto& fe : S.all_heightfield_files) {
            const std::string fe_full =
                normalize_asset_key(fe.full_path.empty()
                    ? fe.name : fe.full_path);
            const std::string fe_name = normalize_asset_key(fe.name);
            const bool match =
                fe_full == key ||
                fe_name == key ||
                (!leaf.empty() &&
                 (filename_of_key(fe_full) == leaf ||
                  filename_of_key(fe_name) == leaf));
            if (!match) continue;
            if (try_extract(fe.bnk_path, fe.file_index)) {
                return true;
            }
        }

        for (const auto& bnk_path : S.bnk_paths) {
            if (bnk_path == entry.bnk_path) continue;
            if (try_bnk_path(bnk_path, key, leaf)) {
                return true;
            }
        }

        if (key.compare(0, 5, "data/") == 0) {
            auto try_iso_file = [&](const std::string& virtual_path) -> bool {
                if (!ISO::IsoMount::instance().is_mounted()) return false;
                std::string vp = virtual_path;
                std::replace(vp.begin(), vp.end(), '\\', '/');
                auto bytes = ISO::IsoMount::instance().read_file(vp);
                if (bytes.empty()) return false;
                out_bytes = std::move(bytes);
                return true;
            };
            if (try_iso_file(key)) {
                return true;
            }

            std::vector<std::filesystem::path> game_roots;
            auto add_game_root = [&](const std::filesystem::path& root) {
                if (root.empty()) return;
                if (ISO::IsoMount::is_iso_path(root.string())) return;
                std::error_code ec;
                const auto abs = std::filesystem::absolute(root, ec);
                const auto candidate = ec ? root : abs;
                for (const auto& existing : game_roots) {
                    if (existing == candidate) return;
                }
                game_roots.push_back(candidate);
            };
            auto add_root_from_path = [&](const std::string& p) {
                if (p.empty()) return;
                if (ISO::IsoMount::is_iso_path(p)) return;
                std::string norm = p;
                std::replace(norm.begin(), norm.end(), '\\', '/');
                std::string low = norm;
                std::transform(low.begin(), low.end(), low.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                const size_t data_pos = low.find("/data/");
                if (data_pos != std::string::npos) {
                    add_game_root(norm.substr(0, data_pos));
                }
            };
            add_game_root(S.root_dir);
            add_root_from_path(entry.bnk_path);
            for (const auto& bnk_path : S.bnk_paths) {
                add_root_from_path(bnk_path);
            }
            for (const auto& bnk_path : S.nested_bnk_paths) {
                add_root_from_path(bnk_path);
            }

            const std::string without_data = key.substr(5);
            for (const auto& root : game_roots) {
                if (try_file(root / std::filesystem::path(key))) {
                    return true;
                }
                if (try_file(root / "data" /
                             std::filesystem::path(without_data))) {
                    return true;
                }
                if (lower_slash(root.string()).size() >= 4 &&
                    lower_slash(root.string()).compare(
                        lower_slash(root.string()).size() - 4, 4,
                        "data") == 0 &&
                    try_file(root / std::filesystem::path(without_data))) {
                    return true;
                }
            }
        }

        if (!leaf.empty()) {
            std::error_code ec;
            const auto cwd = std::filesystem::current_path(ec);
            if (!ec) {
                if (try_file(cwd / "extracted" / leaf)) return true;
                if (try_file(cwd / "cmake-build-debug" / "extracted" / leaf))
                    return true;
                if (try_file(cwd.parent_path() / "cmake-build-debug" /
                             "extracted" / leaf))
                    return true;
            }
        }

        return false;
    };

    LevelResources res;
    {
        std::vector<uint8_t> list_bytes;
        const std::string list_path = sibling_with_ext(".list");
        if (load_text_sibling(list_path, list_bytes)) {
            std::string list_str(reinterpret_cast<const char*>(list_bytes.data()),
                                 list_bytes.size());
            std::ostringstream ls; ls << "list (" << list_bytes.size() << " bytes):";
            OutputLog::info(ls.str());

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
                auto matches = [&](const char* ext) {
                    size_t n = std::strlen(ext);
                    return low.size() >= n &&
                           low.compare(low.size() - n, n, ext) == 0;
                };
                if      (matches(".ehf"))  res.ehf_path  = line;
                else if (matches(".ghf"))  res.ghf_path  = line;
                else if (matches(".hdb"))  res.hdb_path  = line;
                else if (matches(".genv")) res.genv_path = line;
                else if (matches(".ama"))  res.ama_path  = line;
                else if (matches(".amm"))  res.amm_path  = line;
                else if (matches(".amr"))  res.amr_path  = line;
                else if (matches("_models.bnk")) res.model_body_bnk = line;

                OutputLog::info("  " + line);
            }
        } else {
            OutputLog::warn("no companion .list (" + list_path + ") in BNK");
        }
    }

    auto basename_no_ext = [](const std::string& p) -> std::string {
        size_t slash = p.find_last_of("/\\");
        std::string s = (slash == std::string::npos)
            ? p
            : p.substr(slash + 1);
        auto dot = s.find_last_of('.');
        if (dot != std::string::npos) s.resize(dot);
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        return s;
    };
    if (res.ehf_path.empty() && !all_ehf_refs.empty()) {
        const std::string ghf_base = basename_no_ext(res.ghf_path);
        for (const auto& candidate : all_ehf_refs) {
            if (!ghf_base.empty() &&
                basename_no_ext(candidate) == ghf_base) {
                res.ehf_path = candidate;
                break;
            }
        }
        if (res.ehf_path.empty()) res.ehf_path = all_ehf_refs.front();
    }

    auto report_slot = [](const char* label, const std::string& v) {
        if (v.empty()) {
            OutputLog::warn(std::string("  ") + label + ": (missing)");
        } else {
            OutputLog::success(std::string("  ") + label + ": " + v);
        }
    };
    OutputLog::info("heightfield resources for this level:");
    report_slot(".ehf  (graphics desc)", res.ehf_path);
    report_slot(".ghf  (raw heightmap)", res.ghf_path);
    report_slot(".hdb  (height database)", res.hdb_path);
    report_slot(".genv (env table)",     res.genv_path);
    report_slot(".ama  (ambient)",       res.ama_path);
    report_slot(".amm  (ambient meta)",  res.amm_path);
    report_slot(".amr  (ambient refs)",  res.amr_path);
    report_slot("models",                res.model_body_bnk);

    {
        struct SiblingSlot { const char* label; const std::string& path; };
        const SiblingSlot slots[] = {
            { ".hdb  (height database)", res.hdb_path  },
            { ".genv (env table)",       res.genv_path },
            { ".ama  (ambient)",         res.ama_path  },
            { ".amm  (ambient meta)",    res.amm_path  },
            { ".amr  (ambient refs)",    res.amr_path  },
        };
        OutputLog::info("loading .list terrain siblings:");
        for (const auto& s : slots) {
            if (s.path.empty()) continue;
            std::vector<uint8_t> bytes;
            if (load_text_sibling(s.path, bytes)) {
                std::ostringstream os;
                os << "  " << s.label << " loaded (" << bytes.size() << " bytes)";
                OutputLog::success(os.str());
            } else {
                OutputLog::warn(std::string("  ") + s.label + " load FAILED: " + s.path);
            }
        }
    }

    g_level_havok_collision.clear();
    OutputLog::info("havok_scenario loading disabled");
    if (bail_if_cancelled("after terrain siblings")) return false;

    g_level_vfs_texture_body_bnks.clear();
    g_level_vfs_model_bnks.clear();
    g_level_vfs_streaming_bnks.clear();
    {
        std::vector<uint8_t> vfs_bytes;
        std::filesystem::path vfs_path = entry.full_path;
        vfs_path.replace_filename("level.vfsconfig");
        if (load_text_sibling(vfs_path.string(), vfs_bytes)) {
            auto vfs = Level::ParseVfsConfig(vfs_bytes);
            g_level_vfs_texture_body_bnks = std::move(vfs.texture_body_bnks);
            g_level_vfs_model_bnks        = std::move(vfs.model_bnks);
            g_level_vfs_streaming_bnks    = std::move(vfs.streaming_bnks);
            std::ostringstream os;
            os << "vfsconfig: "
               << g_level_vfs_texture_body_bnks.size() << " texture body BNKs, "
               << g_level_vfs_model_bnks.size() << " model BNKs, "
               << g_level_vfs_streaming_bnks.size() << " streaming BNKs";
            OutputLog::info(os.str());
            for (const auto& p : g_level_vfs_texture_body_bnks) {
                OutputLog::info("  tex-body: " + p);
            }
            for (const auto& p : g_level_vfs_model_bnks) {
                OutputLog::info("  model:    " + p);
            }
            for (const auto& p : g_level_vfs_streaming_bnks) {
                OutputLog::info("  stream:   " + p);
            }
        } else {
            OutputLog::warn("no level.vfsconfig sibling in BNK");
        }
    }
    if (bail_if_cancelled("after vfsconfig")) return false;
    const std::vector<StreamingModelCandidate> streaming_model_candidates =
        collect_streaming_model_candidates(g_level_vfs_streaming_bnks);
    if (!streaming_model_candidates.empty()) {
        size_t indexed = 0;
        for (const auto& c : streaming_model_candidates) {
            if (c.entry) ++indexed;
        }
        OutputLog::info("streaming model candidates: " +
                        std::to_string(streaming_model_candidates.size()) +
                        " streaming hint path(s), " + std::to_string(indexed) +
                        " resolved through global .mdl index");
    }
    g_level_gdb_placements.clear();
    {
        std::vector<std::pair<uint32_t, std::string>> save_hash_to_name;
        struct SavePhysicsPlacement {
            uint32_t hash = 0;
            std::string entity_name;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;
        };
        std::vector<SavePhysicsPlacement> save_physics_placements;
        {
            std::vector<uint8_t> save_bytes;
            const std::string save_path = sibling_with_ext(".save");
            if (load_text_sibling(save_path, save_bytes)) {
                std::string xml(reinterpret_cast<const char*>(save_bytes.data()),
                                save_bytes.size());
                auto tag_payload = [](const std::string& s,
                                      const char* tag,
                                      std::string& out) -> bool {
                    const std::string open = std::string("<") + tag;
                    const std::string close = std::string("</") + tag + ">";
                    size_t a = s.find(open);
                    if (a == std::string::npos) return false;
                    a = s.find('>', a);
                    if (a == std::string::npos) return false;
                    size_t b = s.find(close, a + 1);
                    if (b == std::string::npos) return false;
                    out = s.substr(a + 1, b - (a + 1));
                    return true;
                };
                auto parse_float_text = [](const std::string& s,
                                           float& out) -> bool {
                    const char* p = s.c_str();
                    char* end = nullptr;
                    float v = std::strtof(p, &end);
                    if (end == p || !std::isfinite(v)) return false;
                    out = v;
                    return true;
                };
                auto read_float_tag = [&](const std::string& s,
                                          const char* tag,
                                          float& out) -> bool {
                    std::string payload;
                    return tag_payload(s, tag, payload) &&
                           parse_float_text(payload, out);
                };
                auto read_vec3_tag = [&](const std::string& s,
                                         const char* tag,
                                         float& x,
                                         float& y,
                                         float& z) -> bool {
                    std::string payload;
                    return tag_payload(s, tag, payload) &&
                           read_float_tag(payload, "X", x) &&
                           read_float_tag(payload, "Y", y) &&
                           read_float_tag(payload, "Z", z);
                };
                auto read_quat_tag = [&](const std::string& s,
                                         const char* tag,
                                         float& x,
                                         float& y,
                                         float& z,
                                         float& w) -> bool {
                    std::string payload;
                    if (!tag_payload(s, tag, payload)) return false;
                    bool ok = read_float_tag(payload, "X", x) &&
                              read_float_tag(payload, "Y", y) &&
                              read_float_tag(payload, "Z", z);
                    float rw = 1.0f;
                    if (read_float_tag(payload, "W", rw)) w = rw;
                    return ok;
                };
                const std::string tag_open  = "<Entity name=\"";
                const std::string tag_close = "</Entity>";
                size_t pos = 0;
                while (true) {
                    size_t a = xml.find(tag_open, pos);
                    if (a == std::string::npos) break;
                    a += tag_open.size();
                    size_t name_end = xml.find('"', a);
                    if (name_end == std::string::npos) break;
                    std::string name = xml.substr(a, name_end - a);
                    size_t hash_start = xml.find("0x", name_end);
                    if (hash_start == std::string::npos) break;
                    size_t hash_end = xml.find('<', hash_start);
                    if (hash_end == std::string::npos) break;
                    std::string hex = xml.substr(hash_start + 2, hash_end - hash_start - 2);
                    size_t entity_close = xml.find(tag_close, hash_end);
                    if (entity_close == std::string::npos) break;
                    uint32_t h = 0;
                    for (char c : hex) {
                        h <<= 4;
                        if (c >= '0' && c <= '9') h |= (c - '0');
                        else if (c >= 'A' && c <= 'F') h |= (c - 'A' + 10);
                        else if (c >= 'a' && c <= 'f') h |= (c - 'a' + 10);
                    }
                    std::string entity_xml =
                        xml.substr(name_end + 1, entity_close - (name_end + 1));
                    std::string physics_xml;
                    SavePhysicsPlacement sp;
                    sp.hash = h;
                    sp.entity_name = name;
                    if (tag_payload(entity_xml, "PhysicsData", physics_xml) &&
                        read_vec3_tag(physics_xml, "Position", sp.x, sp.y, sp.z)) {
                        read_quat_tag(physics_xml, "Orientation",
                                      sp.qx, sp.qy, sp.qz, sp.qw);
                        save_physics_placements.push_back(std::move(sp));
                    }
                    save_hash_to_name.emplace_back(h, std::move(name));
                    pos = entity_close + tag_close.size();
                    if ((save_hash_to_name.size() & 0x7fu) == 0 &&
                        bail_if_cancelled("save parse"))
                    {
                        return false;
                    }
                }
                OutputLog::info("save: " + std::to_string(save_hash_to_name.size())
                                + " entity hash→name mappings");
                if (!save_physics_placements.empty()) {
                    OutputLog::info("save: " +
                                    std::to_string(save_physics_placements.size()) +
                                    " PhysicsData transform(s)");
                }
            } else {
                OutputLog::warn("no .save sibling in BNK");
            }
        }
        if (bail_if_cancelled("after save parse")) return false;

        struct SupplementalGdb {
            std::string path;
            std::vector<uint8_t> bytes;
        };
        std::vector<SupplementalGdb> supplemental_gdbs;
        {
            const char* game_gdb_paths[] = {
                "data\\Globals\\Globals.gdb",
                "data\\Globals\\SpeechAction.gdb",
                "data\\InteractiveCutscenes\\InteractiveCutscenes.gdb",
                "data\\Entity\\Entity.gdb",
            };
            for (const char* game_gdb_path : game_gdb_paths) {
                std::vector<uint8_t> bytes;
                if (load_text_sibling(game_gdb_path, bytes)) {
                    supplemental_gdbs.push_back(
                        SupplementalGdb{game_gdb_path, std::move(bytes)});
                }
            }
            if (!supplemental_gdbs.empty()) {
                OutputLog::info(
                    "gdb supplemental resolver: loaded " +
                    std::to_string(supplemental_gdbs.size()) +
                    " game DB(s)");
                for (const auto& db : supplemental_gdbs) {
                    OutputLog::info("  gdb-supplement: " + db.path +
                                    " (" + std::to_string(db.bytes.size()) +
                                    " bytes)");
                }
            }
        }
        if (bail_if_cancelled("after supplemental gdb load")) return false;

        const auto& level_prop_blocks = info.prop_blocks;
        std::vector<uint8_t> gdb_bytes;
        const std::string gdb_path = sibling_with_ext(".gdb");
        if (load_text_sibling(gdb_path, gdb_bytes)) {
            auto info = Gdb::ParseWithSaveMap(gdb_bytes, save_hash_to_name);
            if (bail_if_cancelled("after gdb parse")) return false;
            {
                std::vector<const std::vector<uint8_t>*> water_theme_gdbs;
                water_theme_gdbs.reserve(1 + supplemental_gdbs.size());
                water_theme_gdbs.push_back(&gdb_bytes);
                  for (const auto& db : supplemental_gdbs) {
                      water_theme_gdbs.push_back(&db.bytes);
                  }
 
                  auto colour_text = [](const float (&c)[3]) {
                      std::ostringstream ss;
                      ss << std::fixed << std::setprecision(3)
                         << c[0] << ',' << c[1] << ',' << c[2];
                      return ss.str();
                  };

                  Gdb::WaterTheme water_theme;
                  if (Gdb::ExtractWaterTheme(water_theme_gdbs, water_theme)) {
                      g_pending_level_water_theme = water_theme;
                      std::ostringstream ss;
                      ss << "water theme: GDB env params found";
                    if (water_theme.has_shallow_colour) {
                        ss << " shallow=("
                           << colour_text(water_theme.shallow_colour) << ')';
                    }
                    if (water_theme.has_deep_colour) {
                        ss << " deep=("
                           << colour_text(water_theme.deep_colour) << ')';
                    }
                    if (water_theme.source_time_of_day >= 0.0f) {
                        ss << " time="
                           << std::fixed << std::setprecision(3)
                           << water_theme.source_time_of_day;
                    }
                    OutputLog::success(ss.str());
                } else {
                    g_pending_level_water_theme = Gdb::WaterTheme{};
                      OutputLog::info(
                          "water theme: no GDB environment water params found; "
                          "using shader fallback");
                  }

                  Gdb::SkyTheme sky_theme;
                  if (Gdb::ExtractSkyTheme(water_theme_gdbs, sky_theme)) {
                      g_pending_level_sky_theme = sky_theme;
                      std::ostringstream ss;
                      ss << "sky theme: GDB env params found";
                      if (sky_theme.has_sky_colour) {
                          ss << " sky=("
                             << colour_text(sky_theme.sky_colour) << ')';
                      }
                      if (sky_theme.has_fog_colour) {
                          ss << " fog=("
                             << colour_text(sky_theme.fog_colour) << ')';
                      }
                      if (sky_theme.source_time_of_day >= 0.0f) {
                          ss << " time="
                             << std::fixed << std::setprecision(3)
                             << sky_theme.source_time_of_day;
                      }
                      OutputLog::success(ss.str());
                  } else {
                      g_pending_level_sky_theme = Gdb::SkyTheme{};
                      OutputLog::info(
                          "sky theme: no GDB environment sky params found; "
                          "using preview fallback");
                  }

                  Gdb::CloudTheme cloud_theme;
                  if (Gdb::ExtractCloudTheme(water_theme_gdbs,
                                             cloud_theme)) {
                      g_pending_level_cloud_theme = cloud_theme;
                      std::ostringstream ss;
                      ss << "cloud theme: GDB env params found layers="
                         << cloud_theme.layer_count;
                      if (cloud_theme.layer_count > 0) {
                          const auto& layer = cloud_theme.layers[0];
                          ss << " first=(height="
                             << std::fixed << std::setprecision(1)
                             << layer.height
                             << ", transparency="
                             << std::setprecision(2)
                             << layer.transparency << ')';
                      }
                      if (cloud_theme.source_time_of_day >= 0.0f) {
                          ss << " time="
                             << std::fixed << std::setprecision(3)
                             << cloud_theme.source_time_of_day;
                      }
                      OutputLog::success(ss.str());
                  } else {
                      g_pending_level_cloud_theme = Gdb::CloudTheme{};
                      OutputLog::info(
                          "cloud theme: no GDB environment cloud params found; "
                          "using clear sky");
                  }

                  Gdb::EnvironmentThemeTimeline env_timeline;
                  if (Gdb::ExtractEnvironmentThemeTimeline(
                          water_theme_gdbs, env_timeline)) {
                      g_pending_level_environment_timeline = env_timeline;
                      std::ostringstream ss;
                      ss << "day/night cycle: GDB env day-set found keyframes="
                         << env_timeline.keyframes.size();
                      if (!env_timeline.keyframes.empty()) {
                          ss << " span=["
                             << std::fixed << std::setprecision(3)
                             << env_timeline.keyframes.front().time_of_day
                             << ".."
                             << env_timeline.keyframes.back().time_of_day
                             << "]";
                      }
                      OutputLog::success(ss.str());
                  } else {
                      g_pending_level_environment_timeline =
                          Gdb::EnvironmentThemeTimeline{};
                      OutputLog::info(
                          "day/night cycle: no multi-keyframe GDB day-set; "
                          "using fixed environment theme");
                  }
              }
            if (bail_if_cancelled("after environment theme parse")) return false;
            if (!supplemental_gdbs.empty()) {
                const bool is_bwsslums_level_for_supplemental =
                    lower_slash(entry.full_path).find("bwsslums") !=
                    std::string::npos;
                size_t supplemental_model_hits = 0;
                size_t supplemental_model_augments = 0;
                size_t supplemental_parent_hits = 0;
                size_t supplemental_model_parts = 0;
                size_t supplemental_misses = 0;
                size_t supplemental_poll = 0;
                for (auto& p : info.placements) {
                    if ((++supplemental_poll & 0x7fu) == 0 &&
                        bail_if_cancelled("supplemental model resolve"))
                    {
                        return false;
                    }
                    const std::string supplemental_entity_key =
                        compact_match_key(p.entity_name);
                    const bool allow_slums_shell_augment =
                        is_bwsslums_level_for_supplemental &&
                        p.model_path_hash != 0 &&
                        (supplemental_entity_key.find("slumstreethouse") !=
                             std::string::npos ||
                         supplemental_entity_key.find("townhouse") !=
                             std::string::npos);
                    if (p.hash_a == 0 ||
                        (p.model_path_hash != 0 &&
                         !allow_slums_shell_augment))
                    {
                        continue;
                    }
                    std::vector<uint32_t> model_hashes;
                    uint32_t parent_hash = 0;
                    bool found = false;
                    std::string source_gdb;
                    uint32_t lookup_hash = 0;
                    for (const auto& db : supplemental_gdbs) {
                        if (Gdb::LookupModelPathHashes(
                                db.bytes, p.hash_a, model_hashes,
                                &parent_hash))
                        {
                            found = true;
                            source_gdb = db.path;
                            lookup_hash = p.hash_a;
                            break;
                        }
                        if (p.parent_hash != 0 &&
                            Gdb::LookupModelPathHashes(
                                db.bytes, p.parent_hash, model_hashes,
                                nullptr))
                        {
                            found = true;
                            source_gdb = db.path;
                            lookup_hash = p.parent_hash;
                            break;
                        }
                    }
                    if (found && !model_hashes.empty()) {
                        const bool augmenting_existing_hash =
                            p.model_path_hash != 0;
                        if (augmenting_existing_hash) {
                            if (p.model_path_hashes.empty()) {
                                p.model_path_hashes.push_back(
                                    p.model_path_hash);
                            }
                            for (uint32_t h : model_hashes) {
                                if (std::find(
                                        p.model_path_hashes.begin(),
                                        p.model_path_hashes.end(), h) ==
                                    p.model_path_hashes.end())
                                {
                                    p.model_path_hashes.push_back(h);
                                }
                            }
                            ++supplemental_model_augments;
                        } else {
                            p.model_path_hashes = model_hashes;
                            p.model_path_hash = model_hashes.front();
                        }
                        ++supplemental_model_hits;
                        supplemental_model_parts +=
                            p.model_path_hashes.size();
                        if (p.parent_hash == 0 && parent_hash != 0) {
                            p.parent_hash = parent_hash;
                            ++supplemental_parent_hits;
                        }
                    } else if (!p.indexed_record) {
                        ++supplemental_misses;
                    }
                }
                if (supplemental_model_parts > supplemental_model_hits) {
                    std::vector<Gdb::Placement> extra_parts;
                    size_t extra_poll = 0;
                    for (auto& p : info.placements) {
                        if ((++extra_poll & 0x7fu) == 0 &&
                            bail_if_cancelled("supplemental part expansion"))
                        {
                            return false;
                        }
                        if (p.model_path_hashes.size() <= 1) continue;
                        const std::vector<uint32_t> hashes =
                            p.model_path_hashes;
                        p.model_path_hash = hashes.front();
                        p.model_path_hashes.clear();
                        p.model_path_hashes.push_back(hashes.front());
                        for (size_t i = 1; i < hashes.size(); ++i) {
                            Gdb::Placement part = p;
                            part.model_path_hash = hashes[i];
                            part.model_path_hashes.clear();
                            part.model_path_hashes.push_back(hashes[i]);
                            extra_parts.push_back(std::move(part));
                        }
                    }
                    info.placements.insert(
                        info.placements.end(),
                        std::make_move_iterator(extra_parts.begin()),
                        std::make_move_iterator(extra_parts.end()));
                }
                if (supplemental_model_hits > 0 ||
                    supplemental_misses > 0)
                {
                    OutputLog::info(
                        "gdb supplemental model path hashes: hit=" +
                        std::to_string(supplemental_model_hits) +
                        ", augment=" +
                        std::to_string(supplemental_model_augments) +
                        ", parts=" +
                        std::to_string(supplemental_model_parts) +
                        ", miss=" + std::to_string(supplemental_misses) +
                        ", parent-filled=" +
                        std::to_string(supplemental_parent_hits));
                }
            }

            if (!supplemental_gdbs.empty())
            {
                std::unordered_set<uint32_t> existing_hashes;
                existing_hashes.reserve(info.placements.size() * 2);
                for (const auto& p : info.placements) {
                    if (p.hash_a != 0) existing_hashes.insert(p.hash_a);
                }

                size_t supplemental_entity_hits = 0;
                size_t supplemental_entity_parts = 0;
                size_t supplemental_entity_misses = 0;
                size_t supplemental_entity_poll = 0;
                for (const auto& kv : save_hash_to_name) {
                    if ((++supplemental_entity_poll & 0x7fu) == 0 &&
                        bail_if_cancelled("supplemental entity resolve"))
                    {
                        return false;
                    }
                    if (kv.first == 0 ||
                        existing_hashes.find(kv.first) !=
                            existing_hashes.end())
                    {
                        continue;
                    }

                    Gdb::Placement supplemental;
                    bool found = false;
                    for (const auto& db : supplemental_gdbs) {
                        if (Gdb::LookupPlacement(db.bytes, kv.first,
                                                 kv.second, supplemental) &&
                            !supplemental.model_path_hashes.empty())
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        ++supplemental_entity_misses;
                        continue;
                    }

                    ++supplemental_entity_hits;
                    existing_hashes.insert(kv.first);
                    if (supplemental.model_path_hashes.size() > 1) {
                        const std::vector<uint32_t> hashes =
                            supplemental.model_path_hashes;
                        for (uint32_t h : hashes) {
                            Gdb::Placement part = supplemental;
                            part.model_path_hash = h;
                            part.model_path_hashes.clear();
                            part.model_path_hashes.push_back(h);
                            info.placements.push_back(std::move(part));
                            ++supplemental_entity_parts;
                        }
                    } else {
                        info.placements.push_back(std::move(supplemental));
                        ++supplemental_entity_parts;
                    }
                }
                if (supplemental_entity_hits > 0 ||
                    supplemental_entity_misses > 0)
                {
                    OutputLog::info(
                        "gdb supplemental direct entity records: hit=" +
                        std::to_string(supplemental_entity_hits) +
                        ", parts=" +
                        std::to_string(supplemental_entity_parts) +
                        ", miss=" +
                        std::to_string(supplemental_entity_misses));
                }
            }
            g_level_gdb_placements.reserve(info.placements.size());

            size_t fixed_count = 0, var_count = 0, named_count = 0;
            size_t model_hash_count = 0;
            size_t placement_poll = 0;
            for (const auto& p : info.placements) {
                if ((++placement_poll & 0xffu) == 0 &&
                    bail_if_cancelled("placement table build"))
                {
                    return false;
                }
                GdbWorldPlacement gp;
                gp.x      = p.x;
                gp.y      = p.y;
                gp.z      = p.z;
                gp.yaw    = p.yaw;
                gp.rot_x  = p.rot_x;
                gp.rot_y  = p.rot_y;
                gp.rot_z  = p.rot_z;
                gp.scale  = p.scale;
                gp.hash   = p.hash_a;
                gp.parent_hash = p.parent_hash;
                gp.model_path_hash = p.model_path_hash;
                gp.marker = p.marker;
                g_level_gdb_placements.push_back(gp);
                if (p.marker == 0x00004B40) ++fixed_count;
                else                         ++var_count;
                if (!p.entity_name.empty())  ++named_count;
                if (p.model_path_hash != 0)  ++model_hash_count;
            }
            std::ostringstream os;
            os << "gdb: " << g_level_gdb_placements.size()
               << " placements (" << fixed_count << " fixed + "
               << var_count << " variable, "
               << named_count << " resolved to .save entity names, "
               << model_hash_count << " with model path hashes)";
            OutputLog::success(os.str());

            struct GdbStreamingChoiceCacheValue {
                const StreamingModelCandidate* hit = nullptr;
                int score = INT_MIN;
            };
            std::unordered_map<std::string, GdbStreamingChoiceCacheValue>
                gdb_streaming_choice_cache;
            auto choose_streaming_cached =
                [&](const std::string& entity_name,
                    uint32_t parent_hash,
                    int* out_score) -> const StreamingModelCandidate* {
                    if (streaming_model_candidates.empty()) {
                        if (out_score) *out_score = INT_MIN;
                        return nullptr;
                    }
                    std::string cache_key = std::to_string(parent_hash);
                    cache_key.push_back('|');
                    cache_key += entity_name;
                    auto cached =
                        gdb_streaming_choice_cache.find(cache_key);
                    if (cached != gdb_streaming_choice_cache.end()) {
                        if (out_score) *out_score = cached->second.score;
                        return cached->second.hit;
                    }

                    int score = INT_MIN;
                    const StreamingModelCandidate* hit =
                        choose_streaming_model_for_gdb(
                            entity_name, streaming_model_candidates,
                            &score, parent_hash);
                    gdb_streaming_choice_cache.emplace(
                        std::move(cache_key),
                        GdbStreamingChoiceCacheValue{hit, score});
                    if (out_score) *out_score = score;
                    return hit;
                };


            struct GdbArchetypeDiag {
                size_t count = 0;
                std::vector<std::string> examples;
            };
            std::unordered_map<uint32_t, GdbArchetypeDiag> archetype_diag;
            for (const auto& p : info.placements) {
                if (p.marker != 0x00004B80 || p.parent_hash == 0) continue;
                auto& d = archetype_diag[p.parent_hash];
                ++d.count;
                if (!p.entity_name.empty() && d.examples.size() < 12) {
                    d.examples.push_back(p.entity_name);
                }
            }
            std::vector<std::pair<uint32_t, GdbArchetypeDiag*>> archetypes;
            archetypes.reserve(archetype_diag.size());
            for (auto& kv : archetype_diag) {
                archetypes.push_back({kv.first, &kv.second});
            }
            std::sort(archetypes.begin(), archetypes.end(),
                      [](const auto& a, const auto& b) {
                          return a.second->count > b.second->count;
                      });

            auto strip_suffix = [](std::string s, const char* suf) {
                size_t n = std::strlen(suf);
                if (s.size() > n && s.compare(s.size() - n, n, suf) == 0) {
                    s.resize(s.size() - n);
                }
                return s;
            };
            auto canonicalize_for_match = [&strip_suffix](std::string s) {
                size_t us = s.find_last_of('_');
                if (us != std::string::npos && us + 1 < s.size()) {
                    bool all_digits = true;
                    for (size_t k = us + 1; k < s.size(); ++k) {
                        if (s[k] < '0' || s[k] > '9') { all_digits = false; break; }
                    }
                    if (all_digits) s.resize(us);
                }
                static const char* prefixes[] = {
                    "NewObjectBuilding", "ObjectBuilding",
                    "NewObjectFurniture", "ObjectFurniture",
                    "NewObjectStatic", "ObjectStatic",
                    "NewObject", "Object",
                    "Static", "New"
                };
                for (const char* pfx : prefixes) {
                    size_t pn = std::strlen(pfx);
                    if (s.size() > pn && s.compare(0, pn, pfx) == 0) {
                        s = s.substr(pn);
                        break;
                    }
                }
                std::string out;
                out.reserve(s.size());
                for (char c : s) {
                    if (c == '_') continue;
                    out.push_back(char(std::tolower(static_cast<unsigned char>(c))));
                }
                out = strip_suffix(out, "facademid");
                out = strip_suffix(out, "facade");
                out = strip_suffix(out, "lod1");
                out = strip_suffix(out, "lod0");
                out = strip_suffix(out, "mid");
                return out;
            };

            auto best_gdb_name_for_examples_for_matching =
                [&](const std::vector<std::string>& examples,
                    uint32_t parent_hash) {
                    std::string best_name = gdb_representative_name(examples);
                    int best_score = INT_MIN;
                    for (const auto& ex : examples) {
                        const std::string repr =
                            gdb_representative_name(std::vector<std::string>{ex});
                        int score = INT_MIN;
                        const StreamingModelCandidate* hit =
                            choose_streaming_cached(
                                repr, parent_hash, &score);
                        if (hit && score > best_score) {
                            best_name = repr;
                            best_score = score;
                        }
                    }
                    return best_name;
                };

            std::unordered_map<uint32_t, std::string> parent_match_names;
            if (!streaming_model_candidates.empty()) {
                parent_match_names.reserve(archetype_diag.size());
                for (const auto& kv : archetype_diag) {
                    std::string repr =
                        best_gdb_name_for_examples_for_matching(
                            kv.second.examples, kv.first);
                    if (repr.empty()) continue;
                    if (choose_streaming_cached(
                            repr, kv.first, nullptr)) {
                        parent_match_names.emplace(kv.first, std::move(repr));
                    }
                }
            }
            std::vector<std::string> preferred_model_bnks;
            auto add_preferred_model_bnk = [&](const std::string& bnk) {
                if (bnk.empty()) return;
                std::string norm = bnk;
                std::transform(norm.begin(), norm.end(), norm.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                std::replace(norm.begin(), norm.end(), '\\', '/');
                if (std::find(preferred_model_bnks.begin(),
                              preferred_model_bnks.end(),
                              norm) == preferred_model_bnks.end()) {
                    preferred_model_bnks.push_back(std::move(norm));
                }
            };
            auto resolve_preferred_model_bnk = [&](const std::string& vpath) {
                if (vpath.empty()) return;
                if (auto found = find_bnk_by_virtual_path(vpath)) {
                    add_preferred_model_bnk(*found);
                    return;
                }
                size_t slash = vpath.find_last_of("/\\");
                std::string leaf = (slash == std::string::npos)
                    ? vpath : vpath.substr(slash + 1);
                std::transform(leaf.begin(), leaf.end(), leaf.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                if (auto found = find_bnk_by_filename(leaf)) {
                    add_preferred_model_bnk(*found);
                }
            };
            resolve_preferred_model_bnk(res.model_body_bnk);
            for (const auto& bnk : g_level_vfs_model_bnks) {
                resolve_preferred_model_bnk(bnk);
            }

            std::unordered_map<std::string, std::vector<const FlatAssetEntry*>> mdl_by_token;
            mdl_by_token.reserve(S.all_mdl_files.size() * 2);
            for (const auto& m : S.all_mdl_files) {
                std::string base = m.name;
                size_t dot = base.find_last_of('.');
                if (dot != std::string::npos) base.resize(dot);
                std::string lc;
                lc.reserve(base.size());
                for (char c : base) {
                    if (c == '_') continue;
                    lc.push_back(char(std::tolower(static_cast<unsigned char>(c))));
                }
                lc = strip_suffix(lc, "facademid");
                lc = strip_suffix(lc, "facade");
                lc = strip_suffix(lc, "lod1");
                lc = strip_suffix(lc, "lod0");
                lc = strip_suffix(lc, "mid");
                if (!lc.empty()) {
                    mdl_by_token[lc].push_back(&m);
                }
            }
            auto normalized_path = [](std::string s) {
                std::transform(s.begin(), s.end(), s.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                std::replace(s.begin(), s.end(), '\\', '/');
                return s;
            };
            auto model_bank_score = [&](const FlatAssetEntry* e) {
                if (!e) return 0;
                const std::string bnk = normalized_path(e->bnk_path);
                for (size_t i = 0; i < preferred_model_bnks.size(); ++i) {
                    if (bnk == preferred_model_bnks[i]) {
                        return 4000 - int(i);
                    }
                }
                if (bnk.find("/globals_models.bnk") != std::string::npos ||
                    bnk == "globals_models.bnk") {
                    return 100;
                }
                return 0;
            };
            auto choose_model_candidate =
                [&](const std::vector<const FlatAssetEntry*>& candidates) {
                    const FlatAssetEntry* best = nullptr;
                    int best_score = INT_MIN;
                    for (const FlatAssetEntry* e : candidates) {
                        int score = model_bank_score(e);
                        if (e && e->from_nested) score += 250;
                        score -= int(std::min<size_t>(e ? e->full_path.size() : 0, 200));
                        if (!best || score > best_score) {
                            best = e;
                            best_score = score;
                        }
                    }
                    return best;
                };

            std::unordered_map<uint32_t, std::vector<const FlatAssetEntry*>>
                mdl_by_model_path_hash;
            mdl_by_model_path_hash.reserve(S.all_mdl_files.size() * 2);
            std::unordered_map<std::string, std::vector<const FlatAssetEntry*>>
                mdl_by_lower_path;
            mdl_by_lower_path.reserve(S.all_mdl_files.size() * 2);
            for (const auto& m : S.all_mdl_files) {
                if (m.full_path.empty()) continue;
                mdl_by_model_path_hash[fnv1_model_path_hash(m.full_path)]
                    .push_back(&m);
                mdl_by_lower_path[lower_slash(m.full_path)].push_back(&m);
            }
            auto path_suffix_matches_local =
                [](const std::string& path, const std::string& target) {
                    if (path.empty() || target.empty()) return false;
                    if (path == target) return true;
                    return path.size() > target.size() &&
                           path.compare(path.size() - target.size(),
                                        target.size(), target) == 0 &&
                           path[path.size() - target.size() - 1] == '/';
                };
            std::unordered_map<uint32_t, const FlatAssetEntry*>
                model_path_hash_cache;
            model_path_hash_cache.reserve(info.placements.size());
            auto resolve_model_by_path_hash = [&](uint32_t model_path_hash) {
                if (model_path_hash == 0) {
                    return static_cast<const FlatAssetEntry*>(nullptr);
                }
                auto cached = model_path_hash_cache.find(model_path_hash);
                if (cached != model_path_hash_cache.end()) {
                    return cached->second;
                }
                const FlatAssetEntry* hit = nullptr;
                auto it = mdl_by_model_path_hash.find(model_path_hash);
                if (it != mdl_by_model_path_hash.end()) {
                    hit = choose_model_candidate(it->second);
                }
                model_path_hash_cache.emplace(model_path_hash, hit);
                return hit;
            };
            std::unordered_map<std::string, const FlatAssetEntry*>
                lower_path_model_cache;
            auto resolve_model_by_lower_path =
                [&](const std::string& lower_path) {
                    if (lower_path.empty()) {
                        return static_cast<const FlatAssetEntry*>(nullptr);
                    }
                    auto cached = lower_path_model_cache.find(lower_path);
                    if (cached != lower_path_model_cache.end()) {
                        return cached->second;
                    }
                    const FlatAssetEntry* hit = nullptr;
                    auto it = mdl_by_lower_path.find(lower_path);
                    if (it != mdl_by_lower_path.end()) {
                        hit = choose_model_candidate(it->second);
                    }
                    if (!hit) {
                        std::vector<const FlatAssetEntry*> suffix_hits;
                        for (const auto& kv : mdl_by_lower_path) {
                            if (path_suffix_matches_local(kv.first,
                                                          lower_path)) {
                                suffix_hits.insert(suffix_hits.end(),
                                                   kv.second.begin(),
                                                   kv.second.end());
                            }
                        }
                        if (!suffix_hits.empty()) {
                            hit = choose_model_candidate(suffix_hits);
                        }
                    }
                    if (!hit) {
                        const std::string leaf_key =
                            canonicalize_for_match(
                                model_name_from_path(lower_path));
                        if (!leaf_key.empty() &&
                            leaf_key != "interior" &&
                            leaf_key != "exterior")
                        {
                            auto tok = mdl_by_token.find(leaf_key);
                            if (tok != mdl_by_token.end()) {
                                hit = choose_model_candidate(tok->second);
                            }
                        }
                    }
                    lower_path_model_cache.emplace(lower_path, hit);
                    return hit;
                };
            std::unordered_map<std::string, const FlatAssetEntry*>
                entity_model_cache;
            auto resolve_model_for_entity = [&](const std::string& entity_name) {
                std::string tok = canonicalize_for_match(entity_name);
                if (tok.empty()) return static_cast<const FlatAssetEntry*>(nullptr);
                auto token_is_or_numbered = [&](const char* base) {
                    const size_t n = std::strlen(base);
                    if (tok == base) return true;
                    if (tok.size() <= n || tok.compare(0, n, base) != 0) {
                        return false;
                    }
                    for (size_t i = n; i < tok.size(); ++i) {
                        if (!std::isdigit(static_cast<unsigned char>(tok[i]))) {
                            return false;
                        }
                    }
                    return true;
                };
                const bool general_store_building =
                    token_is_or_numbered("generalstore");
                auto is_bad_general_store_fallback =
                    [&](const std::string& model_key,
                        const FlatAssetEntry* candidate) {
                        if (!general_store_building) return false;
                        if (model_key.find("signgeneralstore") !=
                            std::string::npos)
                        {
                            return true;
                        }
                        return candidate &&
                               compact_match_key(candidate->full_path).find(
                                   "signgeneralstore") != std::string::npos;
                    };

                auto cached = entity_model_cache.find(tok);
                if (cached != entity_model_cache.end()) {
                    return cached->second;
                }

                const FlatAssetEntry* best = nullptr;
                const std::string entity_lookup_key =
                    gdb_entity_key(entity_name);
                auto exact = mdl_by_token.find(tok);
                if (exact != mdl_by_token.end()) {
                    best = choose_model_candidate(exact->second);
                    if (is_bad_general_store_fallback(tok, best) ||
                        (best && is_bad_market_helper_substitution(
                            entity_lookup_key, tok, best->full_path)))
                    {
                        best = nullptr;
                    }
                    entity_model_cache.emplace(tok, best);
                    return best;
                }

                if (tok.size() < 5) {
                    entity_model_cache.emplace(tok, nullptr);
                    return static_cast<const FlatAssetEntry*>(nullptr);
                }

                int best_score = INT_MIN;
                for (const auto& kv : mdl_by_token) {
                    const std::string& mk = kv.first;
                    if (mk.size() < 5) continue;

                    int relation = INT_MIN;
                    if (mk.find(tok) != std::string::npos) {
                        relation = 5000 + int(tok.size() * 30)
                                 - int((mk.size() > tok.size())
                                           ? (mk.size() - tok.size()) : 0);
                    } else if (tok.find(mk) != std::string::npos &&
                               mk.size() * 2 >= tok.size()) {
                        relation = 2500 + int(mk.size() * 20);
                    } else {
                        continue;
                    }

                    const FlatAssetEntry* candidate =
                        choose_model_candidate(kv.second);
                    if (is_bad_general_store_fallback(mk, candidate) ||
                        (candidate && is_bad_market_helper_substitution(
                            entity_lookup_key, tok, candidate->full_path)))
                    {
                        continue;
                    }
                    const int score = relation + model_bank_score(candidate);
                    if (!best || score > best_score) {
                        best = candidate;
                        best_score = score;
                    }
                }
                entity_model_cache.emplace(tok, best);
                return best;
            };

            constexpr bool emit_gdb_render_placements = true;
            constexpr bool emit_derived_render_placements = false;
            std::unordered_map<std::string, Level::PropBlock> blocks_by_path;
            std::unordered_set<std::string> emitted_prop_transform_keys;
            emitted_prop_transform_keys.reserve(level_prop_blocks.size() * 64);
            for (const auto& block : level_prop_blocks) {
                if (block.model_path.empty()) continue;
                for (const auto& inst : block.instances) {
                    emitted_prop_transform_keys.insert(
                        prop_instance_transform_key(inst, block.model_path));
                }
            }
            auto append_prop_instance_for_model =
                [&](const FlatAssetEntry* model_hit,
                    const Level::PropInstance& inst) {
                    if (!model_hit || model_hit->full_path.empty()) {
                        return false;
                    }
                    if (is_gdb_static_prop_reject_model(model_hit->full_path)) {
                        return false;
                    }
                    if (is_market_bridge_facade_proxy_model(model_hit->full_path)) {
                        return false;
                    }
                    if (!emitted_prop_transform_keys.insert(
                            prop_instance_transform_key(
                                inst, model_hit->full_path)).second)
                    {
                        return false;
                    }
                    auto& pb = blocks_by_path[model_hit->full_path];
                    if (pb.model_path.empty()) {
                        pb.type = 0xB1;
                        pb.model_path = model_hit->full_path;
                    }
                    pb.instances.push_back(inst);
                    return true;
                };
            auto append_prop_instance_for_model_path =
                [&](const std::string& lower_path,
                    const Level::PropInstance& inst) {
                    const FlatAssetEntry* model_hit =
                        resolve_model_by_lower_path(lower_path);
                    return append_prop_instance_for_model(model_hit, inst);
                };

            size_t authored_shop_companions_emitted = 0;
            size_t authored_shop_companion_misses = 0;
            std::unordered_map<std::string, size_t>
                authored_shop_companion_paths;
            for (const auto& block : level_prop_blocks) {
                std::string exterior_path =
                    shop_facade_companion_exterior_path(block.model_path);
                if (exterior_path.empty()) continue;

                std::array<std::string, 2> companions = {
                    exterior_path,
                    companion_interior_path(exterior_path),
                };
                for (const auto& inst : block.instances) {
                    for (const std::string& companion_path : companions) {
                        if (companion_path.empty()) continue;
                        const std::string lower_path =
                            lower_slash(companion_path);
                        if (append_prop_instance_for_model_path(
                                lower_path, inst))
                        {
                            ++authored_shop_companions_emitted;
                            ++authored_shop_companion_paths[lower_path];
                        } else if (!resolve_model_by_lower_path(lower_path)) {
                            ++authored_shop_companion_misses;
                        }
                    }
                }
            }
            if (authored_shop_companions_emitted > 0 ||
                authored_shop_companion_misses > 0)
            {
                OutputLog::info(
                    "authored shop companions: emitted " +
                    std::to_string(authored_shop_companions_emitted) +
                    " instance(s), missing-path " +
                    std::to_string(authored_shop_companion_misses));
                std::vector<std::pair<std::string, size_t>> paths(
                    authored_shop_companion_paths.begin(),
                    authored_shop_companion_paths.end());
                std::sort(paths.begin(), paths.end(),
                          [](const auto& a, const auto& b) {
                              return a.second > b.second;
                          });
                const size_t n = std::min<size_t>(paths.size(), 6);
                for (size_t i = 0; i < n; ++i) {
                    OutputLog::info(
                        "  authored shop companion: " +
                        std::to_string(paths[i].second) + "x  " +
                        paths[i].first);
                }
            }
            const bool is_bwsmarket_level =
                lower_slash(entry.full_path).find("bwsmarket") !=
                std::string::npos;
            const bool is_bwsslums_level =
                lower_slash(entry.full_path).find("bwsslums") !=
                std::string::npos;
            const uint32_t bwsmarket_clocktower_base_hash =
                fnv1_model_path_hash(
                    "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\"
                    "BS_Market_ClockTower\\BS_Market_ClockTower.mdl");
            bool bwsmarket_has_explicit_clocktower_base_record = false;
            if (is_bwsmarket_level) {
                for (const auto& p : info.placements) {
                    if (p.model_path_hash == bwsmarket_clocktower_base_hash) {
                        bwsmarket_has_explicit_clocktower_base_record = true;
                        break;
                    }
                    if (std::find(p.model_path_hashes.begin(),
                                  p.model_path_hashes.end(),
                                  bwsmarket_clocktower_base_hash) !=
                        p.model_path_hashes.end())
                    {
                        bwsmarket_has_explicit_clocktower_base_record = true;
                        break;
                    }
                }
            }

            size_t save_physics_instances_emitted = 0;
            if (emit_derived_render_placements) {
                for (const auto& p : save_physics_placements) {
                    if (p.entity_name.empty()) continue;
                    std::string tok = canonicalize_for_match(p.entity_name);
                    if (tok.empty()) continue;

                    const FlatAssetEntry* hit =
                        resolve_model_for_entity(p.entity_name);
                    if (!hit) continue;
                    if (is_gdb_static_prop_reject_model(hit->full_path)) {
                        continue;
                    }

                    auto& pb = blocks_by_path[hit->full_path];
                    if (pb.model_path.empty()) {
                        pb.type = 0xB2;
                        pb.model_path = hit->full_path;
                    }

                    Level::PropInstance pi;
                    pi.hash = p.hash;
                    pi.values[0] = p.x;
                    pi.values[1] = p.y;
                    pi.values[2] = p.z;
                    float qx = p.qx, qy = p.qy, qz = p.qz, qw = p.qw;
                    const float qmag =
                        std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
                    if (std::isfinite(qmag) && qmag > 1e-6f) {
                        qx /= qmag; qy /= qmag; qz /= qmag; qw /= qmag;
                        const float num = 2.0f * (qw * qz + qx * qy);
                        const float den = 1.0f - 2.0f * (qy * qy + qz * qz);
                        const float mag = std::sqrt(num * num + den * den);
                        if (std::isfinite(mag) && mag > 1e-6f) {
                            pi.values[6] = num / mag;
                            pi.values[7] = den / mag;
                        } else {
                            pi.values[6] = 0.0f;
                            pi.values[7] = 1.0f;
                        }
                    } else {
                        pi.values[6] = 0.0f;
                        pi.values[7] = 1.0f;
                    }
                    pi.values[9] = pi.values[10] = pi.values[11] = 1.0f;
                    pb.instances.push_back(pi);
                    ++save_physics_instances_emitted;
                }
            }
            if (save_physics_instances_emitted > 0) {
                OutputLog::success(
                    "save-derived placements: " +
                    std::to_string(save_physics_instances_emitted) +
                    " PhysicsData instance(s) appended to prop pipeline");
            }

            size_t resolved = 0;
            size_t gdb_instances_emitted = 0;
            size_t gdb_hint_only_skipped = 0;
            size_t gdb_full_euler_rotations = 0;
            size_t gdb_yaw_only_rotations = 0;
            size_t gdb_identity_rotations = 0;
            size_t gdb_pi_pair_yaw_rotations = 0;
            size_t gdb_model_hash_hits = 0;
            size_t gdb_model_hash_misses = 0;
            size_t gdb_authored_shell_skipped = 0;
            size_t gdb_authored_shop_companion_skipped = 0;
            size_t gdb_duplicate_instances_skipped = 0;
            size_t gdb_companion_interiors_emitted = 0;
            size_t gdb_companion_exteriors_emitted = 0;
            std::unordered_map<std::string, size_t>
                gdb_authored_shell_skip_paths;
            std::unordered_map<std::string, std::vector<std::string>>
                gdb_authored_shell_skip_samples;
            std::unordered_map<std::string, size_t>
                gdb_emitted_shell_paths;
            std::unordered_map<std::string, std::vector<std::string>>
                gdb_emitted_shell_samples;
            std::unordered_map<std::string, size_t>
                gdb_companion_interior_paths;
            std::unordered_map<std::string, size_t>
                gdb_companion_exterior_paths;
            std::unordered_map<std::string, size_t>
                gdb_duplicate_skip_paths;
            size_t gdb_shell_entity_duplicates_skipped = 0;
            size_t gdb_shell_bad_position_skipped = 0;
            std::unordered_set<std::string> gdb_emitted_shell_entity_keys;
            std::unordered_map<std::string, size_t>
                gdb_shell_entity_duplicate_paths;
            std::unordered_map<std::string, size_t>
                gdb_shell_bad_position_paths;
            size_t gdb_shell_path_limit_skipped = 0;
            std::unordered_map<std::string, size_t> gdb_shell_path_emit_counts;
            std::unordered_map<std::string, size_t>
                gdb_shell_path_limit_skip_paths;
            std::unordered_map<std::string, size_t>
                gdb_interest_category_counts;
            std::unordered_map<std::string, size_t>
                gdb_interest_status_counts;
            std::unordered_map<std::string,
                               std::unordered_map<std::string, size_t>>
                gdb_interest_category_status_counts;
            size_t gdb_clocktower_seen = 0;
            size_t gdb_clocktower_emitted = 0;
            size_t gdb_clocktower_companions_emitted = 0;
            std::vector<std::string> gdb_clocktower_audit_lines;
            size_t gdb_shop_seen = 0;
            size_t gdb_shop_emitted = 0;
            size_t gdb_shop_authored_skipped = 0;
            size_t gdb_shop_duplicates = 0;
            size_t gdb_shop_unresolved = 0;
            size_t gdb_shop_hint_only = 0;
            size_t gdb_shop_companions_emitted = 0;
            size_t gdb_shop_companion_misses = 0;
            size_t gdb_shop_companion_invalid_positions = 0;
            size_t gdb_nohash_shell_companions_emitted = 0;
            size_t gdb_nohash_shell_companion_misses = 0;
            size_t gdb_gmd_layout_children_emitted = 0;
            size_t gdb_gmd_layout_children_missing = 0;
            size_t gdb_gmd_layout_sidecars_loaded = 0;
            size_t gdb_gmd_layout_sidecars_missing = 0;
            std::unordered_map<std::string, size_t>
                gdb_gmd_layout_child_paths;
            std::unordered_map<std::string, size_t>
                gdb_gmd_layout_sidecar_sources;
            std::unordered_map<std::string, size_t>
                gdb_shop_companion_paths;
            std::unordered_map<std::string, size_t>
                gdb_nohash_shell_companion_paths;
            std::vector<std::string> gdb_shop_audit_lines;
            struct NoHashShellCandidate {
                Gdb::Placement placement;
                std::string entity_key;
                std::string category;
            };
            std::vector<NoHashShellCandidate> gdb_nohash_shell_candidates;
            std::vector<Level::PropInstance> gdb_generalshop_floor_anchors;
            std::vector<Level::PropInstance> gdb_tavern_pub_anchors;
            struct HouseCompanionAudit {
                size_t skipped = 0;
                size_t exterior_hits = 0;
                size_t exterior_misses = 0;
                size_t interior_hits = 0;
                size_t interior_misses = 0;
                std::string exterior_path;
                std::string interior_path;
                std::vector<std::string> samples;
            };
            std::unordered_map<std::string, HouseCompanionAudit>
                gdb_house_companion_audits;
            std::unordered_set<std::string> gdb_emitted_instance_keys;
            gdb_emitted_instance_keys.reserve(info.placements.size() * 2);
            auto is_market_shop_key = [](const std::string& key) {
                return key.find("largeshop") != std::string::npos ||
                       key.find("smallshop") != std::string::npos ||
                       key.find("generalshop") != std::string::npos ||
                       key.find("generalstore") != std::string::npos ||
                       key.find("tavern") != std::string::npos ||
                       key.find("openstall") != std::string::npos ||
                       key.find("marketstall") != std::string::npos ||
                       key.find("tarotstall") != std::string::npos ||
                       key.find("pub") != std::string::npos ||
                       key.find("inn") != std::string::npos;
            };
            auto is_market_shop_path = [](const std::string& model_path) {
                const std::string p = lower_slash(model_path);
                return p.find("bs_market_largeshop") != std::string::npos ||
                       p.find("bs_market_smallshop") != std::string::npos ||
                       p.find("bs_market_generalshop") != std::string::npos ||
                       p.find("bs_market_tavern") != std::string::npos ||
                       p.find("openstall") != std::string::npos ||
                       p.find("marketstall") != std::string::npos ||
                       p.find("tarotstall") != std::string::npos ||
                       p.find("tavern") != std::string::npos ||
                       p.find("/pub") != std::string::npos ||
                       p.find("_pub") != std::string::npos ||
                       p.find("/inn") != std::string::npos ||
                       p.find("_inn") != std::string::npos;
            };
            auto is_nohash_market_shell_key =
                [](const std::string& entity_key) {
                    return entity_key == "bsmarkettavern" ||
                           entity_key == "generalstore" ||
                           entity_key == "generalstore1";
                };
            auto has_worldish_gdb_position = [](const Gdb::Placement& p) {
                if (!std::isfinite(p.x) || !std::isfinite(p.y) ||
                    !std::isfinite(p.z))
                {
                    return false;
                }
                if (p.x < -64.0f || p.x > 512.0f ||
                    p.y < -64.0f || p.y > 512.0f ||
                    p.z < -64.0f || p.z > 256.0f)
                {
                    return false;
                }
                constexpr float kPiLocal = 3.14159265358979323846f;
                const bool looks_like_rotation_triplet =
                    std::fabs(p.x) < 10.0f &&
                    ((std::fabs(p.y) < 0.02f && std::fabs(p.z) < 0.02f) ||
                     (std::fabs(p.y + kPiLocal) < 0.02f &&
                      std::fabs(p.z + kPiLocal) < 0.02f));
                return !looks_like_rotation_triplet;
            };
            auto make_gdb_prop_instance_no_count =
                [](const Gdb::Placement& p) {
                    Level::PropInstance pi;
                    pi.hash = p.hash_a;
                    pi.values[0] = p.x;
                    pi.values[1] = p.y;
                    pi.values[2] = p.z;
                    const float scale =
                        (std::isfinite(p.scale) && p.scale > 0.01f &&
                         p.scale < 100.0f)
                            ? p.scale : 1.0f;
                    if (p.has_rotation) {
                        fill_gdb_rotation_matrix(
                            pi, p.rot_x, p.rot_y, p.rot_z, scale);
                    } else {
                        const float s_yaw = std::sin(p.yaw);
                        const float c_yaw = std::cos(p.yaw);
                        if (std::isfinite(s_yaw) && std::isfinite(c_yaw)) {
                            pi.values[6] = s_yaw;
                            pi.values[7] = c_yaw;
                        } else {
                            pi.values[6] = 0.0f;
                            pi.values[7] = 1.0f;
                        }
                        pi.values[9] = pi.values[10] = pi.values[11] = scale;
                    }
                    return pi;
                };
            auto classify_gdb_interest =
                [](const std::string& entity_key,
                   const std::string& token,
                   const std::string* model_path) {
                    std::string text = entity_key + " " + token;
                    if (model_path) {
                        text += " ";
                        text += lower_slash(*model_path);
                    }
                    auto has = [&](const char* needle) {
                        return text.find(needle) != std::string::npos;
                    };
                    if (has("openstall") || has("marketstall") ||
                        has("tarotstall") || has("stall"))
                    {
                        return std::string("stall");
                    }
                    if (has("lamp") || has("lantern") ||
                        has("candleholder") || has("candle") ||
                        has("lightfixing") || has("lightceiling") ||
                        has("oillamp") || has("oillantern"))
                    {
                        return std::string("light");
                    }
                    if (has("caravan") || has("coachhouse") ||
                        has("coachouse") || has("coach"))
                    {
                        return std::string("caravan");
                    }
                    if (has("tavern") || has("pub") || has("inn")) {
                        return std::string("tavern");
                    }
                    if (has("generalshop") || has("generalstore") ||
                        has("largeshop") || has("smallshop") ||
                        has("clotheshop") || has("shop"))
                    {
                        return std::string("shop");
                    }
                    if (has("townhouse") || has("slumstreethouse") ||
                        has("shantie") || has("shanty") || has("house"))
                    {
                        return std::string("house");
                    }
                    return std::string();
                };
            auto add_gdb_interest_row =
                [&](const Gdb::Placement& p,
                    const std::string& entity_key,
                    std::string category,
                    const char* status,
                    const std::string& model_path) {
                    if (category.empty() && !model_path.empty()) {
                        const std::string token =
                            canonicalize_for_match(p.entity_name);
                        category = classify_gdb_interest(
                            entity_key, token, &model_path);
                    }
                    if (category.empty()) return;
                    ++gdb_interest_category_counts[category];
                    const std::string status_key = status ? status : "";
                    ++gdb_interest_status_counts[status_key];
                    ++gdb_interest_category_status_counts[category][status_key];
                };

            std::unordered_map<std::string,
                               std::vector<const FlatAssetEntry*>>
                mdl_by_gmd_asset_key;
            mdl_by_gmd_asset_key.reserve(S.all_mdl_files.size());
            for (const auto& m : S.all_mdl_files) {
                const std::string key =
                    compact_match_key(model_name_from_path(m.full_path));
                if (!key.empty()) {
                    mdl_by_gmd_asset_key[key].push_back(&m);
                }
            }
            auto choose_gmd_layout_child_model =
                [&](const std::string& asset_key) {
                    if (asset_key.empty()) {
                        return static_cast<const FlatAssetEntry*>(nullptr);
                    }
                    if (const char* curated =
                            GdbModelHashlist::LookupEntityKey(asset_key))
                    {
                        if (const FlatAssetEntry* hit =
                                resolve_model_by_lower_path(
                                    lower_slash(curated)))
                        {
                            return hit;
                        }
                    }
                    auto choose_best =
                        [&](const std::vector<const FlatAssetEntry*>& hits) {
                            const FlatAssetEntry* best = nullptr;
                            int best_score = INT_MIN;
                            for (const FlatAssetEntry* e : hits) {
                                if (!e) continue;
                                const std::string p =
                                    lower_slash(e->full_path);
                                int score = model_bank_score(e);
                                if (e->from_nested) score += 250;
                                if (p.find("/doors_windows/") !=
                                    std::string::npos)
                                {
                                    score += 400;
                                }
                                if (p.find("/props/") != std::string::npos) {
                                    score += 200;
                                }
                                if (p.find("/buildings/") !=
                                    std::string::npos)
                                {
                                    score += 100;
                                }
                                score -= int(std::min<size_t>(
                                    e->full_path.size(), 240));
                                if (!best || score > best_score) {
                                    best = e;
                                    best_score = score;
                                }
                            }
                            return best;
                        };
                    if (auto it = mdl_by_gmd_asset_key.find(asset_key);
                        it != mdl_by_gmd_asset_key.end())
                    {
                        return choose_best(it->second);
                    }
                    std::vector<const FlatAssetEntry*> fuzzy;
                    for (const auto& kv : mdl_by_gmd_asset_key) {
                        const std::string& model_key = kv.first;
                        if (model_key.size() < 5) continue;
                        if (model_key.find(asset_key) == std::string::npos &&
                            asset_key.find(model_key) == std::string::npos)
                        {
                            continue;
                        }
                        fuzzy.insert(fuzzy.end(),
                                     kv.second.begin(),
                                     kv.second.end());
                    }
                    return choose_best(fuzzy);
                };

            using GmdSidecarHit = std::pair<std::string, int>;
            std::unordered_map<std::string, std::vector<GmdSidecarHit>>
                global_gmd_sidecar_index;
            bool global_gmd_sidecar_index_built = false;
            size_t global_gmd_sidecar_index_bnks = 0;
            auto build_global_gmd_sidecar_index = [&]() {
                if (global_gmd_sidecar_index_built) return;
                global_gmd_sidecar_index_built = true;

                std::vector<std::string> candidate_bnks;
                auto add_unique_bnk = [&](const std::string& path) {
                    if (path.empty()) return;
                    const std::string norm = lower_slash(path);
                    for (const auto& existing : candidate_bnks) {
                        if (lower_slash(existing) == norm) return;
                    }
                    candidate_bnks.push_back(path);
                };
                auto is_streaming_bnk = [](const std::string& path) {
                    std::string p = lower_slash(path);
                    const size_t slash = p.find_last_of('/');
                    const std::string leaf = slash == std::string::npos
                        ? p
                        : p.substr(slash + 1);
                    return leaf.find("streaming") != std::string::npos;
                };
                for (const auto& p : S.bnk_paths) {
                    if (is_streaming_bnk(p)) add_unique_bnk(p);
                }
                for (const auto& p : S.nested_bnk_paths) {
                    if (is_streaming_bnk(p)) add_unique_bnk(p);
                }

                for (const auto& bnk_path : candidate_bnks) {
                    try {
                        BnkCache::Entry& bnk = BnkCache::get(bnk_path);
                        const auto& files = bnk.reader->list_files();
                        bool had_gmd = false;
                        for (size_t i = 0; i < files.size(); ++i) {
                            std::string key = lower_slash(files[i].name);
                            if (key.size() < 8 ||
                                key.compare(key.size() - 8, 8,
                                            ".mdl.gmd") != 0)
                            {
                                continue;
                            }
                            global_gmd_sidecar_index[key].push_back(
                                {bnk_path, static_cast<int>(i)});
                            had_gmd = true;
                        }
                        if (had_gmd) ++global_gmd_sidecar_index_bnks;
                    } catch (...) {
                    }
                }
            };
            auto find_global_gmd_sidecar =
                [&](const std::string& key,
                    const std::string& preferred_model_bnk)
                    -> const GmdSidecarHit* {
                    build_global_gmd_sidecar_index();
                    auto it = global_gmd_sidecar_index.find(lower_slash(key));
                    if (it == global_gmd_sidecar_index.end() ||
                        it->second.empty())
                    {
                        return nullptr;
                    }
                    const std::string preferred =
                        lower_slash(preferred_model_bnk);
                    const bool prefer_globals =
                        preferred.find("/globals/") != std::string::npos ||
                        preferred.find("globals_models.bnk") !=
                            std::string::npos;
                    if (prefer_globals) {
                        for (const auto& hit : it->second) {
                            const std::string bnk = lower_slash(hit.first);
                            if (bnk.find("/globals/") != std::string::npos ||
                                bnk.find("globals_streaming.bnk") !=
                                    std::string::npos)
                            {
                                return &hit;
                            }
                        }
                    }
                    return &it->second.front();
                };

            std::unordered_map<std::string, std::vector<GmdLayoutChild>>
                gmd_layout_child_cache;
            std::unordered_set<std::string> gmd_layout_child_missing;
            auto load_gmd_layout_children_for_model =
                [&](const FlatAssetEntry* model_hit)
                    -> const std::vector<GmdLayoutChild>* {
                    if (!model_hit || model_hit->full_path.empty()) {
                        return static_cast<const std::vector<GmdLayoutChild>*>(
                            nullptr);
                    }
                    const std::string model_lower =
                        lower_slash(model_hit->full_path);
                    if (auto it = gmd_layout_child_cache.find(model_lower);
                        it != gmd_layout_child_cache.end())
                    {
                        return &it->second;
                    }
                    if (gmd_layout_child_missing.find(model_lower) !=
                        gmd_layout_child_missing.end())
                    {
                        return static_cast<const std::vector<GmdLayoutChild>*>(
                            nullptr);
                    }

                    std::vector<uint8_t> bytes;
                    std::string gmd_source_bnk;
                    auto leaf_of_lower_slash_path =
                        [](const std::string& path) {
                            std::string p = lower_slash(path);
                            const size_t slash = p.find_last_of('/');
                            return slash == std::string::npos
                                ? p
                                : p.substr(slash + 1);
                        };
                    auto sibling_with_leaf =
                        [](const std::string& path,
                           const std::string& leaf) {
                            if (path.empty() || leaf.empty()) return std::string();
                            std::string p = path;
                            std::replace(p.begin(), p.end(), '\\', '/');
                            const size_t slash = p.find_last_of('/');
                            if (slash == std::string::npos) return leaf;
                            return p.substr(0, slash + 1) + leaf;
                        };
                    auto add_unique_bnk =
                        [](std::vector<std::string>& out,
                           const std::string& path) {
                            if (path.empty()) return;
                            const std::string norm = lower_slash(path);
                            for (const auto& existing : out) {
                                if (lower_slash(existing) == norm) return;
                            }
                            out.push_back(path);
                        };
                    auto add_virtual_match =
                        [&](std::vector<std::string>& out,
                            const std::string& path) {
                            if (path.empty()) return;
                            std::string p = path;
                            std::replace(p.begin(), p.end(), '\\', '/');
                            std::string low = lower_slash(p);
                            const size_t data_pos = low.find("data/");
                            if (data_pos == std::string::npos) return;
                            if (auto found = find_bnk_by_virtual_path(
                                    p.substr(data_pos)))
                            {
                                add_unique_bnk(out, *found);
                            }
                        };
                    auto derived_streaming_leaf_for_model_bnk =
                        [](const std::string& bnk_path) {
                            const std::string leaf =
                                [&]() {
                                    std::string p = lower_slash(bnk_path);
                                    const size_t slash = p.find_last_of('/');
                                    return slash == std::string::npos
                                        ? p
                                        : p.substr(slash + 1);
                                }();
                            if (leaf == "globals_models.bnk") {
                                return std::string("globals_streaming.bnk");
                            }
                            static constexpr const char* suffix =
                                "_models.bnk";
                            const size_t n = std::strlen(suffix);
                            if (leaf.size() > n &&
                                leaf.compare(leaf.size() - n, n, suffix) == 0)
                            {
                                return leaf.substr(0, leaf.size() - n) +
                                       "_streaming.bnk";
                            }
                            return std::string();
                        };
                    auto add_model_streaming_sidecars =
                        [&](std::vector<std::string>& out,
                            const std::string& model_bnk_path) {
                            const std::string stream_leaf =
                                derived_streaming_leaf_for_model_bnk(
                                    model_bnk_path);
                            if (stream_leaf.empty()) return;

                            const std::string sibling =
                                sibling_with_leaf(
                                    model_bnk_path, stream_leaf);
                            add_unique_bnk(out, sibling);
                            add_virtual_match(out, sibling);

                            auto leaf_matches =
                                [&](const std::string& candidate_path) {
                                    const std::string leaf =
                                        leaf_of_lower_slash_path(
                                            candidate_path);
                                    if (leaf == stream_leaf) return true;
                                    if (leaf.size() <= stream_leaf.size() + 1) {
                                        return false;
                                    }
                                    const size_t off =
                                        leaf.size() - stream_leaf.size();
                                    return leaf.compare(
                                               off,
                                               stream_leaf.size(),
                                               stream_leaf) == 0 &&
                                           leaf[off - 1] == '_';
                                };
                            for (const auto& p : S.bnk_paths) {
                                if (leaf_matches(p)) add_unique_bnk(out, p);
                            }
                            for (const auto& p : S.nested_bnk_paths) {
                                if (leaf_matches(p)) add_unique_bnk(out, p);
                            }
                        };
                    auto try_extract =
                        [&](const std::string& bnk_path,
                            const std::string& key,
                            bool allow_leaf_match) {
                            if (!bytes.empty() || bnk_path.empty() ||
                                key.empty())
                            {
                                return;
                            }
                            int idx = BnkCache::find_index(bnk_path, key);
                            if (idx < 0 && allow_leaf_match) {
                                idx = BnkCache::find_index(
                                    bnk_path, leaf_of_lower_slash_path(key));
                            }
                            if (idx < 0) return;
                            try {
                                bytes = BnkCache::extract_bytes(bnk_path, idx);
                                if (!bytes.empty()) {
                                    gmd_source_bnk = bnk_path;
                                }
                            } catch (...) {
                                bytes.clear();
                                gmd_source_bnk.clear();
                            }
                        };

                    const std::string gmd_key = model_lower + ".gmd";
                    const std::string gmd_leaf =
                        leaf_of_lower_slash_path(gmd_key);
                    const bool generic_leaf =
                        gmd_leaf == "exterior.mdl.gmd" ||
                        gmd_leaf == "interior.mdl.gmd";
                    std::vector<std::string> gmd_sidecar_bnks;
                    add_unique_bnk(gmd_sidecar_bnks, model_hit->bnk_path);
                    add_model_streaming_sidecars(
                        gmd_sidecar_bnks, model_hit->bnk_path);
                    if (auto nested_it =
                            S.nested_bnk_virtual_paths.find(
                                model_hit->bnk_path);
                        nested_it != S.nested_bnk_virtual_paths.end())
                    {
                        add_model_streaming_sidecars(
                            gmd_sidecar_bnks, nested_it->second);
                    }
                    for (const auto& p : g_level_vfs_streaming_bnks) {
                        add_unique_bnk(
                            gmd_sidecar_bnks,
                            resolve_streaming_bnk_path(p));
                    }

                    for (const auto& bnk_path : gmd_sidecar_bnks) {
                        try_extract(bnk_path, gmd_key, false);
                        if (!bytes.empty()) break;
                    }
                    for (const auto& c : streaming_model_candidates) {
                        if (!bytes.empty()) break;
                        if (!c.from_gmd || c.gmd_file_index < 0 ||
                            c.gmd_bnk_path.empty())
                        {
                            continue;
                        }
                        if (c.resolved_lower == model_lower ||
                            c.hint_lower == model_lower)
                        {
                            try {
                                bytes = BnkCache::extract_bytes(
                                    c.gmd_bnk_path, c.gmd_file_index);
                                if (!bytes.empty()) {
                                    gmd_source_bnk = c.gmd_bnk_path;
                                }
                            } catch (...) {
                                bytes.clear();
                                gmd_source_bnk.clear();
                            }
                        }
                    }
                    if (bytes.empty()) {
                        if (const GmdSidecarHit* global_hit =
                                find_global_gmd_sidecar(
                                    gmd_key, model_hit->bnk_path))
                        {
                            try {
                                bytes = BnkCache::extract_bytes(
                                    global_hit->first, global_hit->second);
                                if (!bytes.empty()) {
                                    gmd_source_bnk = global_hit->first;
                                }
                            } catch (...) {
                                bytes.clear();
                                gmd_source_bnk.clear();
                            }
                        }
                    }
                    if (bytes.empty() && !generic_leaf) {
                        for (const auto& bnk_path : gmd_sidecar_bnks) {
                            try_extract(bnk_path, gmd_key, true);
                            if (!bytes.empty()) break;
                        }
                    }

                    if (bytes.empty()) {
                        gmd_layout_child_missing.insert(model_lower);
                        ++gdb_gmd_layout_sidecars_missing;
                        return static_cast<const std::vector<GmdLayoutChild>*>(
                            nullptr);
                    }
                    ++gdb_gmd_layout_sidecars_loaded;
                    if (!gmd_source_bnk.empty()) {
                        ++gdb_gmd_layout_sidecar_sources[gmd_source_bnk];
                    }
                    std::vector<GmdLayoutChild> children =
                        parse_gmd_layout_children(bytes);
                    for (auto& child : children) {
                        if (const FlatAssetEntry* hit =
                                choose_gmd_layout_child_model(
                                    child.asset_key))
                        {
                            child.resolved_path = hit->full_path;
                            child.resolved_key =
                                compact_match_key(
                                    model_name_from_path(hit->full_path));
                        }
                    }
                    auto [it, _] = gmd_layout_child_cache.emplace(
                        model_lower, std::move(children));
                    return &it->second;
                };

            auto should_emit_gmd_layout_child =
                [](const GmdLayoutChild& child) {
                    std::string text =
                        lower_slash(child.raw_path + " " +
                                    child.resolved_path);
                    return text.find("door") != std::string::npos ||
                           text.find("window") != std::string::npos ||
                           text.find("win_") != std::string::npos ||
                           text.find("_win") != std::string::npos ||
                           text.find("sign") != std::string::npos ||
                           text.find("lamp") != std::string::npos ||
                           text.find("lantern") != std::string::npos ||
                           text.find("candle") != std::string::npos ||
                           text.find("light") != std::string::npos;
                };
            auto emit_gmd_layout_children_for_model =
                [&](const FlatAssetEntry* parent_model,
                    const Level::PropInstance& parent_inst) {
                    if (!parent_model) return size_t(0);
                    const std::string parent_lower =
                        lower_slash(parent_model->full_path);
                    const bool shell_parent =
                        parent_lower.find("/exterior.mdl") !=
                            std::string::npos ||
                        parent_lower.find("/interior.mdl") !=
                            std::string::npos ||
                        parent_lower.find("bs_market_tarotstall/") !=
                            std::string::npos;
                    if (!shell_parent) return size_t(0);

                    const std::vector<GmdLayoutChild>* children =
                        load_gmd_layout_children_for_model(parent_model);
                    if (!children || children->empty()) return size_t(0);

                    size_t emitted = 0;
                    const Xform3f parent_xf =
                        prop_instance_xform(parent_inst);
                    for (const auto& child : *children) {
                        if (!should_emit_gmd_layout_child(child)) continue;
                        const FlatAssetEntry* child_model =
                            choose_gmd_layout_child_model(child.asset_key);
                        if (!child_model) {
                            ++gdb_gmd_layout_children_missing;
                            continue;
                        }
                        const Xform3f child_world =
                            xform_compose(parent_xf, child.local);
                        Level::PropInstance child_inst =
                            prop_instance_from_xform(
                                child_world, parent_inst.hash);
                        if (append_prop_instance_for_model(
                                child_model, child_inst))
                        {
                            ++emitted;
                            ++gdb_gmd_layout_children_emitted;
                            ++gdb_gmd_layout_child_paths[
                                child_model->full_path];
                        }
                    }
                    return emitted;
                };
            size_t gdb_render_poll = 0;
            for (const auto& p : info.placements) {
                if ((++gdb_render_poll & 0x7fu) == 0 &&
                    bail_if_cancelled("gdb render placement resolve"))
                {
                    return false;
                }
                if (!emit_gdb_render_placements) continue;
                const bool has_model_hash = p.model_path_hash != 0;
                if (p.entity_name.empty() && !has_model_hash) continue;
                std::string tok = canonicalize_for_match(p.entity_name);
                if (tok.empty() && !has_model_hash) continue;
                const std::string entity_key = gdb_entity_key(p.entity_name);
                std::string gdb_interest_category =
                    classify_gdb_interest(entity_key, tok, nullptr);
                const bool clocktower_audit =
                    p.parent_hash == 0xD55304DB ||
                    entity_key.find("clocktower") != std::string::npos ||
                    tok.find("clocktower") != std::string::npos;
                if (clocktower_audit) {
                    ++gdb_clocktower_seen;
                }
                bool shop_audit = is_market_shop_key(entity_key) ||
                                  is_market_shop_key(tok);
                if (shop_audit) {
                    ++gdb_shop_seen;
                }
                if (is_bwsmarket_level &&
                    p.model_path_hash == 0 &&
                    has_worldish_gdb_position(p) &&
                    is_nohash_market_shell_key(entity_key))
                {
                    gdb_nohash_shell_candidates.push_back(
                        {p, entity_key, gdb_interest_category});
                    add_gdb_interest_row(
                        p, entity_key, gdb_interest_category,
                        "candidate_nohash_shell",
                        std::string());
                    continue;
                }
                if (!p.indexed_record && p.model_path_hash == 0 &&
                    p.parent_hash == 0 &&
                    is_unindexed_shell_fallback_entity(entity_key, tok))
                {
                    add_gdb_interest_row(
                        p, entity_key, gdb_interest_category,
                        "skipped_unindexed_shell_fallback",
                        std::string());
                    if (clocktower_audit &&
                        gdb_clocktower_audit_lines.size() < 8)
                    {
                        gdb_clocktower_audit_lines.push_back(
                            "clocktower audit skipped unindexed shell fallback: " +
                            gdb_shell_sample_text(p, "<no indexed record>"));
                    }
                    if (shop_audit) {
                        ++gdb_shop_unresolved;
                        if (gdb_shop_audit_lines.size() < 12) {
                            gdb_shop_audit_lines.push_back(
                                "shop audit skipped unindexed shell fallback: " +
                                gdb_shell_sample_text(
                                    p, "<no indexed record>"));
                        }
                    }
                    continue;
                }
                auto parent_name_it = parent_match_names.find(p.parent_hash);
                const std::string* parent_match_name =
                    (parent_name_it == parent_match_names.end())
                        ? nullptr : &parent_name_it->second;

                const FlatAssetEntry* hit = nullptr;
                bool matched_model = false;
                bool matched_model_path_hash = false;
                bool hint_only = false;
                if (p.model_path_hash != 0) {
                    hit = resolve_model_by_path_hash(p.model_path_hash);
                    if (hit) {
                        matched_model = true;
                        matched_model_path_hash = true;
                        ++gdb_model_hash_hits;
                    } else {
                        ++gdb_model_hash_misses;
                    }
                }

                if (!matched_model) {
                    const char* curated_path =
                        GdbModelHashlist::LookupParentHash(p.parent_hash);
                    if (!curated_path) {
                        curated_path =
                            GdbModelHashlist::LookupEntityKey(entity_key);
                    }
                    if (curated_path && *curated_path) {
                        hit = resolve_model_by_lower_path(
                            lower_slash(curated_path));
                        if (hit) {
                            matched_model = true;
                        }
                    }
                }

                if (!matched_model && !streaming_model_candidates.empty()) {
                    const StreamingModelCandidate* stream_hit =
                        choose_streaming_cached(
                            p.entity_name, p.parent_hash, nullptr);
                    if (!stream_hit && parent_match_name) {
                        stream_hit = choose_streaming_cached(
                            *parent_match_name, p.parent_hash, nullptr);
                    }
                    if (stream_hit) {
                        matched_model = true;
                        hit = stream_hit->entry;
                        if (!hit) {
                            hint_only = true;
                        }
                    }
                }
                if (!matched_model &&
                    (entity_key == "bsmarkettavern" ||
                     tok.find("bsmarkettavern") != std::string::npos))
                {
                    if (has_worldish_gdb_position(p)) {
                        gdb_nohash_shell_candidates.push_back(
                            {p, "bsmarkettavern", gdb_interest_category});
                    }
                    add_gdb_interest_row(
                        p, entity_key, gdb_interest_category,
                        "skipped_unverified_tavern_shell_guess",
                        std::string());
                    continue;
                }
                if (!matched_model) {
                    hit = resolve_model_for_entity(p.entity_name);
                    if (!hit) {
                        if (is_bwsmarket_level && has_worldish_gdb_position(p) &&
                            (entity_key == "generalstore" ||
                             entity_key == "generalstore1"))
                        {
                            gdb_nohash_shell_candidates.push_back(
                                {p, entity_key, gdb_interest_category});
                        }
                        add_gdb_interest_row(
                            p, entity_key, gdb_interest_category,
                            "unresolved", std::string());
                        if (clocktower_audit &&
                            gdb_clocktower_audit_lines.size() < 8)
                        {
                            gdb_clocktower_audit_lines.push_back(
                                "clocktower audit unresolved: " +
                                gdb_shell_sample_text(p, "<no model>"));
                        }
                        if (shop_audit) {
                            ++gdb_shop_unresolved;
                            if (gdb_shop_audit_lines.size() < 12) {
                                gdb_shop_audit_lines.push_back(
                                    "shop audit unresolved: " +
                                    gdb_shell_sample_text(p, "<no model>"));
                            }
                        }
                        continue;
                    }
                    matched_model = true;
                }

                if (!matched_model) {
                    add_gdb_interest_row(
                        p, entity_key, gdb_interest_category,
                        "unmatched", std::string());
                    continue;
                }
                if (hit) {
                    const std::string path_category =
                        classify_gdb_interest(
                            entity_key, tok, &hit->full_path);
                    if (!path_category.empty()) {
                        gdb_interest_category = path_category;
                    }
                }
                if (hit && is_gdb_static_prop_reject_model(hit->full_path)) {
                    add_gdb_interest_row(
                        p, entity_key, gdb_interest_category,
                        "rejected_non_static_model", hit->full_path);
                    continue;
                }
                if (hit && !matched_model_path_hash &&
                    is_bad_market_helper_substitution(
                        entity_key, tok, hit->full_path))
                {
                    add_gdb_interest_row(
                        p, entity_key, gdb_interest_category,
                        "rejected_helper_substitute", hit->full_path);
                    if (shop_audit) {
                        ++gdb_shop_unresolved;
                        if (gdb_shop_audit_lines.size() < 12) {
                            gdb_shop_audit_lines.push_back(
                                "shop audit rejected helper substitute: " +
                                gdb_shell_sample_text(p, hit->full_path));
                        }
                    }
                    continue;
                }
                ++resolved;
                if (!shop_audit && hit && is_market_shop_path(hit->full_path)) {
                    shop_audit = true;
                    ++gdb_shop_seen;
                }
                if (hint_only || !hit) {
                    add_gdb_interest_row(
                        p, entity_key, gdb_interest_category,
                        "hint_only", hit ? hit->full_path : std::string());
                    ++gdb_hint_only_skipped;
                    if (clocktower_audit &&
                        gdb_clocktower_audit_lines.size() < 8)
                    {
                        gdb_clocktower_audit_lines.push_back(
                            "clocktower audit hint-only: " +
                            gdb_shell_sample_text(p, "<hint only>"));
                    }
                    if (shop_audit) {
                        ++gdb_shop_hint_only;
                        if (gdb_shop_audit_lines.size() < 12) {
                            gdb_shop_audit_lines.push_back(
                                "shop audit hint-only: " +
                                gdb_shell_sample_text(p, "<hint only>"));
                        }
                    }
                    continue;
                }
                const FlatAssetEntry* clocktower_platform_companion = nullptr;
                if (clocktower_audit) {
                    const std::string primary_path = lower_slash(hit->full_path);
                    if (!bwsmarket_has_explicit_clocktower_base_record &&
                        primary_path.find("bs_market_platform") !=
                            std::string::npos)
                    {
                        const char* tower_path =
                            GdbModelHashlist::LookupParentHash(0xD55304DB);
                        const FlatAssetEntry* tower_hit =
                            resolve_model_by_lower_path(
                                lower_slash(tower_path ? tower_path : ""));
                        if (tower_hit) {
                            clocktower_platform_companion = hit;
                            hit = tower_hit;
                        }
                    }
                }
                if (is_gdb_authored_level_shell_model(
                        hit->full_path, authored_level_model_paths))
                {
                    add_gdb_interest_row(
                        p, entity_key, gdb_interest_category,
                        "skipped_authored_shell", hit->full_path);
                    if (clocktower_audit &&
                        gdb_clocktower_audit_lines.size() < 8)
                    {
                        gdb_clocktower_audit_lines.push_back(
                            "clocktower audit skipped as authored shell: " +
                            gdb_shell_sample_text(p, hit->full_path));
                    }
                    if (shop_audit) {
                        ++gdb_shop_authored_skipped;
                        if (gdb_shop_audit_lines.size() < 12) {
                            gdb_shop_audit_lines.push_back(
                                "shop audit skipped as authored shell: " +
                                gdb_shell_sample_text(p, hit->full_path));
                        }
                    }
                    const std::string authored_path = hit->full_path;
                    const std::string shop_companion_exterior =
                        shop_facade_companion_exterior_path(authored_path);
                    if (!shop_companion_exterior.empty()) {
                        ++gdb_authored_shop_companion_skipped;
                        if (shop_audit &&
                            gdb_shop_audit_lines.size() < 12)
                        {
                            gdb_shop_audit_lines.push_back(
                                "shop companion skipped on GDB authored shell duplicate: " +
                                gdb_shell_sample_text(
                                    p, shop_companion_exterior));
                        }
                    }
                    const std::string companion_exterior =
                        house_facade_companion_exterior_path(authored_path);
                    if (!companion_exterior.empty()) {
                        HouseCompanionAudit& audit =
                            gdb_house_companion_audits[authored_path];
                        ++audit.skipped;
                        if (audit.exterior_path.empty()) {
                            audit.exterior_path = companion_exterior;
                        }
                        if (resolve_model_by_lower_path(companion_exterior)) {
                            ++audit.exterior_hits;
                        } else {
                            ++audit.exterior_misses;
                        }
                        const std::string companion_interior =
                            companion_interior_path(companion_exterior);
                        if (audit.interior_path.empty()) {
                            audit.interior_path = companion_interior;
                        }
                        if (!companion_interior.empty() &&
                            resolve_model_by_lower_path(companion_interior)) {
                            ++audit.interior_hits;
                        } else {
                            ++audit.interior_misses;
                        }
                        if (audit.samples.size() < 4) {
                            audit.samples.push_back(
                                gdb_shell_sample_text(p, authored_path));
                        }
                    }
                    ++gdb_authored_shell_skipped;
                    ++gdb_authored_shell_skip_paths[authored_path];
                    auto& samples =
                        gdb_authored_shell_skip_samples[authored_path];
                    if (samples.size() < 4) {
                        samples.push_back(
                            gdb_shell_sample_text(p, authored_path));
                    }
                    continue;
                }

                const std::string matched_lower_path =
                    lower_slash(hit->full_path);
                const bool bwsmarket_tarotstall_shell =
                    is_bwsmarket_level &&
                    matched_lower_path.find("bs_market_tarotstall/") !=
                        std::string::npos &&
                    matched_lower_path.find("bs_market_tarotstall_doors") ==
                        std::string::npos;
                if (bwsmarket_tarotstall_shell &&
                    p.parent_hash == 0 &&
                    p.model_path_hash == 0 &&
                    !p.indexed_record)
                {
                    add_gdb_interest_row(
                        p, entity_key, gdb_interest_category,
                        "skipped_unindexed_tarot_shell_fallback",
                        hit->full_path);
                    ++gdb_duplicate_instances_skipped;
                    ++gdb_duplicate_skip_paths[hit->full_path];
                    continue;
                }

                const bool unique_entity_shell =
                    is_gdb_unique_entity_shell_model(hit->full_path);
                if (unique_entity_shell && !has_worldish_gdb_position(p)) {
                    add_gdb_interest_row(
                        p, entity_key, gdb_interest_category,
                        "skipped_non_world_shell_position", hit->full_path);
                    ++gdb_shell_bad_position_skipped;
                    ++gdb_shell_bad_position_paths[hit->full_path];
                    if (clocktower_audit &&
                        gdb_clocktower_audit_lines.size() < 8)
                    {
                        gdb_clocktower_audit_lines.push_back(
                            "clocktower audit skipped non-world shell position: " +
                            gdb_shell_sample_text(p, hit->full_path));
                    }
                    if (shop_audit) {
                        ++gdb_shop_duplicates;
                        if (gdb_shop_audit_lines.size() < 12) {
                            gdb_shop_audit_lines.push_back(
                                "shop audit skipped non-world shell position: " +
                                gdb_shell_sample_text(p, hit->full_path));
                        }
                    }
                    continue;
                }

                if (unique_entity_shell && !has_model_hash) {
                    std::string shell_entity_key = entity_key;
                    if (shell_entity_key.empty()) {
                        shell_entity_key =
                            p.entity_name.empty()
                                ? std::string()
                                : compact_match_key(p.entity_name);
                    }
                    if (shell_entity_key.empty() && p.hash_a != 0) {
                        shell_entity_key = hex_u32(p.hash_a);
                    }
                    if (!shell_entity_key.empty()) {
                        const std::string shell_key =
                            lower_slash(hit->full_path) + "|" +
                            shell_entity_key;
                        if (!gdb_emitted_shell_entity_keys.insert(
                                shell_key).second)
                        {
                            add_gdb_interest_row(
                                p, entity_key, gdb_interest_category,
                                "skipped_repeated_shell_entity",
                                hit->full_path);
                            ++gdb_shell_entity_duplicates_skipped;
                            ++gdb_shell_entity_duplicate_paths[
                                hit->full_path];
                            if (clocktower_audit &&
                                gdb_clocktower_audit_lines.size() < 8)
                            {
                                gdb_clocktower_audit_lines.push_back(
                                    "clocktower audit skipped repeated shell entity: " +
                                    gdb_shell_sample_text(p, hit->full_path));
                            }
                            if (shop_audit) {
                                ++gdb_shop_duplicates;
                                if (gdb_shop_audit_lines.size() < 12) {
                                    gdb_shop_audit_lines.push_back(
                                        "shop audit skipped repeated shell entity: " +
                                        gdb_shell_sample_text(
                                            p, hit->full_path));
                                }
                            }
                            continue;
                        }
                    }
                }

                const int shell_path_limit =
                    is_bwsmarket_level
                        ? bwsmarket_shell_instance_limit(hit->full_path)
                        : -1;
                const bool has_shell_path_limit = shell_path_limit >= 0;
                const std::string shell_path_count_key =
                    has_shell_path_limit
                        ? lower_slash(hit->full_path)
                        : std::string();
                if (has_shell_path_limit &&
                    gdb_shell_path_emit_counts[shell_path_count_key] >=
                        static_cast<size_t>(shell_path_limit))
                {
                    add_gdb_interest_row(
                        p, entity_key, gdb_interest_category,
                        "skipped_shell_path_cap", hit->full_path);
                    ++gdb_shell_path_limit_skipped;
                    ++gdb_shell_path_limit_skip_paths[hit->full_path];
                    if (shop_audit) {
                        ++gdb_shop_duplicates;
                        if (gdb_shop_audit_lines.size() < 12) {
                            gdb_shop_audit_lines.push_back(
                                "shop audit skipped BWSMarket shell path cap: " +
                                gdb_shell_sample_text(p, hit->full_path));
                        }
                    }
                    continue;
                }

                const std::string instance_key =
                    gdb_instance_key(p, hit->full_path);
                if (!gdb_emitted_instance_keys.insert(instance_key).second) {
                    add_gdb_interest_row(
                        p, entity_key, gdb_interest_category,
                        "skipped_duplicate_gdb_record", hit->full_path);
                    ++gdb_duplicate_instances_skipped;
                    ++gdb_duplicate_skip_paths[hit->full_path];
                    if (clocktower_audit &&
                        gdb_clocktower_audit_lines.size() < 8)
                    {
                        gdb_clocktower_audit_lines.push_back(
                            "clocktower audit skipped duplicate gdb record: " +
                            gdb_shell_sample_text(p, hit->full_path));
                    }
                    if (shop_audit) {
                        ++gdb_shop_duplicates;
                        if (gdb_shop_audit_lines.size() < 12) {
                            gdb_shop_audit_lines.push_back(
                                "shop audit skipped duplicate gdb record: " +
                                gdb_shell_sample_text(p, hit->full_path));
                        }
                    }
                    continue;
                }

                auto& pb = blocks_by_path[hit->full_path];
                if (pb.model_path.empty()) {
                    pb.type = 0xB1;
                    pb.model_path = hit->full_path;
                }

                Level::PropInstance pi;
                pi.hash = p.hash_a;
                pi.values[0] = p.x;
                pi.values[1] = p.y;
                pi.values[2] = p.z;
                const float scale =
                    (std::isfinite(p.scale) && p.scale > 0.01f && p.scale < 100.0f)
                        ? p.scale : 1.0f;
                if (p.has_rotation) {
                    const bool pi_pair_yaw =
                        is_gdb_pi_pair_yaw_rotation(p.rot_y, p.rot_z);
                    if (pi_pair_yaw) {
                        ++gdb_pi_pair_yaw_rotations;
                    }
                    fill_gdb_rotation_matrix(pi, p.rot_x, p.rot_y, p.rot_z, scale);
                    if (pi_pair_yaw) {

                    } else if (std::fabs(p.rot_y) > 1e-4f ||
                               std::fabs(p.rot_z) > 1e-4f) {
                        ++gdb_full_euler_rotations;
                    } else if (std::fabs(p.rot_x) > 1e-4f) {
                        ++gdb_yaw_only_rotations;
                    } else {
                        ++gdb_identity_rotations;
                    }
                } else {
                    const float s_yaw = std::sin(p.yaw);
                    const float c_yaw = std::cos(p.yaw);
                    if (std::isfinite(s_yaw) && std::isfinite(c_yaw)) {
                        pi.values[6] = s_yaw;
                        pi.values[7] = c_yaw;
                    } else {
                        pi.values[6] = 0.0f;
                        pi.values[7] = 1.0f;
                    }
                    pi.values[9] = pi.values[10] = pi.values[11] = scale;
                    if (std::fabs(p.yaw) > 1e-4f) {
                        ++gdb_yaw_only_rotations;
                    } else {
                        ++gdb_identity_rotations;
                    }
                }
                if (!emitted_prop_transform_keys.insert(
                        prop_instance_transform_key(pi, hit->full_path)).second)
                {
                    add_gdb_interest_row(
                        p, entity_key, gdb_interest_category,
                        "skipped_existing_prop_transform", hit->full_path);
                    ++gdb_duplicate_instances_skipped;
                    ++gdb_duplicate_skip_paths[hit->full_path];
                    if (clocktower_audit &&
                        gdb_clocktower_audit_lines.size() < 8)
                    {
                        gdb_clocktower_audit_lines.push_back(
                            "clocktower audit skipped existing prop transform: " +
                            gdb_shell_sample_text(p, hit->full_path));
                    }
                    if (shop_audit) {
                        ++gdb_shop_duplicates;
                        if (gdb_shop_audit_lines.size() < 12) {
                            gdb_shop_audit_lines.push_back(
                                "shop audit skipped existing prop transform: " +
                                gdb_shell_sample_text(p, hit->full_path));
                        }
                    }
                    continue;
                }
                pb.instances.push_back(pi);
                emit_gmd_layout_children_for_model(hit, pi);
                add_gdb_interest_row(
                    p, entity_key, gdb_interest_category,
                    "emitted", hit->full_path);
                if (is_bwsmarket_level) {
                    const std::string emitted_path =
                        lower_slash(hit->full_path);
                    if (emitted_path.find(
                            "bs_market_generalshop_stairs_floor") !=
                        std::string::npos)
                    {
                        gdb_generalshop_floor_anchors.push_back(pi);
                    }
                    if (emitted_path.find("esa_table_tavern") !=
                        std::string::npos)
                    {
                        gdb_tavern_pub_anchors.push_back(pi);
                    }
                }
                if (has_shell_path_limit) {
                    ++gdb_shell_path_emit_counts[shell_path_count_key];
                }
                if (clocktower_audit) {
                    ++gdb_clocktower_emitted;
                    if (gdb_clocktower_audit_lines.size() < 8) {
                        gdb_clocktower_audit_lines.push_back(
                            "clocktower audit emitted: " +
                            gdb_shell_sample_text(p, hit->full_path));
                    }
                    const std::string primary_path =
                        lower_slash(hit->full_path);
                    const bool primary_is_clocktower_base =
                        primary_path.find(
                            "bs_market_clocktower/"
                            "bs_market_clocktower.mdl") !=
                            std::string::npos;
                    const bool primary_is_platform =
                        primary_path.find(
                            "bs_market_platform/"
                            "bs_market_platform.mdl") !=
                            std::string::npos;
                    const bool clocktower_base_primary =
                        primary_is_clocktower_base ||
                        (primary_is_platform &&
                         !bwsmarket_has_explicit_clocktower_base_record);
                    if (clocktower_base_primary) {
                        if (clocktower_platform_companion) {
                            if (append_prop_instance_for_model(
                                    clocktower_platform_companion, pi))
                            {
                                ++gdb_clocktower_companions_emitted;
                                if (gdb_clocktower_audit_lines.size() < 8) {
                                    gdb_clocktower_audit_lines.push_back(
                                        "clocktower platform companion emitted: " +
                                        gdb_shell_sample_text(
                                            p,
                                            clocktower_platform_companion->full_path));
                                }
                            }
                        } else if (!bwsmarket_has_explicit_clocktower_base_record &&
                                   primary_path.find("bs_market_clocktower") ==
                                       std::string::npos)
                        {
                            const char* tower_path =
                                GdbModelHashlist::LookupParentHash(0xD55304DB);
                            const FlatAssetEntry* tower_hit =
                                resolve_model_by_lower_path(
                                    lower_slash(tower_path ? tower_path : ""));
                            if (append_prop_instance_for_model(tower_hit, pi)) {
                                ++gdb_clocktower_companions_emitted;
                                if (gdb_clocktower_audit_lines.size() < 8) {
                                    gdb_clocktower_audit_lines.push_back(
                                        "clocktower companion emitted: " +
                                        gdb_shell_sample_text(
                                            p, tower_hit->full_path));
                                }
                            } else if (!tower_hit &&
                                       gdb_clocktower_audit_lines.size() < 8)
                            {
                                gdb_clocktower_audit_lines.push_back(
                                    "clocktower companion unresolved: " +
                                    gdb_shell_sample_text(
                                        p, tower_path ? tower_path : "<no path>"));
                            }
                        }

                        const std::array<const char*, 3> clocktower_parts = {
                            "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_ClockTower_Cogs\\BS_Market_ClockTower_Cogs.mdl",
                            "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_ClockTower_HourHand\\BS_Market_ClockTower_HourHand.mdl",
                            "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_ClockTower_MinuteHand\\BS_Market_ClockTower_MinuteHand.mdl",
                        };
                        for (const char* part_path : clocktower_parts) {
                            const std::string part_lower =
                                lower_slash(part_path);
                            if (primary_path == part_lower) {
                                continue;
                            }
                            const FlatAssetEntry* part_hit =
                                resolve_model_by_lower_path(part_lower);
                            if (append_prop_instance_for_model(part_hit, pi)) {
                                ++gdb_clocktower_companions_emitted;
                                if (gdb_clocktower_audit_lines.size() < 8) {
                                    gdb_clocktower_audit_lines.push_back(
                                        "clocktower part emitted: " +
                                        gdb_shell_sample_text(
                                            p, part_hit->full_path));
                                }
                            } else if (!part_hit &&
                                       gdb_clocktower_audit_lines.size() < 8)
                            {
                                gdb_clocktower_audit_lines.push_back(
                                    "clocktower part unresolved: " +
                                    gdb_shell_sample_text(p, part_path));
                            }
                        }
                    }
                }
                if (shop_audit) {
                    ++gdb_shop_emitted;
                    if (gdb_shop_audit_lines.size() < 12) {
                        gdb_shop_audit_lines.push_back(
                            "shop audit emitted: " +
                            gdb_shell_sample_text(p, hit->full_path));
                    }
                }
                std::vector<std::pair<std::string, bool>>
                    shell_companion_paths = {
                        {companion_interior_path(hit->full_path), true},
                        {companion_exterior_path(hit->full_path), false},
                    };
                if (is_bwsslums_level) {
                    const std::string house_exterior =
                        house_facade_companion_exterior_path(hit->full_path);
                    if (!house_exterior.empty()) {
                        shell_companion_paths.push_back(
                            {house_exterior, false});
                        shell_companion_paths.push_back(
                            {companion_interior_path(house_exterior), true});
                    }
                }
                for (const auto& companion : shell_companion_paths) {
                    if (companion.first.empty()) continue;
                    const FlatAssetEntry* companion_hit =
                        resolve_model_by_lower_path(companion.first);
                    if (!companion_hit) continue;
                    auto& companion_pb =
                        blocks_by_path[companion_hit->full_path];
                    if (companion_pb.model_path.empty()) {
                        companion_pb.type = 0xB1;
                        companion_pb.model_path = companion_hit->full_path;
                    }
                    if (emitted_prop_transform_keys.insert(
                            prop_instance_transform_key(
                                pi, companion_hit->full_path)).second)
                    {
                        companion_pb.instances.push_back(pi);
                        emit_gmd_layout_children_for_model(companion_hit, pi);
                        if (companion.second) {
                            ++gdb_companion_interiors_emitted;
                            ++gdb_companion_interior_paths[
                                companion_hit->full_path];
                        } else {
                            ++gdb_companion_exteriors_emitted;
                            ++gdb_companion_exterior_paths[
                                companion_hit->full_path];
                        }
                    }
                }
                if (is_gdb_shell_audit_model(hit->full_path)) {
                    ++gdb_emitted_shell_paths[hit->full_path];
                    auto& samples = gdb_emitted_shell_samples[hit->full_path];
                    if (samples.size() < 4) {
                        samples.push_back(
                            gdb_shell_sample_text(p, hit->full_path));
                    }
                }
                ++gdb_instances_emitted;
            }

            if (is_bwsmarket_level) {
                struct WorldAnchor {
                    std::string model_path;
                    std::string key;
                    Xform3f xf;
                };
                std::unordered_map<std::string, std::vector<WorldAnchor>>
                    world_anchors_by_key;
                auto add_world_anchor =
                    [&](const std::string& model_path,
                        const Level::PropInstance& inst) {
                        const std::string key =
                            compact_match_key(model_name_from_path(model_path));
                        if (key.empty()) return;
                        world_anchors_by_key[key].push_back(
                            {model_path, key, prop_instance_xform(inst)});
                    };
                for (const auto& block : level_prop_blocks) {
                    for (const auto& inst : block.instances) {
                        add_world_anchor(block.model_path, inst);
                    }
                }
                for (const auto& kv : blocks_by_path) {
                    const auto& block = kv.second;
                    for (const auto& inst : block.instances) {
                        add_world_anchor(block.model_path, inst);
                    }
                }

                std::unordered_map<std::string,
                                   std::vector<const FlatAssetEntry*>>
                    mdl_by_asset_key;
                mdl_by_asset_key.reserve(S.all_mdl_files.size());
                for (const auto& m : S.all_mdl_files) {
                    const std::string key =
                        compact_match_key(model_name_from_path(m.full_path));
                    if (!key.empty()) {
                        mdl_by_asset_key[key].push_back(&m);
                    }
                }
                auto choose_model_for_gmd_asset =
                    [&](const std::string& key) {
                    if (key.empty()) {
                        return static_cast<const FlatAssetEntry*>(nullptr);
                    }
                    auto choose_best =
                        [](const std::vector<const FlatAssetEntry*>& hits) {
                        const FlatAssetEntry* best = nullptr;
                        int best_score = INT_MIN;
                        for (const FlatAssetEntry* e : hits) {
                            if (!e) continue;
                            int score = 0;
                            const std::string p = lower_slash(e->full_path);
                            if (p.find("/globals_models.bnk") ==
                                std::string::npos)
                            {
                                score += 500;
                            }
                            if (e->from_nested) score += 250;
                            if (p.find("/doors_windows/") !=
                                std::string::npos)
                            {
                                score += 200;
                            }
                            if (p.find("/props/") != std::string::npos) {
                                score += 100;
                            }
                            score -= int(std::min<size_t>(
                                e->full_path.size(), 240));
                            if (!best || score > best_score) {
                                best = e;
                                best_score = score;
                            }
                        }
                        return best;
                    };
                    if (auto it = mdl_by_asset_key.find(key);
                        it != mdl_by_asset_key.end())
                    {
                        return choose_best(it->second);
                    }
                    std::vector<const FlatAssetEntry*> fuzzy;
                    for (const auto& kv : mdl_by_asset_key) {
                        const std::string& mk = kv.first;
                        if (mk.size() < 5) continue;
                        if (mk.find(key) == std::string::npos &&
                            key.find(mk) == std::string::npos)
                        {
                            continue;
                        }
                        fuzzy.insert(fuzzy.end(),
                                     kv.second.begin(),
                                     kv.second.end());
                    }
                    return choose_best(fuzzy);
                };

                auto shell_candidate_for_path =
                    [&](const char* exterior_path) {
                    const std::string target = lower_slash(exterior_path);
                    const StreamingModelCandidate* best = nullptr;
                    for (const auto& c : streaming_model_candidates) {
                        if (!c.from_gmd || c.gmd_file_index < 0 ||
                            c.gmd_bnk_path.empty())
                        {
                            continue;
                        }
                        if (c.hint_lower == target ||
                            c.resolved_lower == target)
                        {
                            return &c;
                        }
                        if (c.hint_lower.size() > target.size() &&
                            c.hint_lower.compare(
                                c.hint_lower.size() - target.size(),
                                target.size(), target) == 0)
                        {
                            best = &c;
                        }
                    }
                    return best;
                };

                struct GmdShellSolution {
                    Xform3f xf;
                    int matches = 0;
                    int distinct_matches = 0;
                    float error = 0.0f;
                    std::string seed;
                };
                auto is_distinct_anchor_key =
                    [&](const std::string& key) {
                    auto it = world_anchors_by_key.find(key);
                    const size_t count =
                        (it == world_anchors_by_key.end())
                            ? 0 : it->second.size();
                    return count <= 16 ||
                           key.find("sign") != std::string::npos ||
                           key.find("door") != std::string::npos ||
                           key.find("counter") != std::string::npos ||
                           key.find("stairs") != std::string::npos ||
                           key.find("tarot") != std::string::npos;
                };
                auto plausible_shell_xform =
                    [](const Xform3f& xf) {
                    return std::isfinite(xf.t.x) &&
                           std::isfinite(xf.t.y) &&
                           std::isfinite(xf.t.z) &&
                           xf.t.x >= -96.0f && xf.t.x <= 512.0f &&
                           xf.t.z >= -96.0f && xf.t.z <= 512.0f &&
                           xf.t.y >= -96.0f && xf.t.y <= 256.0f;
                };
                auto score_gmd_shell_seed =
                    [&](const Xform3f& seed,
                        const std::vector<GmdLayoutChild>& children,
                        const std::string& seed_label) {
                    GmdShellSolution sol;
                    sol.xf = seed;
                    sol.seed = seed_label;
                    if (!plausible_shell_xform(seed)) {
                        sol.error = std::numeric_limits<float>::infinity();
                        return sol;
                    }
                    for (const auto& child : children) {
                        if (child.resolved_key.empty()) continue;
                        auto it =
                            world_anchors_by_key.find(child.resolved_key);
                        if (it == world_anchors_by_key.end() ||
                            it->second.empty())
                        {
                            continue;
                        }
                        const Vec3f predicted =
                            xform_apply_point(seed, child.local.t);
                        float best_d2 =
                            std::numeric_limits<float>::infinity();
                        for (const auto& anchor : it->second) {
                            const float d2 =
                                vec3_len2(vec3_sub(predicted, anchor.xf.t));
                            if (d2 < best_d2) best_d2 = d2;
                        }
                        if (!std::isfinite(best_d2)) continue;
                        const float d = std::sqrt(best_d2);
                        if (d <= 2.5f) {
                            ++sol.matches;
                            sol.error += d;
                            if (is_distinct_anchor_key(child.resolved_key)) {
                                ++sol.distinct_matches;
                            }
                        }
                    }
                    return sol;
                };
                auto solve_gmd_shell =
                    [&](const char* exterior_path,
                        const char* label,
                        size_t max_solutions) {
                    std::vector<GmdShellSolution> selected;
                    const StreamingModelCandidate* cand =
                        shell_candidate_for_path(exterior_path);
                    if (!cand) {
                        return selected;
                    }

                    std::vector<uint8_t> bytes;
                    try {
                        bytes = BnkCache::extract_bytes(
                            cand->gmd_bnk_path, cand->gmd_file_index);
                    } catch (...) {
                    }
                    std::vector<GmdLayoutChild> children =
                        parse_gmd_layout_children(bytes);
                    for (auto& child : children) {
                        if (const FlatAssetEntry* hit =
                                choose_model_for_gmd_asset(child.asset_key))
                        {
                            child.resolved_path = hit->full_path;
                            child.resolved_key =
                                compact_match_key(
                                    model_name_from_path(hit->full_path));
                        }
                    }

                    std::vector<GmdShellSolution> scored;
                    for (const auto& child : children) {
                        if (child.resolved_key.empty()) continue;
                        auto it =
                            world_anchors_by_key.find(child.resolved_key);
                        if (it == world_anchors_by_key.end() ||
                            it->second.empty())
                        {
                            continue;
                        }
                        Xform3f local_inv;
                        if (!xform_inverse(child.local, local_inv)) {
                            continue;
                        }
                        const bool distinct =
                            is_distinct_anchor_key(child.resolved_key);
                        const size_t max_seed_count = distinct ? 512 : 96;
                        const size_t stride =
                            (it->second.size() > max_seed_count)
                                ? std::max<size_t>(
                                      1, it->second.size() / max_seed_count)
                                : 1;
                        size_t used = 0;
                        for (size_t i = 0; i < it->second.size();
                             i += stride)
                        {
                            if (used++ >= max_seed_count) break;
                            const auto& anchor = it->second[i];
                            const Xform3f seed =
                                xform_compose(anchor.xf, local_inv);
                            const std::string seed_label =
                                child.resolved_key + " -> " +
                                anchor.model_path;
                            GmdShellSolution sol =
                                score_gmd_shell_seed(
                                    seed, children, seed_label);
                            if (sol.matches <= 0) continue;
                            scored.push_back(std::move(sol));
                        }
                    }

                    std::sort(scored.begin(), scored.end(),
                              [](const auto& a, const auto& b) {
                                  const int as = a.distinct_matches * 2000 +
                                                 a.matches * 1000;
                                  const int bs = b.distinct_matches * 2000 +
                                                 b.matches * 1000;
                                  if (as != bs) return as > bs;
                                  return a.error < b.error;
                              });

                    for (const auto& sol : scored) {
                        if (sol.matches < 2 && sol.distinct_matches < 1) {
                            continue;
                        }
                        bool duplicate = false;
                        for (const auto& prev : selected) {
                            const Vec3f d =
                                vec3_sub(sol.xf.t, prev.xf.t);
                            if (d.x * d.x + d.z * d.z < 16.0f &&
                                std::fabs(d.y) < 6.0f)
                            {
                                duplicate = true;
                                break;
                            }
                        }
                        if (duplicate) continue;
                        selected.push_back(sol);
                        if (selected.size() >= max_solutions) break;
                    }

                    return selected;
                };

                auto emit_gmd_solved_shells =
                    [&](const char* exterior_path,
                        const char* label,
                        size_t max_solutions) {
                    size_t emitted = 0;
                    std::vector<GmdShellSolution> solutions =
                        solve_gmd_shell(exterior_path, label, max_solutions);
                    const std::string exterior_lower =
                        lower_slash(exterior_path);
                    const std::array<std::string, 2> shell_paths = {
                        exterior_lower,
                        companion_interior_path(exterior_lower),
                    };
                    for (const auto& sol : solutions) {
                        const Level::PropInstance shell_pi =
                            prop_instance_from_xform(sol.xf);
                        bool emitted_any = false;
                        for (const std::string& shell_path : shell_paths) {
                            if (shell_path.empty()) continue;
                            if (append_prop_instance_for_model_path(
                                    shell_path, shell_pi))
                            {
                                emitted_any = true;
                                ++gdb_nohash_shell_companions_emitted;
                                ++gdb_nohash_shell_companion_paths[shell_path];
                            } else if (!resolve_model_by_lower_path(
                                           shell_path))
                            {
                                ++gdb_nohash_shell_companion_misses;
                            }
                        }
                        if (emitted_any) {
                            ++emitted;
                            ++gdb_instances_emitted;
                        }
                    }
                    return emitted;
                };

                constexpr bool enable_gmd_shell_solve = false;
                const size_t solved_general =
                    enable_gmd_shell_solve
                        ? emit_gmd_solved_shells(
                              "art/environment/regions/bowerstone/buildings/"
                              "dotxsi/bs_market_generalshop/"
                              "bs_market_generalshop/exterior.mdl",
                              "generalshop",
                              2)
                        : 0;
                const size_t solved_tavern =
                    enable_gmd_shell_solve
                        ? emit_gmd_solved_shells(
                              "art/environment/regions/bowerstone/buildings/"
                              "dotxsi/bs_market_tavern/"
                              "bs_market_tavern/exterior.mdl",
                              "tavern",
                              1)
                        : 0;
                if (solved_general > 0 || solved_tavern > 0) {
                    OutputLog::info(
                        "GDB .gmd shell solve: generalshop=" +
                        std::to_string(solved_general) +
                        ", tavern=" +
                        std::to_string(solved_tavern));
                }

            }

            const bool allow_legacy_nohash_shell_anchor = false;
            if (allow_legacy_nohash_shell_anchor &&
                is_bwsmarket_level && !gdb_nohash_shell_candidates.empty()) {
                std::unordered_set<size_t> used_nohash_shell_candidates;
                auto find_nearest_nohash_shell =
                    [&](const std::initializer_list<const char*> entity_keys,
                        float x,
                        float y,
                        float z,
                        float max_dist,
                        float max_dz,
                        bool prefer_parent_backed) -> size_t {
                        size_t best = static_cast<size_t>(-1);
                        float best_score =
                            std::numeric_limits<float>::infinity();
                        auto consider = [&](bool require_parent_backed) {
                            for (size_t i = 0;
                                 i < gdb_nohash_shell_candidates.size(); ++i)
                            {
                                if (used_nohash_shell_candidates.find(i) !=
                                    used_nohash_shell_candidates.end())
                                {
                                    continue;
                                }
                                const NoHashShellCandidate& c =
                                    gdb_nohash_shell_candidates[i];
                                if (require_parent_backed &&
                                    c.placement.parent_hash == 0)
                                {
                                    continue;
                                }
                                bool key_match = false;
                                for (const char* key : entity_keys) {
                                    if (c.entity_key == key) {
                                        key_match = true;
                                        break;
                                    }
                                }
                                if (!key_match) continue;
                                const float dx = c.placement.x - x;
                                const float dy = c.placement.y - y;
                                const float dz = c.placement.z - z;
                                if (std::fabs(dz) > max_dz) continue;
                                const float dxy2 = dx * dx + dy * dy;
                                if (dxy2 > max_dist * max_dist) continue;
                                const float score = dxy2 + dz * dz * 9.0f;
                                if (score < best_score) {
                                    best_score = score;
                                    best = i;
                                }
                            }
                        };
                        if (prefer_parent_backed) {
                            consider(true);
                        }
                        if (best == static_cast<size_t>(-1)) {
                            consider(false);
                        }
                        return best;
                    };
                auto nohash_shell_placement_with_rotation =
                    [&](const NoHashShellCandidate& candidate) {
                        Gdb::Placement placement = candidate.placement;
                        if (placement.has_rotation) return placement;

                        float best_d2 = 64.0f;
                        const NoHashShellCandidate* best = nullptr;
                        for (const NoHashShellCandidate& other :
                             gdb_nohash_shell_candidates)
                        {
                            if (other.entity_key != candidate.entity_key ||
                                !other.placement.has_rotation)
                            {
                                continue;
                            }
                            const float dx =
                                other.placement.x - placement.x;
                            const float dy =
                                other.placement.y - placement.y;
                            const float d2 = dx * dx + dy * dy;
                            if (d2 < best_d2) {
                                best_d2 = d2;
                                best = &other;
                            }
                        }
                        if (best) {
                            placement.rot_x = best->placement.rot_x;
                            placement.rot_y = best->placement.rot_y;
                            placement.rot_z = best->placement.rot_z;
                            placement.yaw = best->placement.yaw;
                            placement.has_rotation = true;
                        }
                        return placement;
                    };
                auto emit_nohash_shell_pair =
                    [&](const NoHashShellCandidate& candidate,
                        const char* exterior_path) {
                        if (!exterior_path || !*exterior_path) return;
                        const Gdb::Placement shell_placement =
                            nohash_shell_placement_with_rotation(candidate);
                        const Level::PropInstance shell_pi =
                            make_gdb_prop_instance_no_count(
                                shell_placement);
                        const std::string exterior_lower =
                            lower_slash(exterior_path);
                        std::array<std::string, 2> shell_paths = {
                            exterior_lower,
                            companion_interior_path(exterior_lower),
                        };
                        bool emitted_any = false;
                        for (const std::string& shell_path : shell_paths) {
                            if (shell_path.empty()) continue;
                            const FlatAssetEntry* shell_hit =
                                resolve_model_by_lower_path(shell_path);
                            if (append_prop_instance_for_model(
                                    shell_hit, shell_pi))
                            {
                                emitted_any = true;
                                emit_gmd_layout_children_for_model(
                                    shell_hit, shell_pi);
                                ++gdb_nohash_shell_companions_emitted;
                                ++gdb_nohash_shell_companion_paths[shell_path];
                                add_gdb_interest_row(
                                    shell_placement,
                                    candidate.entity_key,
                                    candidate.category,
                                    "emitted_nohash_shell_companion",
                                    shell_path);
                            } else if (!shell_hit)
                            {
                                ++gdb_nohash_shell_companion_misses;
                            }
                        }
                        if (emitted_any) {
                            ++gdb_instances_emitted;
                        }
                    };

                for (const auto& anchor : gdb_generalshop_floor_anchors) {
                    const size_t idx = find_nearest_nohash_shell(
                        {"generalstore", "generalstore1"},
                        anchor.values[0], anchor.values[1], anchor.values[2],
                        18.0f, 5.0f, true);
                    if (idx == static_cast<size_t>(-1)) continue;
                    used_nohash_shell_candidates.insert(idx);
                    emit_nohash_shell_pair(
                        gdb_nohash_shell_candidates[idx],
                        "art/environment/regions/bowerstone/buildings/"
                        "dotxsi/bs_market_generalshop/"
                        "bs_market_generalshop/exterior.mdl");
                }

                if (!gdb_tavern_pub_anchors.empty()) {
                    float tavern_x = 0.0f;
                    float tavern_y = 0.0f;
                    float tavern_z = 0.0f;
                    size_t tavern_anchor_count = 0;
                    for (const auto& anchor : gdb_tavern_pub_anchors) {
                        if (anchor.values[2] > 43.0f) continue;
                        tavern_x += anchor.values[0];
                        tavern_y += anchor.values[1];
                        tavern_z += anchor.values[2];
                        ++tavern_anchor_count;
                    }
                    if (tavern_anchor_count == 0) {
                        for (const auto& anchor : gdb_tavern_pub_anchors) {
                            tavern_x += anchor.values[0];
                            tavern_y += anchor.values[1];
                            tavern_z += anchor.values[2];
                            ++tavern_anchor_count;
                        }
                    }
                    tavern_x /= static_cast<float>(tavern_anchor_count);
                    tavern_y /= static_cast<float>(tavern_anchor_count);
                    tavern_z /= static_cast<float>(tavern_anchor_count);
                    const size_t idx = find_nearest_nohash_shell(
                        {"bsmarkettavern"}, tavern_x, tavern_y, tavern_z,
                        25.0f, 4.0f, false);
                    if (idx != static_cast<size_t>(-1)) {
                        used_nohash_shell_candidates.insert(idx);
                        emit_nohash_shell_pair(
                            gdb_nohash_shell_candidates[idx],
                            "art/environment/regions/bowerstone/buildings/"
                            "dotxsi/bs_market_tavern/"
                            "bs_market_tavern/exterior.mdl");
                    }
                }
            }

            std::ostringstream os3;
            os3 << "gdb-derived placements: "
                << resolved << " entities matched a model";
            if (gdb_instances_emitted > 0) {
                os3 << ", emitted " << gdb_instances_emitted
                    << " instance(s)";
                OutputLog::success(os3.str());
                OutputLog::info(
                    "gdb-derived rotations: full-euler=" +
                    std::to_string(gdb_full_euler_rotations) +
                    ", yaw-only=" +
                    std::to_string(gdb_yaw_only_rotations) +
                    ", identity=" +
                    std::to_string(gdb_identity_rotations) +
                    ", pi-pair-full=" +
                    std::to_string(gdb_pi_pair_yaw_rotations));
                if (gdb_model_hash_hits > 0 || gdb_model_hash_misses > 0) {
                    OutputLog::info(
                        "gdb-derived model path hashes: hit=" +
                        std::to_string(gdb_model_hash_hits) +
                        ", miss=" +
                        std::to_string(gdb_model_hash_misses));
                }
                if (gdb_hint_only_skipped > 0) {
                    OutputLog::info(
                        "gdb-derived skipped after model match: hint-only=" +
                        std::to_string(gdb_hint_only_skipped));
                }
                if (gdb_shop_companions_emitted > 0) {
                    OutputLog::info(
                        "gdb shop companions: " +
                        std::to_string(gdb_shop_companions_emitted) +
                        " instance(s) across " +
                        std::to_string(gdb_shop_companion_paths.size()) +
                        " model(s)");
                    std::vector<std::pair<std::string, size_t>> shop_paths(
                        gdb_shop_companion_paths.begin(),
                        gdb_shop_companion_paths.end());
                    std::sort(shop_paths.begin(), shop_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(shop_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  shop companion: " +
                            std::to_string(shop_paths[i].second) +
                            "x  " + shop_paths[i].first);
                    }
                }
                if (gdb_nohash_shell_companions_emitted > 0 ||
                    gdb_nohash_shell_companion_misses > 0)
                {
                    OutputLog::info(
                        "gdb nohash shell companions: emitted " +
                        std::to_string(
                            gdb_nohash_shell_companions_emitted) +
                        " instance(s) across " +
                        std::to_string(
                            gdb_nohash_shell_companion_paths.size()) +
                        " model(s), missing-path " +
                        std::to_string(
                            gdb_nohash_shell_companion_misses));
                    std::vector<std::pair<std::string, size_t>> nohash_paths(
                        gdb_nohash_shell_companion_paths.begin(),
                        gdb_nohash_shell_companion_paths.end());
                    std::sort(nohash_paths.begin(), nohash_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(nohash_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  nohash shell companion: " +
                            std::to_string(nohash_paths[i].second) +
                            "x  " + nohash_paths[i].first);
                    }
                }
                if (gdb_gmd_layout_children_emitted > 0 ||
                    gdb_gmd_layout_children_missing > 0)
                {
                    OutputLog::info(
                        "gdb .gmd layout children: emitted " +
                        std::to_string(gdb_gmd_layout_children_emitted) +
                        " instance(s) across " +
                        std::to_string(gdb_gmd_layout_child_paths.size()) +
                        " model(s), unresolved " +
                        std::to_string(gdb_gmd_layout_children_missing));
                    std::vector<std::pair<std::string, size_t>> child_paths(
                        gdb_gmd_layout_child_paths.begin(),
                        gdb_gmd_layout_child_paths.end());
                    std::sort(child_paths.begin(), child_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(child_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  .gmd child: " +
                            std::to_string(child_paths[i].second) +
                            "x  " + child_paths[i].first);
                    }
                }
                if (gdb_gmd_layout_sidecars_loaded > 0 ||
                    gdb_gmd_layout_sidecars_missing > 0)
                {
                    OutputLog::info(
                        "gdb .gmd sidecars: loaded " +
                        std::to_string(gdb_gmd_layout_sidecars_loaded) +
                        ", missing " +
                        std::to_string(gdb_gmd_layout_sidecars_missing));
                    if (global_gmd_sidecar_index_built) {
                        OutputLog::info(
                            "gdb .gmd global index: " +
                            std::to_string(
                                global_gmd_sidecar_index.size()) +
                            " exact sidecar path(s) across " +
                            std::to_string(
                                global_gmd_sidecar_index_bnks) +
                            " streaming BNK(s)");
                    }
                    std::vector<std::pair<std::string, size_t>> sources(
                        gdb_gmd_layout_sidecar_sources.begin(),
                        gdb_gmd_layout_sidecar_sources.end());
                    std::sort(sources.begin(), sources.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n = std::min<size_t>(sources.size(), 6);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  .gmd source: " +
                            std::to_string(sources[i].second) +
                            "x  " + sources[i].first);
                    }
                }
                if (gdb_shell_bad_position_skipped > 0) {
                    OutputLog::info(
                        "gdb-derived skipped non-world shell positions: " +
                        std::to_string(gdb_shell_bad_position_skipped));
                    std::vector<std::pair<std::string, size_t>> bad_paths(
                        gdb_shell_bad_position_paths.begin(),
                        gdb_shell_bad_position_paths.end());
                    std::sort(bad_paths.begin(), bad_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(bad_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  skip non-world shell: " +
                            std::to_string(bad_paths[i].second) +
                            "x  " + bad_paths[i].first);
                    }
                }
                if (gdb_shell_entity_duplicates_skipped > 0) {
                    OutputLog::info(
                        "gdb-derived skipped repeated shell entity/model records: " +
                        std::to_string(
                            gdb_shell_entity_duplicates_skipped));
                    std::vector<std::pair<std::string, size_t>> shell_dups(
                        gdb_shell_entity_duplicate_paths.begin(),
                        gdb_shell_entity_duplicate_paths.end());
                    std::sort(shell_dups.begin(), shell_dups.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(shell_dups.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  skip repeated shell entity: " +
                            std::to_string(shell_dups[i].second) +
                            "x  " + shell_dups[i].first);
                    }
                }
                if (gdb_shell_path_limit_skipped > 0) {
                    OutputLog::info(
                        "gdb-derived skipped BWSMarket shell path cap: " +
                        std::to_string(gdb_shell_path_limit_skipped));
                    std::vector<std::pair<std::string, size_t>> cap_paths(
                        gdb_shell_path_limit_skip_paths.begin(),
                        gdb_shell_path_limit_skip_paths.end());
                    std::sort(cap_paths.begin(), cap_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(cap_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  skip shell path cap: " +
                            std::to_string(cap_paths[i].second) +
                            "x  " + cap_paths[i].first);
                    }
                }
                if (gdb_duplicate_instances_skipped > 0) {
                    OutputLog::info(
                        "gdb-derived skipped exact duplicate records: " +
                        std::to_string(gdb_duplicate_instances_skipped));
                    std::vector<std::pair<std::string, size_t>> dup_paths(
                        gdb_duplicate_skip_paths.begin(),
                        gdb_duplicate_skip_paths.end());
                    std::sort(dup_paths.begin(), dup_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(dup_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  skip duplicate: " +
                            std::to_string(dup_paths[i].second) +
                            "x  " + dup_paths[i].first);
                    }
                }
                if (gdb_authored_shell_skipped > 0) {
                    OutputLog::info(
                        "gdb-derived skipped exact authored building/structure duplicates: " +
                        std::to_string(gdb_authored_shell_skipped) +
                        " instance(s) across " +
                        std::to_string(gdb_authored_shell_skip_paths.size()) +
                        " model(s)");
                    std::vector<std::pair<std::string, size_t>> skipped_paths(
                        gdb_authored_shell_skip_paths.begin(),
                        gdb_authored_shell_skip_paths.end());
                    std::sort(skipped_paths.begin(), skipped_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(skipped_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  skip exact authored: " +
                            std::to_string(skipped_paths[i].second) +
                            "x  " + skipped_paths[i].first);
                        auto sample_it =
                            gdb_authored_shell_skip_samples.find(
                                skipped_paths[i].first);
                        if (sample_it !=
                            gdb_authored_shell_skip_samples.end())
                        {
                            for (const auto& sample : sample_it->second) {
                                OutputLog::info("    e.g. " + sample);
                            }
                        }
                    }
                }
                if (gdb_companion_interiors_emitted > 0) {
                    OutputLog::info(
                        "gdb-derived companion interiors: " +
                        std::to_string(gdb_companion_interiors_emitted) +
                        " instance(s) across " +
                        std::to_string(gdb_companion_interior_paths.size()) +
                        " model(s)");
                    std::vector<std::pair<std::string, size_t>> interior_paths(
                        gdb_companion_interior_paths.begin(),
                        gdb_companion_interior_paths.end());
                    std::sort(interior_paths.begin(), interior_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(interior_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  companion interior: " +
                            std::to_string(interior_paths[i].second) +
                            "x  " + interior_paths[i].first);
                    }
                }
                if (gdb_companion_exteriors_emitted > 0) {
                    OutputLog::info(
                        "gdb-derived companion exteriors: " +
                        std::to_string(gdb_companion_exteriors_emitted) +
                        " instance(s) across " +
                        std::to_string(gdb_companion_exterior_paths.size()) +
                        " model(s)");
                    std::vector<std::pair<std::string, size_t>> exterior_paths(
                        gdb_companion_exterior_paths.begin(),
                        gdb_companion_exterior_paths.end());
                    std::sort(exterior_paths.begin(), exterior_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(exterior_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  companion exterior: " +
                            std::to_string(exterior_paths[i].second) +
                            "x  " + exterior_paths[i].first);
                    }
                }
                if (!gdb_emitted_shell_paths.empty()) {
                    size_t total_shells = 0;
                    for (const auto& kv : gdb_emitted_shell_paths) {
                        total_shells += kv.second;
                    }
                    OutputLog::info(
                        "gdb-derived emitted building/structure audit: " +
                        std::to_string(total_shells) +
                        " instance(s) across " +
                        std::to_string(gdb_emitted_shell_paths.size()) +
                        " model(s)");
                    std::vector<std::pair<std::string, size_t>> emitted_paths(
                        gdb_emitted_shell_paths.begin(),
                        gdb_emitted_shell_paths.end());
                    std::sort(emitted_paths.begin(), emitted_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(emitted_paths.size(), 12);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  emit shell: " +
                            std::to_string(emitted_paths[i].second) +
                            "x  " + emitted_paths[i].first);
                        auto sample_it =
                            gdb_emitted_shell_samples.find(
                                emitted_paths[i].first);
                        if (sample_it != gdb_emitted_shell_samples.end()) {
                            for (const auto& sample : sample_it->second) {
                                OutputLog::info("    e.g. " + sample);
                            }
                        }
                    }
                }
            } else {
                os3 << " (not emitted: GDB has entity names, not model paths)";
                OutputLog::warn(os3.str());
            }

            std::vector<uint8_t> hk_scan_bytes;
            const std::string hk_scan_path = sibling_with_ext(".havok_scenario");
            if (!emit_derived_render_placements) {
                OutputLog::info(
                    "derived render placements disabled");
            } else if (save_physics_instances_emitted > 0) {
                OutputLog::info(
                    "havok entity-scan: skipped render placement fallback; using .save PhysicsData transforms");
            } else if (load_text_sibling(hk_scan_path, hk_scan_bytes)) {
                auto be_f32 = [&](size_t off) -> float {
                    if (off + 4 > hk_scan_bytes.size())
                        return std::numeric_limits<float>::quiet_NaN();
                    uint32_t u =
                        (uint32_t(hk_scan_bytes[off    ]) << 24) |
                        (uint32_t(hk_scan_bytes[off + 1]) << 16) |
                        (uint32_t(hk_scan_bytes[off + 2]) <<  8) |
                         uint32_t(hk_scan_bytes[off + 3]);
                    float f; std::memcpy(&f, &u, 4); return f;
                };

                std::unordered_map<uint32_t, std::string> hash_to_name;
                hash_to_name.reserve(save_hash_to_name.size());
                for (const auto& kv : save_hash_to_name) {
                    hash_to_name.emplace(kv.first, kv.second);
                }

                size_t found = 0;
                size_t resolved_hk = 0;
                size_t in_terrain = 0;

                auto looks_pos = [](float x, float y, float z) {
                    if (!std::isfinite(x) || !std::isfinite(y) ||
                        !std::isfinite(z)) return false;
                    if (x < -100 || x > 500) return false;
                    if (y < -100 || y > 500) return false;
                    if (z < -100 || z > 500) return false;
                    int nonzero = 0;
                    if (std::fabs(x) > 0.5f) ++nonzero;
                    if (std::fabs(y) > 0.5f) ++nonzero;
                    if (std::fabs(z) > 0.5f) ++nonzero;
                    return nonzero >= 3;
                };
                auto in_main_terrain = [](float x, float y, float z) {
                    return (x >= 0 && x <= 290) &&
                           (y >= 0 && y <= 390) &&
                           (z >= -10 && z <= 250);
                };

                for (size_t i = 0; i + 4 <= hk_scan_bytes.size(); i += 4) {
                    uint32_t v =
                        (uint32_t(hk_scan_bytes[i    ]) << 24) |
                        (uint32_t(hk_scan_bytes[i + 1]) << 16) |
                        (uint32_t(hk_scan_bytes[i + 2]) <<  8) |
                         uint32_t(hk_scan_bytes[i + 3]);
                    auto it = hash_to_name.find(v);
                    if (it == hash_to_name.end()) continue;
                    ++found;

                    float best_x = 0, best_y = 0, best_z = 0;
                    int   best_dist = INT_MAX;
                    bool  best_in_terrain = false;
                    bool  found_any = false;

                    const size_t lo = (i >= 128) ? i - 128 : 0;
                    const size_t hi = std::min(hk_scan_bytes.size() - 12, i + 64);
                    for (size_t q = lo; q <= hi; q += 4) {
                        float x = be_f32(q);
                        float y = be_f32(q + 4);
                        float z = be_f32(q + 8);
                        if (!looks_pos(x, y, z)) continue;
                        const bool inT = in_main_terrain(x, y, z);
                        int dist = (int)(q > i ? q - i : i - q);
                        bool better = false;
                        if (!found_any) better = true;
                        else if (inT && !best_in_terrain) better = true;
                        else if (inT == best_in_terrain && dist < best_dist) {
                            better = true;
                        }
                        if (better) {
                            best_x = x; best_y = y; best_z = z;
                            best_dist = dist;
                            best_in_terrain = inT;
                            found_any = true;
                        }
                    }
                    if (!found_any) continue;
                    ++resolved_hk;
                    if (best_in_terrain) ++in_terrain;

                    std::string tok = canonicalize_for_match(it->second);
                    if (tok.empty()) continue;
                    const FlatAssetEntry* hit =
                        resolve_model_for_entity(it->second);
                    if (!hit) continue;

                    auto& pb = blocks_by_path[hit->full_path];
                    if (pb.model_path.empty()) {
                        pb.type = 0xB2;
                        pb.model_path = hit->full_path;
                    }
                    Level::PropInstance pi;
                    pi.values[0]  = best_x;
                    pi.values[1]  = best_y;
                    pi.values[2]  = best_z;
                    pi.values[6]  = 0.0f;
                    pi.values[7]  = 1.0f;
                    pi.values[9]  = pi.values[10] = pi.values[11] = 1.0f;
                    pb.instances.push_back(pi);
                }

                std::ostringstream hos;
                hos << "havok entity-scan: " << found
                    << " save hashes matched in havok_scenario, "
                    << resolved_hk << " got positions ("
                    << in_terrain << " in main terrain bounds)";
                if (resolved_hk > 0) OutputLog::success(hos.str());
                else                  OutputLog::warn(hos.str());

            } else {
                OutputLog::warn("havok entity-scan skipped: no .havok_scenario");
            }
            size_t extra_blocks = 0, extra_insts = 0;
            for (auto& kv : blocks_by_path) {
                if (kv.second.instances.empty()) continue;
                ++extra_blocks;
                extra_insts += kv.second.instances.size();
                g_pending_level_prop_blocks.push_back(std::move(kv.second));
            }
            std::ostringstream eos;
            eos << "derived placements: "
                << extra_blocks << " unique models / "
                << extra_insts << " instances appended to prop pipeline";
            if (extra_insts > 0) OutputLog::success(eos.str());
            else                 OutputLog::warn(eos.str());
        } else {
            OutputLog::warn("no .gdb sibling in BNK");
        }
    }

    for (const auto& vfs_stream_path : g_level_vfs_streaming_bnks) {
        std::string wanted_leaf =
            std::filesystem::path(vfs_stream_path).filename().string();
        std::transform(wanted_leaf.begin(), wanted_leaf.end(),
                       wanted_leaf.begin(), ::tolower);

        auto leaf_matches = [&](const std::string& mounted_leaf_lower) {
            if (mounted_leaf_lower == wanted_leaf) return true;
            if (mounted_leaf_lower.size() <= wanted_leaf.size() + 1) return false;
            const size_t off = mounted_leaf_lower.size() - wanted_leaf.size();
            if (mounted_leaf_lower.compare(off, wanted_leaf.size(),
                                           wanted_leaf) != 0) return false;
            return mounted_leaf_lower[off - 1] == '_';
        };

        std::string mounted_path;
        if (auto resolved = find_bnk_by_virtual_path(vfs_stream_path)) {
            mounted_path = *resolved;
        }
        if (mounted_path.empty()) {
            for (const auto& p : S.bnk_paths) {
                std::string leaf =
                    std::filesystem::path(p).filename().string();
                std::transform(leaf.begin(), leaf.end(), leaf.begin(), ::tolower);
                if (leaf_matches(leaf)) { mounted_path = p; break; }
            }
        }
        if (mounted_path.empty()) {
            for (const auto& p : S.nested_bnk_paths) {
                std::string leaf =
                    std::filesystem::path(p).filename().string();
                std::transform(leaf.begin(), leaf.end(),
                               leaf.begin(), ::tolower);
                if (leaf_matches(leaf)) { mounted_path = p; break; }
            }
        }
        if (mounted_path.empty()) {
            OutputLog::warn("streaming bnk not mounted: " + vfs_stream_path);
            continue;
        }

        try {
            BnkCache::Entry& bnk = BnkCache::get(mounted_path);
            const auto& files = bnk.reader->list_files();
            size_t hkx_count = 0;
            size_t total_rb  = 0;
            size_t total_inst = 0;

            for (size_t i = 0; i < files.size(); ++i) {
                const auto& name = files[i].name;
                std::string lower = name;
                std::transform(lower.begin(), lower.end(),
                               lower.begin(), ::tolower);


                if (lower.size() < 4 ||
                    lower.compare(lower.size() - 4, 4, ".hkx") != 0) continue;
                ++hkx_count;
                std::vector<uint8_t> hkx_bytes;
                try {
                    hkx_bytes = bnk.reader->extract_index_bytes((int)i);
                } catch (...) { continue; }
                auto pf = Havok::LoadPackFileFromBytes(
                    std::move(hkx_bytes), name);
                if (!pf) continue;
                total_inst += pf->virtual_fixups.size();
                const auto* rb_class = pf->find_class("hkpRigidBody");
                size_t this_rb = 0;
                if (rb_class) {
                    for (const auto& vf : pf->virtual_fixups) {
                        if (vf.classnames_offset ==
                            rb_class->classnames_offset) {
                            ++total_rb;
                            ++this_rb;
                        }
                    }
                }


            }

            std::ostringstream os;
            os << "streaming bnk '"
               << std::filesystem::path(mounted_path).filename().string()
               << "':  " << files.size() << " files, " << hkx_count
               << " .hkx,  " << total_rb << " rigid bodies across "
               << total_inst << " havok instances";
            OutputLog::success(os.str());

        } catch (const std::exception& ex) {
            OutputLog::warn(std::string("streaming bnk scan failed: ") + ex.what());
        }
    }

    if (bail_if_cancelled("pre-heightfield")) return false;

    if (!res.ehf_path.empty() || !res.ghf_path.empty()) {
        HeightfieldFiles hf;
        loader_progress_update(32, 100, "Loading heightfield files...");
        if (!LoadHeightfieldFiles(res.ehf_path, res.ghf_path,
                                  res.hdb_path, res.genv_path, hf)) {
            OutputLog::error("heightfield load failed: " + hf.error);
        } else if (S.cancel_requested.load()) {
            OutputLog::warn("level load cancelled during heightfield load");
            return false;
        } else {
            std::ostringstream hos;
            hos << "heightfield loaded:"
                << "  ehf=" << hf.ehf_bytes.size() << "B"
                << "  ghf=" << hf.ghf_bytes_compressed.size() << "B (gz)"
                << " → " << hf.ghf_bytes_raw.size() << "B (raw)";
            OutputLog::success(hos.str());

            if (!hf.ghf_bytes_raw.empty()) {
                GhfHeights hg;
                loader_progress_update(45, 100, "Decoding height grid...");
                if (!DecodeGhfHeights(hf.ghf_bytes_raw, hg)) {
                    OutputLog::error("  .ghf decode failed: " + hg.error);
                } else {
                    if (hg.tile_size <= 0.0f) {
                        const float ehf_tile = hf.ehf_header.ok
                                             ? hf.ehf_header.f2 : 0.0f;
                        const float fallback =
                            (ehf_tile > 0.0f && std::isfinite(ehf_tile))
                                ? ehf_tile : 0.5f;
                        std::ostringstream tos;
                        tos << "  .ghf tile_size was 0 — using .ehf f2 = "
                            << fallback << " (world = "
                            << (hg.width  - 1) * fallback << " x "
                            << (hg.height - 1) * fallback << ")";
                        OutputLog::info(tos.str());
                        hg.tile_size = fallback;
                    }

                    std::ostringstream gos;
                    gos << "  .ghf heightmap: " << hg.width << "x" << hg.height
                        << "  tile=" << hg.tile_size
                        << "  h=[" << hg.min_height << ".." << hg.max_height << "]";
                    OutputLog::success(gos.str());

                    TerrainMesh mesh;
                    loader_progress_update(58, 100, "Building terrain mesh...");
                    if (S.cancel_requested.load()) {
                        OutputLog::warn("level load cancelled before terrain mesh build");
                        return false;
                    }
                    if (!BuildTerrainMesh(hg, mesh)) {
                        OutputLog::error("  terrain mesh build failed");
                    } else {
                        const size_t tri_count = mesh.indices.size() / 3;
                        std::ostringstream mos;
                        mos << "  terrain mesh: verts=" << (mesh.positions.size() / 3)
                            << "  tris=" << tri_count;
                        OutputLog::success(mos.str());

                        g_pending_terrain_mesh        = std::move(mesh);
                        g_pending_terrain_label       = entry.name;
                        g_pending_terrain_level_entry = entry;
                        g_pending_terrain_ehf_bytes   = hf.ehf_bytes;
                        g_pending_adjacent_terrain_meshes.clear();

                        auto norm_path = [](std::string s) {
                            std::replace(s.begin(), s.end(), '\\', '/');
                            std::transform(s.begin(), s.end(), s.begin(),
                                [](unsigned char c) { return (char)std::tolower(c); });
                            return s;
                        };
                        auto with_ext = [](std::string p, const char* ext) {
                            const size_t slash = p.find_last_of("/\\");
                            const size_t dot = p.find_last_of('.');
                            if (dot != std::string::npos &&
                                (slash == std::string::npos || dot > slash)) {
                                p.resize(dot);
                            }
                            p += ext;
                            return p;
                        };
                        auto path_leaf = [&](const std::string& p) {
                            const std::string n = norm_path(p);
                            const size_t slash = n.find_last_of('/');
                            return (slash == std::string::npos)
                                ? n
                                : n.substr(slash + 1);
                        };
                        auto has_ext = [&](const std::string& p,
                                           const char* ext) {
                            const std::string n = norm_path(p);
                            const size_t len = std::strlen(ext);
                            return n.size() >= len &&
                                n.compare(n.size() - len, len, ext) == 0;
                        };
                        auto heightfield_id = [&](const std::string& p) {
                            const std::string n = norm_path(p);
                            const size_t id_pos = n.rfind("_id_");
                            if (id_pos == std::string::npos) {
                                return std::string{};
                            }
                            size_t first = id_pos + 4;
                            size_t last = first;
                            while (last < n.size()) {
                                const unsigned char c =
                                    static_cast<unsigned char>(n[last]);
                                if (!std::isxdigit(c)) break;
                                ++last;
                            }
                            return last > first ? n.substr(first, last - first)
                                                : std::string{};
                        };
                        auto path_overlap_score =
                            [&](const std::string& a,
                                const std::string& b) {
                                const std::string an = norm_path(a);
                                const std::string bn = norm_path(b);
                                int score = 0;
                                size_t start = 0;
                                while (start < an.size()) {
                                    const size_t end = an.find('/', start);
                                    const std::string part = an.substr(
                                        start,
                                        end == std::string::npos
                                            ? std::string::npos
                                            : end - start);
                                    if (part.size() > 2 &&
                                        bn.find(part) != std::string::npos) {
                                        ++score;
                                    }
                                    if (end == std::string::npos) break;
                                    start = end + 1;
                                }
                                return score;
                            };
                        auto resolve_adjacent_ghf =
                            [&](const std::string& ehf_path) {
                                const std::string exact =
                                    with_ext(ehf_path, ".ghf");
                                if (const FlatAssetEntry* fe =
                                        Level::FindHeightfieldByPath(exact)) {
                                    return fe->full_path.empty()
                                        ? exact
                                        : fe->full_path;
                                }

                                const std::string exact_leaf =
                                    path_leaf(exact);
                                const std::string id =
                                    heightfield_id(ehf_path);
                                const FlatAssetEntry* best = nullptr;
                                int best_score = 0;
                                for (const auto& fe : S.all_heightfield_files) {
                                    const std::string full =
                                        fe.full_path.empty()
                                            ? fe.name
                                            : fe.full_path;
                                    if (!has_ext(full, ".ghf") &&
                                        !has_ext(fe.name, ".ghf")) {
                                        continue;
                                    }

                                    int score = 0;
                                    if (path_leaf(full) == exact_leaf ||
                                        path_leaf(fe.name) == exact_leaf) {
                                        score += 1000;
                                    }
                                    if (!id.empty()) {
                                        const std::string cand_id =
                                            heightfield_id(full.empty()
                                                ? fe.name
                                                : full);
                                        if (cand_id == id) score += 700;
                                    }
                                    if (score == 0) continue;
                                    score += path_overlap_score(ehf_path,
                                                               full);
                                    if (score > best_score) {
                                        best_score = score;
                                        best = &fe;
                                    }
                                }
                                return best
                                    ? (best->full_path.empty()
                                        ? best->name
                                        : best->full_path)
                                    : std::string{};
                            };
                        auto build_ehf_proxy_mesh =
                            [&](const HeightfieldFiles& src,
                                float fallback_height,
                                TerrainMesh& out) {
                                out = {};
                                EhfParsedBody body;
                                if (!ParseEhfBody(src.ehf_bytes, body) ||
                                    body.chunks.empty() ||
                                    body.chunk_w == 0 ||
                                    body.chunk_h == 0) {
                                    return false;
                                }

                                float min_x = std::numeric_limits<float>::infinity();
                                float min_z = std::numeric_limits<float>::infinity();
                                float max_x = -std::numeric_limits<float>::infinity();
                                float max_z = -std::numeric_limits<float>::infinity();
                                for (const auto& c : body.chunks) {
                                    if (!std::isfinite(c.origin[0]) ||
                                        !std::isfinite(c.origin[1]) ||
                                        !std::isfinite(c.extent[0]) ||
                                        !std::isfinite(c.extent[1])) {
                                        continue;
                                    }
                                    min_x = std::min(min_x, c.origin[0]);
                                    min_z = std::min(min_z, c.origin[1]);
                                    max_x = std::max(max_x, c.extent[0]);
                                    max_z = std::max(max_z, c.extent[1]);
                                }
                                if (!std::isfinite(min_x) ||
                                    !std::isfinite(min_z) ||
                                    !std::isfinite(max_x) ||
                                    !std::isfinite(max_z) ||
                                    max_x <= min_x || max_z <= min_z) {
                                    return false;
                                }

                                uint32_t W = src.ehf_header.u0;
                                uint32_t H = src.ehf_header.u1;
                                if (W < 2 || H < 2 ||
                                    uint64_t(W) * uint64_t(H) > 600000ull) {
                                    W = body.chunk_w + 1;
                                    H = body.chunk_h + 1;
                                }
                                if (W < 2 || H < 2) return false;

                                const uint32_t CW = body.chunk_w;
                                const uint32_t CH = body.chunk_h;
                                const size_t corner_count =
                                    size_t(CW + 1) * size_t(CH + 1);
                                std::vector<float> corner_sum(
                                    corner_count, 0.0f);
                                std::vector<uint32_t> corner_count_hits(
                                    corner_count, 0);
                                auto corner_index =
                                    [&](uint32_t x, uint32_t y) {
                                        return size_t(y) * size_t(CW + 1) + x;
                                    };
                                const float chunk_span_x =
                                    (max_x - min_x) / float(CW);
                                const float chunk_span_z =
                                    (max_z - min_z) / float(CH);
                                auto add_corner =
                                    [&](uint32_t x, uint32_t y, float h) {
                                        const size_t ci = corner_index(x, y);
                                        corner_sum[ci] += h;
                                        ++corner_count_hits[ci];
                                    };
                                for (const auto& c : body.chunks) {
                                    int cx = int(std::lround(
                                        (c.origin[0] - min_x) /
                                        std::max(chunk_span_x, 1e-6f)));
                                    int cy = int(std::lround(
                                        (c.origin[1] - min_z) /
                                        std::max(chunk_span_z, 1e-6f)));
                                    cx = std::clamp(cx, 0, int(CW) - 1);
                                    cy = std::clamp(cy, 0, int(CH) - 1);

                                    float h = fallback_height;
                                    if (std::isfinite(c.origin[2]) &&
                                        std::isfinite(c.extent[2])) {
                                        h = 0.5f * (c.origin[2] + c.extent[2]);
                                    } else if (std::isfinite(c.origin[2])) {
                                        h = c.origin[2];
                                    } else if (std::isfinite(c.extent[2])) {
                                        h = c.extent[2];
                                    }

                                    const uint32_t ux = uint32_t(cx);
                                    const uint32_t uy = uint32_t(cy);
                                    add_corner(ux,     uy,     h);
                                    add_corner(ux + 1, uy,     h);
                                    add_corner(ux,     uy + 1, h);
                                    add_corner(ux + 1, uy + 1, h);
                                }

                                std::vector<float> corner_h(corner_count,
                                                            fallback_height);
                                for (size_t i = 0; i < corner_count; ++i) {
                                    if (corner_count_hits[i] > 0) {
                                        corner_h[i] = corner_sum[i] /
                                            float(corner_count_hits[i]);
                                    }
                                }
                                auto corner_h_at =
                                    [&](uint32_t x, uint32_t y) {
                                        x = std::min(x, CW);
                                        y = std::min(y, CH);
                                        return corner_h[corner_index(x, y)];
                                    };
                                auto bilerp_h =
                                    [&](float fx, float fy) {
                                        const int ix = std::clamp(
                                            int(std::floor(fx)), 0,
                                            int(CW) - 1);
                                        const int iy = std::clamp(
                                            int(std::floor(fy)), 0,
                                            int(CH) - 1);
                                        const float tx = std::clamp(
                                            fx - float(ix), 0.0f, 1.0f);
                                        const float ty = std::clamp(
                                            fy - float(iy), 0.0f, 1.0f);
                                        const float h00 = corner_h_at(
                                            uint32_t(ix), uint32_t(iy));
                                        const float h10 = corner_h_at(
                                            uint32_t(ix + 1), uint32_t(iy));
                                        const float h01 = corner_h_at(
                                            uint32_t(ix), uint32_t(iy + 1));
                                        const float h11 = corner_h_at(
                                            uint32_t(ix + 1), uint32_t(iy + 1));
                                        const float hx0 = h00 + (h10 - h00) * tx;
                                        const float hx1 = h01 + (h11 - h01) * tx;
                                        return hx0 + (hx1 - hx0) * ty;
                                    };

                                const size_t N = size_t(W) * size_t(H);
                                out.width = W;
                                out.height = H;
                                out.positions.resize(N * 3);
                                out.normals.resize(N * 3);
                                out.uvs.resize(N * 2);
                                out.min_height =
                                    std::numeric_limits<float>::infinity();
                                out.max_height =
                                    -std::numeric_limits<float>::infinity();

                                for (uint32_t y = 0; y < H; ++y) {
                                    const float vy = (H > 1)
                                        ? float(y) / float(H - 1)
                                        : 0.0f;
                                    const float fcy = vy * float(CH);
                                    for (uint32_t x = 0; x < W; ++x) {
                                        const float vx = (W > 1)
                                            ? float(x) / float(W - 1)
                                            : 0.0f;
                                        const float fcx = vx * float(CW);
                                        const float ph =
                                            bilerp_h(fcx, fcy);
                                        const size_t i = size_t(y) * W + x;
                                        out.positions[i * 3 + 0] =
                                            min_x + vx * (max_x - min_x);
                                        out.positions[i * 3 + 1] = ph;
                                        out.positions[i * 3 + 2] =
                                            min_z + vy * (max_z - min_z);
                                        out.uvs[i * 2 + 0] = vx;
                                        out.uvs[i * 2 + 1] = vy;
                                        out.min_height =
                                            std::min(out.min_height, ph);
                                        out.max_height =
                                            std::max(out.max_height, ph);
                                    }
                                }

                                const float step_x =
                                    (max_x - min_x) /
                                    float(std::max<uint32_t>(1, W - 1));
                                const float step_z =
                                    (max_z - min_z) /
                                    float(std::max<uint32_t>(1, H - 1));
                                auto height_at =
                                    [&](int x, int y) {
                                        x = std::clamp(x, 0, int(W) - 1);
                                        y = std::clamp(y, 0, int(H) - 1);
                                        return out.positions[
                                            (size_t(y) * W + size_t(x)) * 3 + 1];
                                    };
                                for (uint32_t y = 0; y < H; ++y) {
                                    for (uint32_t x = 0; x < W; ++x) {
                                        const float hl = height_at(
                                            int(x) - 1, int(y));
                                        const float hr = height_at(
                                            int(x) + 1, int(y));
                                        const float hd = height_at(
                                            int(x), int(y) - 1);
                                        const float hu = height_at(
                                            int(x), int(y) + 1);
                                        float nx = (hl - hr) * step_z;
                                        float ny = 2.0f * step_x * step_z;
                                        float nz = (hd - hu) * step_x;
                                        float len =
                                            std::sqrt(nx * nx + ny * ny +
                                                      nz * nz);
                                        if (len > 1e-6f) {
                                            nx /= len;
                                            ny /= len;
                                            nz /= len;
                                        } else {
                                            nx = 0.0f;
                                            ny = 1.0f;
                                            nz = 0.0f;
                                        }
                                        const size_t i = size_t(y) * W + x;
                                        out.normals[i * 3 + 0] = nx;
                                        out.normals[i * 3 + 1] = ny;
                                        out.normals[i * 3 + 2] = nz;
                                    }
                                }

                                out.indices.resize(
                                    size_t(W - 1) * size_t(H - 1) * 6);
                                size_t k = 0;
                                for (uint32_t y = 0; y + 1 < H; ++y) {
                                    for (uint32_t x = 0; x + 1 < W; ++x) {
                                        const uint32_t i00 =
                                            uint32_t(size_t(y) * W + x);
                                        const uint32_t i10 =
                                            uint32_t(size_t(y) * W + x + 1);
                                        const uint32_t i01 =
                                            uint32_t(size_t(y + 1) * W + x);
                                        const uint32_t i11 =
                                            uint32_t(size_t(y + 1) * W + x + 1);
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
                            };
                        const std::string main_ehf_norm = norm_path(res.ehf_path);
                        for (const auto& adj_ehf_path : all_ehf_refs) {
                            if (norm_path(adj_ehf_path) == main_ehf_norm) continue;
                            const std::string adj_ghf_path =
                                resolve_adjacent_ghf(adj_ehf_path);

                            HeightfieldFiles adj_hf;
                            if (!LoadHeightfieldFiles(adj_ehf_path, adj_ghf_path,
                                                      {}, {}, adj_hf)) {
                                OutputLog::warn("adjacent terrain load failed: " +
                                                adj_ehf_path + " (" + adj_hf.error + ")");
                                if (!adj_ghf_path.empty() &&
                                    LoadHeightfieldFiles(adj_ehf_path, {},
                                                         {}, {}, adj_hf)) {
                                    OutputLog::info("adjacent terrain using .ehf only "
                                                    "(resolved .ghf failed): " +
                                                    adj_ehf_path);
                                } else {
                                    continue;
                                }
                            }

                            if (S.cancel_requested.load()) {
                                OutputLog::warn("level load cancelled during adjacent terrain loop");
                                return false;
                            }
                            TerrainMesh adj_mesh;
                            bool used_ehf_proxy = false;
                            bool used_ehf_render_mesh = false;
                            if (!adj_hf.ghf_bytes_raw.empty()) {
                                GhfHeights adj_hg;
                                if (!DecodeGhfHeights(adj_hf.ghf_bytes_raw, adj_hg)) {
                                    OutputLog::warn("adjacent terrain .ghf decode failed: " +
                                                    adj_ghf_path + " (" + adj_hg.error + ")");
                                } else {
                                    if (adj_hg.tile_size <= 0.0f) {
                                        const float ehf_tile = adj_hf.ehf_header.ok
                                            ? adj_hf.ehf_header.f2 : 0.0f;
                                        adj_hg.tile_size =
                                            (ehf_tile > 0.0f &&
                                             std::isfinite(ehf_tile))
                                                ? ehf_tile : hg.tile_size;
                                    }
                                    if (!BuildTerrainMesh(adj_hg, adj_mesh)) {
                                        OutputLog::warn("adjacent terrain mesh build failed: " +
                                                        adj_ehf_path);
                                    }
                                }
                            }
                            if (!adj_mesh.ok) {
                                std::string render_stats;
                                if (build_ehf_render_strip_mesh(
                                        adj_hf.ehf_bytes, adj_mesh,
                                        &render_stats)) {
                                    used_ehf_proxy = true;
                                    used_ehf_render_mesh = true;
                                    OutputLog::info("adjacent terrain using "
                                                    ".ehf render mesh: " +
                                                    adj_ehf_path + " (" +
                                                    render_stats + ")");
                                } else if (build_ehf_proxy_mesh(adj_hf,
                                                                 hg.min_height,
                                                                 adj_mesh)) {
                                    used_ehf_proxy = true;
                                    OutputLog::info("adjacent terrain using .ehf "
                                                    "chunk proxy (last resort): " +
                                                    adj_ehf_path);
                                } else {
                                    OutputLog::info("adjacent terrain skipped "
                                                    "(no usable .ghf/.ehf mesh): " +
                                                    adj_ehf_path);
                                    continue;
                                }
                            } else if (!adj_ghf_path.empty() &&
                                       norm_path(adj_ghf_path) !=
                                       norm_path(with_ext(adj_ehf_path, ".ghf"))) {
                                OutputLog::info("adjacent terrain .ghf resolved: " +
                                                adj_ehf_path + " -> " +
                                                adj_ghf_path);
                            }

                            EhfParsedBody adj_body;
                            if (!used_ehf_proxy &&
                                ParseEhfBody(adj_hf.ehf_bytes, adj_body) &&
                                !adj_body.chunks.empty()) {
                                float min_x = 1e30f, min_z = 1e30f;
                                for (const auto& c : adj_body.chunks) {
                                    min_x = std::min(min_x, c.origin[0]);
                                    min_z = std::min(min_z, c.origin[1]);
                                }
                                if (std::isfinite(min_x) && std::isfinite(min_z) &&
                                    (std::fabs(min_x) > 1e-4f ||
                                     std::fabs(min_z) > 1e-4f)) {
                                    for (size_t pi = 0;
                                         pi + 2 < adj_mesh.positions.size();
                                         pi += 3) {
                                        adj_mesh.positions[pi + 0] += min_x;
                                        adj_mesh.positions[pi + 2] += min_z;
                                    }
                                }
                            }

                            Level::PendingAdjacentTerrain adj;
                            adj.label = std::filesystem::path(adj_ehf_path)
                                            .filename().string();
                            adj.preferred_bnk = g_pending_terrain_level_entry.bnk_path;
                            adj.ehf_bytes = std::move(adj_hf.ehf_bytes);
                            adj.mesh = std::move(adj_mesh);
                            adj.preserve_mesh_uvs = used_ehf_render_mesh;
                            adj.prefer_embedded_albedo = used_ehf_render_mesh;
                            g_pending_adjacent_terrain_meshes.push_back(std::move(adj));
                        }
                        if (!g_pending_adjacent_terrain_meshes.empty()) {
                            OutputLog::success("adjacent terrain meshes loaded: " +
                                std::to_string(g_pending_adjacent_terrain_meshes.size()));
                        }

                        g_pending_terrain_ghf_payload   = hf.ghf_bytes_raw;
                        g_pending_terrain_ghf_heights   = hg.heights;
                        g_pending_terrain_ghf_tile_size = hg.tile_size;
                        g_pending_terrain_ghf_width     = (int)hg.width;
                        g_pending_terrain_ghf_height    = (int)hg.height;
                        {
                            const FlatAssetEntry* fe =
                                Level::FindHeightfieldByPath(res.ghf_path);
                            g_pending_terrain_ghf_entry =
                                fe ? *fe : FlatAssetEntry{};
                        }

                        {
                            std::vector<Level::PropBlock> hkx_blocks =
                                std::move(g_pending_level_prop_blocks);
                            g_pending_level_prop_blocks = info.prop_blocks;
                            g_pending_level_prop_blocks.insert(
                                g_pending_level_prop_blocks.end(),
                                std::make_move_iterator(hkx_blocks.begin()),
                                std::make_move_iterator(hkx_blocks.end()));
                        }

                        if (!g_pending_terrain_ghf_heights.empty() &&
                            g_pending_terrain_ghf_width > 0 &&
                            g_pending_terrain_ghf_height > 0)
                        {
                            const int   gw = g_pending_terrain_ghf_width;
                            const int   gh = g_pending_terrain_ghf_height;
                            const float tile =
                                g_pending_terrain_ghf_tile_size > 0.0f
                                    ? g_pending_terrain_ghf_tile_size : 0.5f;
                            const auto& heights = g_pending_terrain_ghf_heights;
                            auto sample_h = [&](float wx, float wy) -> float {
                                float gx = wx / tile;
                                float gy = wy / tile;
                                int ix = int(gx); int iy = int(gy);
                                if (ix < 0) ix = 0; else if (ix >= gw) ix = gw - 1;
                                if (iy < 0) iy = 0; else if (iy >= gh) iy = gh - 1;
                                return heights[size_t(iy) * size_t(gw) + size_t(ix)];
                            };
                            size_t authored_z_count = 0;
                            size_t terrain_delta_count = 0;
                            float max_abs_delta = 0.0f;
                            for (auto& pb : g_pending_level_prop_blocks) {
                                if (pb.type != 0xB1) continue;
                                for (auto& inst : pb.instances) {
                                    const float terrain_z =
                                        sample_h(inst.values[0], inst.values[1]);
                                    const float delta = inst.values[2] - terrain_z;
                                    if (std::isfinite(delta)) {
                                        max_abs_delta =
                                            std::max(max_abs_delta,
                                                     std::fabs(delta));
                                        if (std::fabs(delta) > 0.25f) {
                                            ++terrain_delta_count;
                                        }
                                    }
                                    ++authored_z_count;
                                }
                            }
                            std::ostringstream gs;
                            gs << "preserved authored Z for "
                               << authored_z_count
                               << " GDB-derived placements";
                            if (authored_z_count > 0) {
                                gs << " ("
                                   << terrain_delta_count
                                   << " differ from terrain by >0.25m, max="
                                   << max_abs_delta << ")";
                            }
                            OutputLog::info(gs.str());
                        }

                        g_pending_level_water_present = false;
                        g_pending_level_water_scene = Level::WaterScene{};
                        g_pending_level_water_theme = Gdb::WaterTheme{};
                        {
                            std::vector<std::string> water_candidates;
                            auto add_unique_water = [&](const std::string& path) {
                                if (path.empty()) return;
                                const std::string norm = norm_path(path);
                                for (const auto& existing : water_candidates) {
                                    if (norm_path(existing) == norm) return;
                                }
                                water_candidates.push_back(path);
                            };

                            const std::string main_base =
                                basename_no_ext(res.ehf_path.empty()
                                    ? res.ghf_path : res.ehf_path);
                            for (const auto& water_ref : all_water_refs) {
                                if (!main_base.empty() &&
                                    basename_no_ext(water_ref) == main_base) {
                                    add_unique_water(water_ref);
                                }
                            }
                            if (!res.ehf_path.empty()) {
                                add_unique_water(with_ext(res.ehf_path, ".water"));
                            }
                            if (!res.ghf_path.empty()) {
                                add_unique_water(with_ext(res.ghf_path, ".water"));
                            }
                            for (const auto& water_ref : all_water_refs) {
                                add_unique_water(water_ref);
                            }

                            bool found_water_file = false;
                            for (const auto& water_path : water_candidates) {
                                std::vector<uint8_t> water_bytes;
                                if (!load_text_sibling(water_path, water_bytes) ||
                                    water_bytes.empty()) {
                                    continue;
                                }

                                found_water_file = true;
                                Level::WaterScene scene;
                                if (Level::ParseWaterFile(water_bytes, scene)) {
                                    size_t total_tiles = 0;
                                    for (const auto& b : scene.bodies)
                                        total_tiles += b.tiles.size();
                                    OutputLog::success(
                                        ".water parsed: " +
                                        std::to_string(scene.bodies.size()) +
                                        " bodies, " +
                                        std::to_string(total_tiles) + " tiles from " +
                                        water_path);
                                    g_pending_level_water_scene = std::move(scene);
                                    g_pending_level_water_present = true;
                                    break;
                                }

                                OutputLog::warn(
                                    ".water sibling found but failed to parse: " +
                                    water_path);
                            }

                            if (!found_water_file && !water_candidates.empty()) {
                                OutputLog::info(
                                    ".water not found in level BNK; first tried " +
                                    water_candidates.front());
                            }
                        }

                        g_pending_level_model_body_bnk.clear();
                        if (!res.model_body_bnk.empty()) {
                            auto found_model_bnk =
                                find_bnk_by_virtual_path(res.model_body_bnk);
                            if (!found_model_bnk) {
                                size_t slash =
                                    res.model_body_bnk.find_last_of("/\\");
                                std::string model_leaf =
                                    (slash == std::string::npos)
                                        ? res.model_body_bnk
                                        : res.model_body_bnk.substr(slash + 1);
                                std::transform(model_leaf.begin(),
                                               model_leaf.end(),
                                               model_leaf.begin(), ::tolower);
                                found_model_bnk = find_bnk_by_filename(model_leaf);
                            }
                            if (found_model_bnk) {
                                g_pending_level_model_body_bnk = *found_model_bnk;
                                OutputLog::info("level props: resolved model BNK " +
                                                res.model_body_bnk + " -> " +
                                                std::filesystem::path(*found_model_bnk)
                                                    .filename().string());
                            } else {
                                OutputLog::warn("level props: model BNK not mounted: " +
                                                res.model_body_bnk);
                            }
                        }

                        if (S.cancel_requested.load()) {
                            OutputLog::warn("level load cancelled before handoff to terrain stage");
                            return false;
                        }
                        g_pending_terrain_load =
                            !g_level_export_only_load.load();

                        {
                            auto pal = EhfPalette::Parse(hf.ehf_bytes);
                            if (pal.ok) {
                                std::ostringstream pos;
                                pos << "ehf palette: " << pal.entries.size()
                                    << " ground-texture entr"
                                    << (pal.entries.size() == 1 ? "y" : "ies")
                                    << " @ 0x" << std::hex << pal.palette_offset;
                                OutputLog::info(pos.str());
                                const size_t n_show = std::min<size_t>(pal.entries.size(), 6);
                                for (size_t pi = 0; pi < n_show; ++pi) {
                                    const auto& e = pal.entries[pi];
                                    std::filesystem::path d_p = e.diffuse_path;
                                    std::filesystem::path n_p = e.normal_path;
                                    std::ostringstream l;
                                    l << "  [" << pi << "] tile=" << e.tile_scale
                                      << " int=" << e.intensity
                                      << "  diff=" << d_p.filename().string()
                                      << "  norm=" << n_p.filename().string();
                                    OutputLog::info(l.str());
                                }
                                if (pal.entries.size() > n_show) {
                                    OutputLog::info("  ... (+ "
                                        + std::to_string(pal.entries.size() - n_show)
                                        + " more)");
                                }
                            }
                        }

                    }
                }
            }
        }
    } else {
        OutputLog::warn("no .ehf or .ghf path in level — can't load terrain");
    }

    return true;
}

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
    // World-space bbox from the 24-byte trailer after the tile's height
    // cells: {min_x, min_y, min_z, max_x, max_y, max_z} as BE floats.
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
    // pf=98 blobs are raw 16-bit masks: header(mt) + w*h*2 bytes of pixels
    // (no size fields at mt -- that dword is the first row of texels).
    // Everything else is compressed: {raw_size, comp_size} @ mt, then zlib.
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

    // Preferred placement: each tile's grid trailer stores its world bbox, so
    // its cell position is exact. Levels with uniform tiles (most outdoor
    // heightfields) coincide with the old row-packing guess, but irregular
    // layouts (caves) do not and were stitched scrambled.
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
        // Fallback: legacy row-packing guess.
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
    // The tile-page streams cover the whole mip chain; raw_size is only the
    // top mip, so inflate stops on a full output buffer WITHOUT reaching
    // Z_STREAM_END. Requiring the end marker here rejected every page in
    // every level. Accept a completely-filled top mip instead (same policy
    // as the huffman-albedo path below).
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
                        aspect_ok(c.W, c.H))
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

    // Parse the body and register the LOD palette FIRST. The embedded-albedo
    // early return below used to skip this, leaving the palette empty (or
    // stale from the previous level) whenever the embedded composite
    // succeeded — which silently killed the LOD thumbnails and the SPLAT
    // shader for exactly those levels.
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
