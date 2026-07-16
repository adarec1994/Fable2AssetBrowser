#include "Level/Core/LevelLoader.h"
#include "Level/Loading/LevelBinaryReader.h"
#include "Level/Loading/LevelTerrainLoaderInternal.h"
#include "Level/Terrain/EhfChunkParser.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Level {


bool BuildEhfRenderStripMesh(const std::vector<uint8_t>& ehf,
                                 TerrainMesh& out,
                                 std::string* out_stats)
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
    struct TileSamples {
        std::unordered_map<uint64_t, Sample> samples;
        std::vector<int32_t> qxs;
        std::vector<int32_t> qzs;
    };
    std::vector<TileSamples> tile_samples;
    tile_samples.reserve(render_tile_count);
    uint64_t cell_count_total = 0;
    uint64_t vertex_sample_total = 0;
    float sample_min_x =  std::numeric_limits<float>::infinity();
    float sample_max_x = -std::numeric_limits<float>::infinity();
    float sample_min_z =  std::numeric_limits<float>::infinity();
    float sample_max_z = -std::numeric_limits<float>::infinity();

    auto sane_coord = [](float v) {
        return std::isfinite(v) && std::fabs(v) < 100000.0f;
    };
    auto quant = [](float v) -> int32_t {
        return int32_t(std::lround(v * 2.0f));
    };
    auto make_key = [](int32_t qx, int32_t qz) -> uint64_t {
        return (uint64_t(uint32_t(qx)) << 32) | uint32_t(qz);
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

        TileSamples ts;
        ts.samples.reserve(size_t(cell_count) * 8);
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
                const int32_t qx = quant(x);
                const int32_t qz = quant(z);
                auto [it, inserted] =
                    ts.samples.try_emplace(make_key(qx, qz));
                if (inserted) {
                    ts.qxs.push_back(qx);
                    ts.qzs.push_back(qz);
                }
                Sample& s = it->second;
                s.y = (s.count == 0) ? y : std::max(s.y, y);
                ++s.count;
                ++vertex_sample_total;
                sample_min_x = std::min(sample_min_x, x);
                sample_max_x = std::max(sample_max_x, x);
                sample_min_z = std::min(sample_min_z, z);
                sample_max_z = std::max(sample_max_z, z);
            }
        }

        r.i += size_t(cell_count) * 160u + 24u;
        tile_samples.push_back(std::move(ts));
    }

    if (vertex_sample_total < 4) {
        return false;
    }

    float tex_min_x = sample_min_x;
    float tex_max_x = sample_max_x;
    float tex_min_z = sample_min_z;
    float tex_max_z = sample_max_z;
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

    out.min_height = std::numeric_limits<float>::infinity();
    out.max_height = -std::numeric_limits<float>::infinity();
    uint32_t filled_holes = 0;
    uint32_t tiles_meshed = 0;

    for (auto& ts : tile_samples) {
        auto& qxs = ts.qxs;
        auto& qzs = ts.qzs;
        std::sort(qxs.begin(), qxs.end());
        qxs.erase(std::unique(qxs.begin(), qxs.end()), qxs.end());
        std::sort(qzs.begin(), qzs.end());
        qzs.erase(std::unique(qzs.begin(), qzs.end()), qzs.end());
        if (qxs.size() < 2 || qzs.size() < 2 ||
            qxs.size() > 2048 || qzs.size() > 2048) {
            continue;
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
                const auto it = ts.samples.find(make_key(qxs[x], qzs[z]));
                if (it == ts.samples.end() || it->second.count == 0) continue;
                const size_t gi = grid_index(x, z);
                heights[gi] = it->second.y;
                has_height[gi] = 1;
            }
        }

        bool tile_ok = true;
        for (uint32_t z = 0; z < H && tile_ok; ++z) {
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
                if (!found) { tile_ok = false; break; }
                heights[gi] = best_h;
                has_height[gi] = 1;
                ++filled_holes;
            }
        }
        if (!tile_ok) continue;

        const uint32_t base = uint32_t(out.positions.size() / 3);
        for (uint32_t z = 0; z < H; ++z) {
            for (uint32_t x = 0; x < W; ++x) {
                const size_t gi = grid_index(x, z);
                const float wx = float(qxs[x]) * 0.5f;
                const float wz = float(qzs[z]) * 0.5f;
                const float wy = heights[gi];
                out.positions.push_back(wx);
                out.positions.push_back(wy);
                out.positions.push_back(wz);
                out.uvs.push_back((wx - tex_min_x) / span_x);
                out.uvs.push_back((wz - tex_min_z) / span_z);
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
                out.normals.push_back(nx);
                out.normals.push_back(ny);
                out.normals.push_back(nz);
            }
        }

        for (uint32_t z = 0; z + 1 < H; ++z) {
            for (uint32_t x = 0; x + 1 < W; ++x) {
                const uint32_t i00 = base + uint32_t(grid_index(x, z));
                const uint32_t i10 = base + uint32_t(grid_index(x + 1, z));
                const uint32_t i01 = base + uint32_t(grid_index(x, z + 1));
                const uint32_t i11 = base + uint32_t(grid_index(x + 1, z + 1));
                out.indices.push_back(i00);
                out.indices.push_back(i01);
                out.indices.push_back(i10);
                out.indices.push_back(i10);
                out.indices.push_back(i01);
                out.indices.push_back(i11);
            }
        }
        ++tiles_meshed;
    }

    out.width  = 0;
    out.height = 0;
    if (!std::isfinite(out.min_height)) out.min_height = 0.0f;
    if (!std::isfinite(out.max_height)) out.max_height = 0.0f;

    out.ok = !out.indices.empty();
    if (out_stats) {
        std::ostringstream ss;
        ss << render_tile_count << " render tile(s), "
           << tiles_meshed << " meshed, "
           << cell_count_total << " strip cell(s), "
           << vertex_sample_total << " vertex sample(s)"
           << (filled_holes ? ", filled " + std::to_string(filled_holes)
                            : std::string{})
           << ", " << (out.indices.size() / 3) << " tri(s)";
        *out_stats = ss.str();
    }
    return out.ok;
}

bool BuildEhfVistaPatchMesh(const std::vector<uint8_t>& ehf,
                                TerrainMesh& out,
                                std::string* out_stats)
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

    const uint32_t body_off  = read_be_u32_raw(ehf.data() + 55);
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
    uint32_t patch_count = 0;
    if (!r.f32(max_height_hint) || !r.u32(patch_count)) return false;
    if (patch_count == 0 || patch_count > 4096) return false;

    auto sane_coord = [](float v) {
        return std::isfinite(v) && std::fabs(v) < 1000000.0f;
    };
    auto qkey = [](float x, float y) -> uint64_t {
        const int32_t qx = int32_t(std::lround(double(x) * 4.0));
        const int32_t qy = int32_t(std::lround(double(y) * 4.0));
        return (uint64_t(uint32_t(qx)) << 32) | uint32_t(qy);
    };

    struct VistaPatch {
        float    aabb_min[3];
        float    aabb_max[3];
        uint32_t W = 0, H = 0;
        size_t   cells_off = 0;
    };
    std::vector<VistaPatch> patches;
    patches.reserve(patch_count);
    std::unordered_map<uint64_t, float> lattice;
    std::unordered_map<uint64_t, float> lattice_diag;

    uint64_t cell_total = 0;
    for (uint32_t pi = 0; pi < patch_count; ++pi) {
        float pf_a = 0.0f, pf_b = 0.0f;
        uint32_t W = 0, H = 0;
        if (!r.f32(pf_a) || !r.f32(pf_b) || !r.u32(W) || !r.u32(H)) {
            return false;
        }
        if (W == 0 || H == 0 || W > 1024 || H > 1024) return false;
        const uint64_t cell_count = uint64_t(W) * uint64_t(H);
        if (cell_count > 65536 ||
            uint64_t(r.i) + cell_count * 160ull + 24ull > uint64_t(r.n)) {
            return false;
        }

        VistaPatch p;
        p.W = W;
        p.H = H;
        p.cells_off = r.i;
        cell_total += cell_count;

        const uint8_t* aabb = r.p + r.i + size_t(cell_count) * 160u;
        for (int k = 0; k < 3; ++k) {
            p.aabb_min[k] = read_be_f32_raw(aabb + size_t(k) * 4u);
            p.aabb_max[k] = read_be_f32_raw(aabb + 12u + size_t(k) * 4u);
        }

        for (uint64_t ci = 0; ci < cell_count; ++ci) {
            const uint8_t* base_v =
                r.p + p.cells_off + size_t(ci) * 160u + 64u + 12u;
            const float bx = read_be_f32_raw(base_v + 0);
            const float by = read_be_f32_raw(base_v + 4);
            const float bh = read_be_f32_raw(base_v + 8);
            if (sane_coord(bx) && sane_coord(by) && sane_coord(bh)) {
                lattice[qkey(bx, by)] = bh;
            }
            const uint8_t* diag_v =
                r.p + p.cells_off + size_t(ci) * 160u + 64u + 48u;
            const float dx = read_be_f32_raw(diag_v + 0);
            const float dy = read_be_f32_raw(diag_v + 4);
            const float dh = read_be_f32_raw(diag_v + 8);
            if (sane_coord(dx) && sane_coord(dy) && sane_coord(dh)) {
                lattice_diag.emplace(qkey(dx, dy), dh);
            }
        }

        patches.push_back(p);
        r.i += size_t(cell_count) * 160u + 24u;
    }
    if (patches.empty() || lattice.empty()) return false;

    out.min_height =  std::numeric_limits<float>::infinity();
    out.max_height = -std::numeric_limits<float>::infinity();

    for (const VistaPatch& p : patches) {
        const float ax = p.aabb_min[0];
        const float ay = p.aabb_min[1];
        const float sx = (p.aabb_max[0] - p.aabb_min[0]) / float(p.W);
        const float sy = (p.aabb_max[1] - p.aabb_min[1]) / float(p.H);
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
                const uint64_t key =
                    qkey(ax + sx * float(i), ay + sy * float(j));
                float hval = 0.0f;
                bool  have = false;
                if (auto it = lattice.find(key); it != lattice.end()) {
                    hval = it->second;
                    have = true;
                } else if (auto it2 = lattice_diag.find(key);
                           it2 != lattice_diag.end()) {
                    hval = it2->second;
                    have = true;
                }
                if (have) {
                    gh[size_t(j) * VW + i]   = hval;
                    ghas[size_t(j) * VW + i] = 1;
                }
            }
        }
        for (uint32_t j = 0; j < VH; ++j) {
            for (uint32_t i = 0; i < VW; ++i) {
                const size_t gi = size_t(j) * VW + i;
                if (ghas[gi]) continue;
                float best = 0.0f;
                int   best_d = INT32_MAX;
                for (uint32_t jj = 0; jj < VH; ++jj) {
                    for (uint32_t ii = 0; ii < VW; ++ii) {
                        const size_t oi = size_t(jj) * VW + ii;
                        if (!ghas[oi]) continue;
                        const int d = std::abs(int(ii) - int(i)) +
                                      std::abs(int(jj) - int(j));
                        if (d < best_d) {
                            best_d = d;
                            best   = gh[oi];
                        }
                    }
                }
                if (best_d == INT32_MAX) { best = p.aabb_min[2]; }
                gh[gi] = best;
            }
        }

        const uint32_t vbase = uint32_t(out.positions.size() / 3);
        for (uint32_t j = 0; j < VH; ++j) {
            for (uint32_t i = 0; i < VW; ++i) {
                const float wx = ax + sx * float(i);
                const float wy = ay + sy * float(j);
                const float wh = gh[size_t(j) * VW + i];
                out.positions.push_back(wx);
                out.positions.push_back(wh);
                out.positions.push_back(wy);
                out.uvs.push_back(wx);
                out.uvs.push_back(wy);
                out.min_height = std::min(out.min_height, wh);
                out.max_height = std::max(out.max_height, wh);
            }
        }

        auto h_at = [&](int i, int j) -> float {
            const float wx = ax + sx * float(i);
            const float wy = ay + sy * float(j);
            const uint64_t key = qkey(wx, wy);
            if (auto it = lattice.find(key); it != lattice.end()) {
                return it->second;
            }
            if (auto it2 = lattice_diag.find(key);
                it2 != lattice_diag.end()) {
                return it2->second;
            }
            const int ci = std::clamp(i, 0, int(VW) - 1);
            const int cj = std::clamp(j, 0, int(VH) - 1);
            return gh[size_t(cj) * VW + size_t(ci)];
        };
        for (uint32_t j = 0; j < VH; ++j) {
            for (uint32_t i = 0; i < VW; ++i) {
                const float hl = h_at(int(i) - 1, int(j));
                const float hr = h_at(int(i) + 1, int(j));
                const float hd = h_at(int(i), int(j) - 1);
                const float hu = h_at(int(i), int(j) + 1);
                float nx = (hl - hr) * sy;
                float ny = 2.0f * sx * sy;
                float nz = (hd - hu) * sx;
                const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (len > 1e-6f) {
                    nx /= len; ny /= len; nz /= len;
                } else {
                    nx = 0.0f; ny = 1.0f; nz = 0.0f;
                }
                out.normals.push_back(nx);
                out.normals.push_back(ny);
                out.normals.push_back(nz);
            }
        }

        for (uint32_t j = 0; j + 1 < VH; ++j) {
            for (uint32_t i = 0; i + 1 < VW; ++i) {
                const uint32_t i00 = vbase + uint32_t(size_t(j) * VW + i);
                const uint32_t i10 = i00 + 1;
                const uint32_t i01 = vbase +
                    uint32_t(size_t(j + 1) * VW + i);
                const uint32_t i11 = i01 + 1;
                out.indices.push_back(i00);
                out.indices.push_back(i01);
                out.indices.push_back(i10);
                out.indices.push_back(i10);
                out.indices.push_back(i01);
                out.indices.push_back(i11);
            }
        }
    }

    if (out.indices.empty()) return false;

    float tex_min_x =  std::numeric_limits<float>::infinity();
    float tex_max_x = -std::numeric_limits<float>::infinity();
    float tex_min_z =  std::numeric_limits<float>::infinity();
    float tex_max_z = -std::numeric_limits<float>::infinity();
    for (const VistaPatch& p : patches) {
        if (!std::isfinite(p.aabb_min[0]) || !std::isfinite(p.aabb_min[1]) ||
            !std::isfinite(p.aabb_max[0]) || !std::isfinite(p.aabb_max[1])) {
            continue;
        }
        tex_min_x = std::min(tex_min_x, p.aabb_min[0]);
        tex_min_z = std::min(tex_min_z, p.aabb_min[1]);
        tex_max_x = std::max(tex_max_x, p.aabb_max[0]);
        tex_max_z = std::max(tex_max_z, p.aabb_max[1]);
    }
    if (!std::isfinite(tex_min_x) || !std::isfinite(tex_min_z) ||
        !(tex_max_x > tex_min_x) || !(tex_max_z > tex_min_z)) {
        tex_min_x =  std::numeric_limits<float>::infinity();
        tex_max_x = -std::numeric_limits<float>::infinity();
        tex_min_z =  std::numeric_limits<float>::infinity();
        tex_max_z = -std::numeric_limits<float>::infinity();
        for (size_t v = 0; v + 1 < out.uvs.size(); v += 2) {
            tex_min_x = std::min(tex_min_x, out.uvs[v + 0]);
            tex_max_x = std::max(tex_max_x, out.uvs[v + 0]);
            tex_min_z = std::min(tex_min_z, out.uvs[v + 1]);
            tex_max_z = std::max(tex_max_z, out.uvs[v + 1]);
        }
    }
    const float span_x = std::max(tex_max_x - tex_min_x, 1e-6f);
    const float span_z = std::max(tex_max_z - tex_min_z, 1e-6f);
    for (size_t v = 0; v + 1 < out.uvs.size(); v += 2) {
        out.uvs[v + 0] = (out.uvs[v + 0] - tex_min_x) / span_x;
        out.uvs[v + 1] = (out.uvs[v + 1] - tex_min_z) / span_z;
    }

    if (!std::isfinite(out.min_height)) out.min_height = 0.0f;
    if (!std::isfinite(out.max_height)) out.max_height = 0.0f;

    out.width  = 0;
    out.height = 0;
    out.ok = true;
    if (out_stats) {
        std::ostringstream ss;
        ss << patch_count << " bg patch(es), " << cell_total << " cell(s), "
           << (out.indices.size() / 3) << " tri(s)";
        *out_stats = ss.str();
    }
    return true;
}


}
