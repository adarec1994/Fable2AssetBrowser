#include "TerrainEdit.h"

#include "UI/ModelPreview.h"
#include "Utilities/State.h"
#include "ISO/IsoMount.h"
#include "ISO/IsoWriteback.h"
#include "UI/OutputLog.h"

#ifdef _WIN32
#include <d3d11.h>
#endif

#include <zlib.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

namespace TerrainEdit {

namespace {

State& storage() {
    static State s;
    return s;
}

void sync_positions_y() {
    auto& s = storage();
    const size_t n = size_t(s.width) * size_t(s.height);
    if (s.positions.size() < n * 3) return;
    for (size_t i = 0; i < n; ++i) {
        s.positions[i * 3 + 1] = s.heights_current[i];
    }
    s.dirty = true;
}

}

void Init(int width, int height, float tile_size,
          float center_x, float center_z,
          std::vector<float> heights,
          std::vector<float> positions,
          std::vector<uint8_t> ghf_payload,
          std::string ghf_bnk_path, int ghf_file_index,
          std::string ghf_full_path,
          uint64_t   ghf_bnk_entry_offset,
          uint32_t   ghf_bnk_entry_on_disk_size,
          bool       ghf_bnk_entry_is_compressed)
{
    Clear();
    auto& s = storage();

    s.width            = width;
    s.height           = height;
    s.tile_size        = (tile_size > 0.f) ? tile_size : 0.5f;
    s.center_x         = center_x;
    s.center_z         = center_z;
    s.heights_original = heights;
    s.heights_current  = std::move(heights);
    s.positions        = std::move(positions);
    s.ghf_payload_original = std::move(ghf_payload);
    s.ghf_bnk_path     = std::move(ghf_bnk_path);
    s.ghf_file_index   = ghf_file_index;
    s.ghf_full_path    = std::move(ghf_full_path);
    s.ghf_bnk_entry_offset       = ghf_bnk_entry_offset;
    s.ghf_bnk_entry_on_disk_size = ghf_bnk_entry_on_disk_size;
    s.ghf_bnk_entry_is_compressed= ghf_bnk_entry_is_compressed;

    s.min_x =  std::numeric_limits<float>::infinity();
    s.max_x = -std::numeric_limits<float>::infinity();
    s.min_z =  std::numeric_limits<float>::infinity();
    s.max_z = -std::numeric_limits<float>::infinity();
    const size_t n = size_t(s.width) * size_t(s.height);
    for (size_t i = 0; i < n; ++i) {
        const float x = s.positions[i * 3 + 0];
        const float z = s.positions[i * 3 + 2];
        if (x < s.min_x) s.min_x = x;
        if (x > s.max_x) s.max_x = x;
        if (z < s.min_z) s.min_z = z;
        if (z > s.max_z) s.max_z = z;
    }

    s.loaded = true;
    s.dirty  = false;
}

const State& Get() { return storage(); }
bool IsLoaded() { return storage().loaded; }
bool IsDirty()  { return storage().dirty;  }

void RaiseAll(float delta) {
    auto& s = storage();
    if (!s.loaded) return;
    for (float& h : s.heights_current) h += delta;
    sync_positions_y();
}

void LowerAll(float delta) {
    RaiseAll(-delta);
}

void SmoothAll() {
    auto& s = storage();
    if (!s.loaded || s.width < 3 || s.height < 3) return;
    const int W = s.width;
    const int H = s.height;
    std::vector<float> dst(s.heights_current.size());
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float sum = 0.f; int cnt = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int xx = std::clamp(x + dx, 0, W - 1);
                    const int yy = std::clamp(y + dy, 0, H - 1);
                    sum += s.heights_current[size_t(yy) * W + xx];
                    ++cnt;
                }
            }
            dst[size_t(y) * W + x] = sum / float(cnt);
        }
    }
    s.heights_current = std::move(dst);
    sync_positions_y();
}

void FlattenAll(float target) {
    auto& s = storage();
    if (!s.loaded) return;
    std::fill(s.heights_current.begin(), s.heights_current.end(), target);
    sync_positions_y();
}

void Reset() {
    auto& s = storage();
    if (!s.loaded) return;
    s.heights_current = s.heights_original;
    sync_positions_y();
    s.dirty = false;
}

float SampleHeightAtWorldXZ(float wx, float wz) {
    auto& s = storage();
    if (!s.loaded || s.tile_size <= 0.f) return 0.f;
    const float gx = (wx + s.center_x) / s.tile_size;
    const float gz = (wz + s.center_z) / s.tile_size;
    if (gx < 0 || gz < 0 ||
        gx > float(s.width - 1) || gz > float(s.height - 1))
        return 0.f;
    const int x0 = (int)std::floor(gx);
    const int z0 = (int)std::floor(gz);
    const int x1 = std::min(s.width  - 1, x0 + 1);
    const int z1 = std::min(s.height - 1, z0 + 1);
    const float fx = gx - float(x0);
    const float fz = gz - float(z0);
    const float h00 = s.heights_current[size_t(z0) * s.width + x0];
    const float h10 = s.heights_current[size_t(z0) * s.width + x1];
    const float h01 = s.heights_current[size_t(z1) * s.width + x0];
    const float h11 = s.heights_current[size_t(z1) * s.width + x1];
    return (h00 * (1.f - fx) + h10 * fx) * (1.f - fz)
         + (h01 * (1.f - fx) + h11 * fx) *        fz;
}

bool Raycast(float ox, float oy, float oz,
             float dx, float dy, float dz,
             float& out_hit_x, float& out_hit_y, float& out_hit_z)
{
    auto& s = storage();
    if (!s.loaded) return false;
    const float dl = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dl < 1e-6f) return false;
    dx /= dl; dy /= dl; dz /= dl;

    const float step = std::max(0.5f, s.tile_size * 0.5f);
    const float max_dist = std::max(
        (s.max_x - s.min_x) + (s.max_z - s.min_z),
        std::abs(oy) * 8.f);
    float prev_t  = 0.f;
    float prev_dh = oy - SampleHeightAtWorldXZ(ox, oz);
    for (float t = step; t < max_dist; t += step) {
        const float wx = ox + dx * t;
        const float wy = oy + dy * t;
        const float wz = oz + dz * t;

        if (wx < s.min_x - step || wx > s.max_x + step ||
            wz < s.min_z - step || wz > s.max_z + step) {
            prev_t = t; prev_dh = wy - SampleHeightAtWorldXZ(wx, wz);
            continue;
        }
        const float th = SampleHeightAtWorldXZ(wx, wz);
        const float dh = wy - th;
        if (dh <= 0.f && prev_dh > 0.f) {
            float lo = prev_t, hi = t;
            for (int it = 0; it < 16; ++it) {
                const float mid = (lo + hi) * 0.5f;
                const float mx = ox + dx * mid;
                const float my = oy + dy * mid;
                const float mz = oz + dz * mid;
                const float mt = SampleHeightAtWorldXZ(mx, mz);
                if (my - mt > 0.f) lo = mid;
                else               hi = mid;
            }
            const float t_hit = (lo + hi) * 0.5f;
            out_hit_x = ox + dx * t_hit;
            out_hit_z = oz + dz * t_hit;
            out_hit_y = SampleHeightAtWorldXZ(out_hit_x, out_hit_z);
            return true;
        }
        prev_t = t; prev_dh = dh;
    }
    return false;
}

void ApplyBrush(BrushTool tool,
                float wx, float wz,
                float radius, float strength,
                float target_h, float falloff)
{
    auto& s = storage();
    if (!s.loaded || tool == BrushTool::None) return;
    if (radius <= 0.f) return;

    const float gx_c = (wx + s.center_x) / s.tile_size;
    const float gz_c = (wz + s.center_z) / s.tile_size;
    const float gr   = radius / s.tile_size;

    const int xmin = std::max(0,                 int(std::floor(gx_c - gr)));
    const int xmax = std::min(s.width  - 1,      int(std::ceil (gx_c + gr)));
    const int zmin = std::max(0,                 int(std::floor(gz_c - gr)));
    const int zmax = std::min(s.height - 1,      int(std::ceil (gz_c + gr)));
    if (xmin > xmax || zmin > zmax) return;

    std::vector<float> smoothed;
    if (tool == BrushTool::Smooth) {
        smoothed.assign(s.heights_current.size(), 0.f);
        for (int z = zmin; z <= zmax; ++z) {
            for (int x = xmin; x <= xmax; ++x) {
                float sum = 0.f; int cnt = 0;
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = std::clamp(x + dx, 0, s.width  - 1);
                        const int zz = std::clamp(z + dz, 0, s.height - 1);
                        sum += s.heights_current[size_t(zz) * s.width + xx];
                        ++cnt;
                    }
                }
                smoothed[size_t(z) * s.width + x] = sum / float(cnt);
            }
        }
    }

    
    static uint32_t s_noise_seed = 0x9E3779B9u;
    s_noise_seed = s_noise_seed * 1664525u + 1013904223u;

    const float clamped_falloff = std::clamp(falloff, 0.f, 1.f);
    const float fade_start = gr * (1.f - clamped_falloff);

    bool any_changed = false;
    for (int z = zmin; z <= zmax; ++z) {
        for (int x = xmin; x <= xmax; ++x) {
            const float dxg = float(x) - gx_c;
            const float dzg = float(z) - gz_c;
            const float dist = std::sqrt(dxg * dxg + dzg * dzg);
            if (dist >= gr) continue;
            
            
            float w = 1.f;
            if (dist > fade_start && gr > fade_start) {
                const float t =
                    1.f - (dist - fade_start) / (gr - fade_start);
                w = t * t * (3.f - 2.f * t);
            }

            const size_t idx = size_t(z) * s.width + x;
            float& h = s.heights_current[idx];
            switch (tool) {
                case BrushTool::Raise:
                    h += strength * w; break;
                case BrushTool::Lower:
                    h -= strength * w; break;
                case BrushTool::Smooth: {
                    const float a = std::clamp(strength * w, 0.f, 1.f);
                    h = h * (1.f - a) + smoothed[idx] * a;
                    break;
                }
                case BrushTool::Flatten: {
                    const float a = std::clamp(strength * w, 0.f, 1.f);
                    h = h * (1.f - a) + target_h * a;
                    break;
                }
                case BrushTool::Noise: {
                    uint32_t n = uint32_t(x) * 73856093u ^
                                 uint32_t(z) * 19349663u ^ s_noise_seed;
                    n ^= n >> 13;
                    n *= 0x85EBCA6Bu;
                    n ^= n >> 16;
                    const float r =
                        (float(n & 0xFFFFFFu) / float(0xFFFFFF)) * 2.f -
                        1.f;
                    h += r * strength * w;
                    break;
                }
                default: break;
            }
            any_changed = true;
        }
    }

    if (any_changed) {
        sync_positions_y();
    }
}

#ifdef _WIN32

void ApplyToGpu(ID3D11Device* device, void* mesh_v) {
    auto& s = storage();
    if (!s.loaded || !device || !mesh_v) return;

    auto* mesh = static_cast<::MPPerMesh*>(mesh_v);
    if (!mesh->vb || mesh->index_count == 0) return;

    const size_t vert_count = size_t(s.width) * size_t(s.height);
    const UINT   vstride    = sizeof(MPVertex);
    const size_t need_bytes = vert_count * vstride;

    D3D11_BUFFER_DESC vd{};
    mesh->vb->GetDesc(&vd);
    if (vd.ByteWidth != need_bytes) return;

    D3D11_BUFFER_DESC sd = vd;
    sd.Usage          = D3D11_USAGE_STAGING;
    sd.BindFlags      = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags      = 0;
    ID3D11Buffer* staging = nullptr;
    if (FAILED(device->CreateBuffer(&sd, nullptr, &staging))) return;

    ID3D11DeviceContext* ctx = nullptr;
    device->GetImmediateContext(&ctx);
    ctx->CopyResource(staging, mesh->vb);

    std::vector<uint8_t> cpu_verts(need_bytes);
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
        staging->Release(); ctx->Release(); return;
    }
    std::memcpy(cpu_verts.data(), map.pData, need_bytes);
    ctx->Unmap(staging, 0);
    staging->Release();

    for (size_t i = 0; i < vert_count; ++i) {
        float* p = reinterpret_cast<float*>(
            cpu_verts.data() + i * vstride);
        p[0] = s.positions[i * 3 + 0];
        p[1] = s.positions[i * 3 + 1];
        p[2] = s.positions[i * 3 + 2];
    }

    for (int gz = 0; gz < s.height; ++gz) {
        for (int gx = 0; gx < s.width; ++gx) {
            const size_t i = size_t(gz) * s.width + gx;
            auto pos_at = [&](int x, int z) -> const float* {
                x = std::clamp(x, 0, s.width - 1);
                z = std::clamp(z, 0, s.height - 1);
                return &s.positions[(size_t(z) * s.width + x) * 3];
            };
            const float* xl = pos_at(gx - 1, gz);
            const float* xr = pos_at(gx + 1, gz);
            const float* zl = pos_at(gx, gz - 1);
            const float* zr = pos_at(gx, gz + 1);
            const float dxx = xr[0] - xl[0];
            const float dxy = xr[1] - xl[1];
            const float dzy = zr[1] - zl[1];
            const float dzz = zr[2] - zl[2];
            float nx = -dxy * dzz;
            float ny = dxx * dzz;
            float nz = -dxx * dzy;
            const float len =
                std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-6f) {
                nx /= len;
                ny /= len;
                nz /= len;
            } else {
                nx = 0.0f;
                ny = 1.0f;
                nz = 0.0f;
            }
            float* vtx = reinterpret_cast<float*>(
                cpu_verts.data() + i * vstride);
            vtx[3] = nx;
            vtx[4] = ny;
            vtx[5] = nz;
        }
    }

    D3D11_BUFFER_DESC nd = vd;
    nd.Usage          = D3D11_USAGE_IMMUTABLE;
    nd.CPUAccessFlags = 0;
    D3D11_SUBRESOURCE_DATA isd{};
    isd.pSysMem = cpu_verts.data();
    ID3D11Buffer* new_vb = nullptr;
    if (SUCCEEDED(device->CreateBuffer(&nd, &isd, &new_vb))) {
        mesh->vb->Release();
        mesh->vb = new_vb;
    }
    ctx->Release();
}

#endif

namespace {

bool gzip_compress(const std::vector<uint8_t>& payload,
                   std::vector<uint8_t>&       out)
{
    out.clear();
    if (payload.empty()) return false;
    z_stream zs{};
    if (deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED,
                     15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return false;
    zs.next_in  = const_cast<Bytef*>(payload.data());
    zs.avail_in = (uInt)payload.size();

    out.resize(payload.size() + 1024);
    size_t produced = 0;
    while (true) {
        zs.next_out  = out.data() + produced;
        zs.avail_out = (uInt)(out.size() - produced);
        int rc = deflate(&zs, Z_FINISH);
        produced = out.size() - zs.avail_out;
        if (rc == Z_STREAM_END) break;
        if (rc == Z_OK) {
            out.resize(out.size() * 2);
            continue;
        }
        deflateEnd(&zs);
        out.clear();
        return false;
    }
    deflateEnd(&zs);
    out.resize(produced);
    return true;
}

}

bool Save(std::string& out_path_or_error) {
    auto& s = storage();
    if (!s.loaded) {
        out_path_or_error = "No terrain loaded.";
        return false;
    }
    if (s.ghf_payload_original.empty()) {
        out_path_or_error = "No .ghf payload snapshot; cannot save.";
        return false;
    }
    constexpr size_t kHdrLen   = 0x14;
    constexpr size_t kCellSize = 14;
    const size_t cells = size_t(s.width) * size_t(s.height);
    if (s.ghf_payload_original.size() < kHdrLen + cells * kCellSize) {
        out_path_or_error = "Stored .ghf payload smaller than expected.";
        return false;
    }
    std::vector<uint8_t> payload = s.ghf_payload_original;
    for (size_t i = 0; i < cells; ++i) {
        float h = s.heights_current[i];
        uint32_t bits;
        std::memcpy(&bits, &h, 4);
        uint8_t* p = payload.data() + kHdrLen + i * kCellSize;
        p[0] = uint8_t(bits >> 24);
        p[1] = uint8_t(bits >> 16);
        p[2] = uint8_t(bits >>  8);
        p[3] = uint8_t(bits      );
    }

    std::vector<uint8_t> gz;
    if (!gzip_compress(payload, gz)) {
        out_path_or_error = "gzip compression failed.";
        return false;
    }

    std::filesystem::path basename =
        std::filesystem::path(s.ghf_full_path).filename();
    if (basename.empty()) basename = "terrain.ghf";
    std::filesystem::path out_dir;
    {
        std::filesystem::path root_p(S.root_dir);
        std::error_code rec;
        if (!S.root_dir.empty()) {
            if (std::filesystem::is_regular_file(root_p, rec)) {
                root_p = root_p.parent_path();
            }
            out_dir = root_p / "edited_heightfields";
        } else {
            out_dir = std::filesystem::current_path()
                    / "edited_heightfields";
        }
    }
    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    const auto sibling_path = out_dir / basename;
    {
        std::ofstream f(sibling_path, std::ios::binary);
        if (!f) {
            out_path_or_error = "Could not open backup file: "
                              + sibling_path.string();
            return false;
        }
        f.write(reinterpret_cast<const char*>(gz.data()),
                std::streamsize(gz.size()));
    }
    OutputLog::info("Edit Terrain: backup written to "
        + sibling_path.string()
        + " (" + std::to_string(gz.size()) + " bytes)");

    const bool is_iso = ISO::IsoMount::is_iso_path(s.ghf_bnk_path);
    if (!is_iso) {
        out_path_or_error =
            "Saved to backup; BNK is not in a mounted ISO. ("
            + sibling_path.string() + ")";
        s.dirty = false;
        return true;
    }
    if (s.ghf_bnk_entry_is_compressed) {
        out_path_or_error =
            "Saved to backup; .ghf entry is BNK-chunked (in-place "
            "splice not supported yet). (" + sibling_path.string() + ")";
        s.dirty = false;
        return true;
    }
    if (s.ghf_bnk_entry_on_disk_size == 0) {
        out_path_or_error =
            "Saved to backup; missing BNK entry locator info. ("
            + sibling_path.string() + ")";
        s.dirty = false;
        return true;
    }
    if (gz.size() > s.ghf_bnk_entry_on_disk_size) {
        std::ostringstream os;
        os << "Saved to backup; new gzipped size (" << gz.size()
           << " B) exceeds the original BNK slot ("
           << s.ghf_bnk_entry_on_disk_size << " B), refusing in-place "
           "write. (" << sibling_path.string() << ")";
        out_path_or_error = os.str();
        s.dirty = false;
        return true;
    }

    std::vector<uint8_t> padded(s.ghf_bnk_entry_on_disk_size, 0);
    std::memcpy(padded.data(), gz.data(), gz.size());

    const std::string bnk_vpath =
        ISO::IsoMount::strip_iso_prefix(s.ghf_bnk_path);
    std::string backup_error;
    if (!ISO::Writeback::EnsureBackedUp({s.ghf_bnk_path}, backup_error)) {
        out_path_or_error = backup_error;
        return false;
    }
    if (!ISO::IsoMount::instance().write_at(
            bnk_vpath, s.ghf_bnk_entry_offset,
            padded.data(), padded.size()))
    {
        out_path_or_error =
            "Saved to backup; in-place ISO write FAILED (could not "
            "open ISO r+b or seek/write).  Use the backup at "
            + sibling_path.string();
        s.dirty = false;
        return false;
    }

    std::ostringstream ok;
    ok << "Patched ISO in place - BNK \"" << bnk_vpath
       << "\" + 0x" << std::hex << s.ghf_bnk_entry_offset
       << " <- " << std::dec << gz.size() << " B gzip (slot "
       << s.ghf_bnk_entry_on_disk_size << " B).  Backup: "
       << sibling_path.string();
    out_path_or_error = ok.str();
    s.dirty = false;
    return true;
}

void MarkSaved() {
    auto& s = storage();
    if (!s.loaded) return;
    s.heights_original = s.heights_current;
    s.dirty = false;
}

void Clear() {
    storage() = State{};
}

}
