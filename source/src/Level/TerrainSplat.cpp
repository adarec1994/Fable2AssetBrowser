#include "TerrainSplat.h"
#include "EhfChunkParser.h"
#include "EhfLodThumbnails.h"
#include "../UI/OutputLog.h"
#include <sstream>

#ifdef _WIN32
#include <d3d11.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace TerrainSplat {

namespace {

Resources& storage() {
    static Resources r;
    return r;
}

uint32_t next_generation() {
    static uint32_t g = 1;
    return g++;
}

#ifdef _WIN32
void release_srv(ID3D11ShaderResourceView*& srv) {
    if (srv) { srv->Release(); srv = nullptr; }
}

bool readback_srv_rgba(ID3D11Device* dev,
                       ID3D11ShaderResourceView* srv,
                       std::vector<uint8_t>& out_rgba,
                       int& out_w, int& out_h)
{
    out_rgba.clear(); out_w = out_h = 0;
    if (!srv) return false;

    ID3D11Resource* res = nullptr;
    srv->GetResource(&res);
    if (!res) return false;
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(res->QueryInterface(__uuidof(ID3D11Texture2D),
                                   (void**)&tex))) {
        res->Release();
        return false;
    }
    res->Release();

    D3D11_TEXTURE2D_DESC td{};
    tex->GetDesc(&td);
    out_w = (int)td.Width;
    out_h = (int)td.Height;

    D3D11_TEXTURE2D_DESC sd = td;
    sd.MipLevels = 1;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    if (FAILED(dev->CreateTexture2D(&sd, nullptr, &staging))) {
        tex->Release();
        return false;
    }

    ID3D11DeviceContext* ctx = nullptr;
    dev->GetImmediateContext(&ctx);
    ctx->CopySubresourceRegion(staging, 0, 0, 0, 0, tex, 0, nullptr);

    D3D11_MAPPED_SUBRESOURCE map{};
    HRESULT hr = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map);
    if (FAILED(hr)) {
        staging->Release(); tex->Release(); ctx->Release();
        return false;
    }
    out_rgba.resize(size_t(out_w) * size_t(out_h) * 4);
    const uint8_t* src = (const uint8_t*)map.pData;
    for (int y = 0; y < out_h; ++y) {
        std::memcpy(out_rgba.data() + size_t(y) * out_w * 4,
                    src + size_t(y) * map.RowPitch,
                    size_t(out_w) * 4);
    }
    ctx->Unmap(staging, 0);
    staging->Release();
    tex->Release();
    ctx->Release();
    return true;
}

void resize_rgba8(const std::vector<uint8_t>& src, int sw, int sh,
                  std::vector<uint8_t>& dst, int dw, int dh)
{
    dst.assign(size_t(dw) * size_t(dh) * 4, 0);
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    const float sx_scale = float(sw) / float(dw);
    const float sy_scale = float(sh) / float(dh);
    for (int y = 0; y < dh; ++y) {
        const float sy = (float(y) + 0.5f) * sy_scale - 0.5f;
        const int sy0 = std::clamp(int(std::floor(sy)), 0, sh - 1);
        const int sy1 = std::min(sy0 + 1, sh - 1);
        const float fy = std::clamp(sy - float(sy0), 0.0f, 1.0f);
        for (int x = 0; x < dw; ++x) {
            const float sx = (float(x) + 0.5f) * sx_scale - 0.5f;
            const int sx0 = std::clamp(int(std::floor(sx)), 0, sw - 1);
            const int sx1 = std::min(sx0 + 1, sw - 1);
            const float fx = std::clamp(sx - float(sx0), 0.0f, 1.0f);
            const uint8_t* p00 = src.data() + (size_t(sy0) * sw + sx0) * 4;
            const uint8_t* p10 = src.data() + (size_t(sy0) * sw + sx1) * 4;
            const uint8_t* p01 = src.data() + (size_t(sy1) * sw + sx0) * 4;
            const uint8_t* p11 = src.data() + (size_t(sy1) * sw + sx1) * 4;
            uint8_t* q = dst.data() + (size_t(y) * dw + x) * 4;
            const float w00 = (1.0f - fx) * (1.0f - fy);
            const float w10 = fx * (1.0f - fy);
            const float w01 = (1.0f - fx) * fy;
            const float w11 = fx * fy;
            for (int c = 0; c < 4; ++c) {
                q[c] = uint8_t(std::clamp(
                    p00[c] * w00 + p10[c] * w10 +
                    p01[c] * w01 + p11[c] * w11,
                    0.0f, 255.0f));
            }
        }
    }
}
#endif

}

const Resources& Get() {
    return storage();
}

void Clear() {
#ifdef _WIN32
    auto& s = storage();
    release_srv(s.lod_diffuse_array);
    release_srv(s.lod_detail_array);
    release_srv(s.chunk_idx_array);
    release_srv(s.chunk_blend_array);
    release_srv(s.chunk_uv_array);
    release_srv(s.splat_mask);
    release_srv(s.lightmap);
    release_srv(s.material_weight_array);
    s = Resources{};
#endif
}

#ifdef _WIN32

bool Build(ID3D11Device*                                       device,
           const Level::EhfParsedBody&                          parsed,
           const std::vector<EhfLodThumbnails::Entry>&          lod_thumbs,
           const std::vector<uint8_t>&                          lightmap_rgba,
           int                                                  lightmap_w,
           int                                                  lightmap_h,
           float                                                mesh_to_world_x,
           float                                                mesh_to_world_z,
           bool                                                 build_material_weights)
{
    Clear();
    auto& R = storage();

    if (!device || parsed.chunks.empty() || lod_thumbs.empty()) {
        return false;
    }

    R.generation = next_generation();
    R.lod_count = (int)lod_thumbs.size();
    R.mesh_to_world_x = mesh_to_world_x;
    R.mesh_to_world_z = mesh_to_world_z;
    R.tile_scale = 0.125f;
    for (int i = 0; i < kMaxMaterials; ++i) {
        R.material_params[i][0] = 0.125f;
        R.material_params[i][1] = 0.125f;
        R.material_params[i][2] = 0.0f;
        R.material_params[i][3] = 1.0f;
    }
    for (size_t i = 0; i < lod_thumbs.size() && i < kMaxMaterials; ++i) {
        const auto& e = lod_thumbs[i];
        const float base_scale =
            (e.base_tile_scale > 0.0f && e.base_tile_scale < 16.0f)
                ? e.base_tile_scale : 0.125f;
        const float detail_scale =
            (e.detail_tile_scale > 0.0f && e.detail_tile_scale < 16.0f)
                ? e.detail_tile_scale : base_scale;
        R.material_params[i][0] = base_scale;
        R.material_params[i][1] = detail_scale;
        R.material_params[i][2] = e.srv_detail_diffuse ? 1.0f : 0.0f;
        R.material_params[i][3] =
            (e.detail_intensity > 0.0f && e.detail_intensity < 16.0f)
                ? e.detail_intensity : 1.0f;
        if (i == 0) R.tile_scale = base_scale;
    }

    float world_min_x = parsed.chunks[0].origin[0];
    float world_min_z = parsed.chunks[0].origin[1];
    float world_max_x = parsed.chunks[0].extent[0];
    float world_max_z = parsed.chunks[0].extent[1];
    for (const auto& c : parsed.chunks) {
        world_min_x = std::min(world_min_x, c.origin[0]);
        world_min_z = std::min(world_min_z, c.origin[1]);
        world_max_x = std::max(world_max_x, c.extent[0]);
        world_max_z = std::max(world_max_z, c.extent[1]);
    }

    {
        const int W = kCommonLodSize, H = kCommonLodSize;
        const int N = R.lod_count;
        if (N <= 0) return false;

        int max_mip = 1;
        for (int s = W; s > 1; s >>= 1) ++max_mip;

        auto make_neutral_detail = [&]() {
            std::vector<uint8_t> neutral(size_t(W) * size_t(H) * 4, 0x80);
            for (size_t i = 3; i < neutral.size(); i += 4) neutral[i] = 0xFF;
            return neutral;
        };

        auto build_array = [&](const char* label,
                               bool detail_array,
                               ID3D11ShaderResourceView*& out_srv,
                               int& out_seeded,
                               int& out_real_seeded) -> bool
        {
            out_srv = nullptr;
            out_seeded = 0;
            out_real_seeded = 0;

            D3D11_TEXTURE2D_DESC td{};
            td.Width            = W;
            td.Height           = H;
            td.MipLevels        = max_mip;
            td.ArraySize        = N;
            td.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage            = D3D11_USAGE_DEFAULT;
            td.BindFlags        = D3D11_BIND_SHADER_RESOURCE
                                | D3D11_BIND_RENDER_TARGET;
            td.MiscFlags        = D3D11_RESOURCE_MISC_GENERATE_MIPS;

            ID3D11Texture2D* arr_tex = nullptr;
            if (FAILED(device->CreateTexture2D(&td, nullptr, &arr_tex))) {
                OutputLog::warn(std::string("TerrainSplat: failed to create ")
                                + label + " array");
                return false;
            }

            ID3D11DeviceContext* ctx = nullptr;
            device->GetImmediateContext(&ctx);

            std::vector<uint8_t> neutral;
            std::vector<uint8_t> base_fallback;
            int fallback_seeded = 0;
            for (int s = 0; s < N; ++s) {
                ID3D11ShaderResourceView* src_srv = nullptr;
                if (detail_array) {
                    src_srv = (lod_thumbs[s].srv_base_diffuse &&
                               lod_thumbs[s].srv_detail_diffuse)
                        ? lod_thumbs[s].srv_detail_diffuse
                        : nullptr;
                } else {
                    src_srv = lod_thumbs[s].srv_base_diffuse
                        ? lod_thumbs[s].srv_base_diffuse
                        : lod_thumbs[s].srv_detail_diffuse;
                }

                std::vector<uint8_t> resized;
                const std::vector<uint8_t>* direct_rgba = nullptr;
                int direct_w = 0;
                int direct_h = 0;
                if (detail_array) {
                    if (!lod_thumbs[s].detail_diffuse_rgba.empty()) {
                        direct_rgba = &lod_thumbs[s].detail_diffuse_rgba;
                        direct_w = lod_thumbs[s].detail_diffuse_w;
                        direct_h = lod_thumbs[s].detail_diffuse_h;
                    }
                } else if (!lod_thumbs[s].base_diffuse_rgba.empty()) {
                    direct_rgba = &lod_thumbs[s].base_diffuse_rgba;
                    direct_w = lod_thumbs[s].base_diffuse_w;
                    direct_h = lod_thumbs[s].base_diffuse_h;
                } else if (!lod_thumbs[s].detail_diffuse_rgba.empty()) {
                    direct_rgba = &lod_thumbs[s].detail_diffuse_rgba;
                    direct_w = lod_thumbs[s].detail_diffuse_w;
                    direct_h = lod_thumbs[s].detail_diffuse_h;
                }

                if (direct_rgba && direct_w > 0 && direct_h > 0) {
                    resize_rgba8(*direct_rgba, direct_w, direct_h,
                                 resized, W, H);
                    if (!detail_array && base_fallback.empty()) {
                        base_fallback = resized;
                    }
                    ++out_real_seeded;
                } else if (src_srv) {
                    std::vector<uint8_t> src_rgba;
                    int sw = 0, sh = 0;
                    if (readback_srv_rgba(device, src_srv,
                                          src_rgba, sw, sh) &&
                        !src_rgba.empty() && sw > 0 && sh > 0) {
                        resize_rgba8(src_rgba, sw, sh, resized, W, H);
                        if (!detail_array && base_fallback.empty()) {
                            base_fallback = resized;
                        }
                        ++out_real_seeded;
                    }
                } else if (detail_array) {
                    if (neutral.empty()) neutral = make_neutral_detail();
                    resized = neutral;
                }

                if (resized.empty() && !detail_array && !base_fallback.empty()) {
                    resized = base_fallback;
                    ++fallback_seeded;
                }
                if (resized.empty()) {
                    continue;
                }

                const UINT sub = D3D11CalcSubresource(0, s, max_mip);
                ctx->UpdateSubresource(arr_tex, sub, nullptr,
                                       resized.data(),
                                       UINT(W * 4), UINT(W * H * 4));
                ++out_seeded;
            }

            if (out_seeded == 0) {
                arr_tex->Release(); ctx->Release();
                OutputLog::warn(std::string("TerrainSplat: no ")
                                + label + " slices seeded");
                return false;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
            sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            sd.Texture2DArray.MostDetailedMip = 0;
            sd.Texture2DArray.MipLevels = max_mip;
            sd.Texture2DArray.FirstArraySlice = 0;
            sd.Texture2DArray.ArraySize = N;
            ID3D11ShaderResourceView* arr_srv = nullptr;
            if (FAILED(device->CreateShaderResourceView(arr_tex, &sd,
                                                        &arr_srv))) {
                arr_tex->Release(); ctx->Release();
                OutputLog::warn(std::string("TerrainSplat: failed to create ")
                                + label + " array SRV");
                return false;
            }
            ctx->GenerateMips(arr_srv);
            arr_tex->Release();
            ctx->Release();

            out_srv = arr_srv;
            OutputLog::info("TerrainSplat: " + std::string(label) + " array "
                + std::to_string(N) + " slices @ " + std::to_string(W) + "x"
                + std::to_string(H) + " (" + std::to_string(out_seeded)
                + " seeded, " + std::to_string(out_real_seeded)
                + " from EHF texture paths, "
                + std::to_string(fallback_seeded) + " fallback)");
            return true;
        };

        int base_seeded = 0, base_real = 0;
        int detail_seeded = 0, detail_real = 0;
        if (!build_array("base diffuse", false, R.lod_diffuse_array,
                         base_seeded, base_real)) {
            return false;
        }
        if (!build_array("detail diffuse", true, R.lod_detail_array,
                         detail_seeded, detail_real)) {
            return false;
        }
    }

    {
        if (parsed.chunk_w == 0 || parsed.chunk_h == 0) {
            OutputLog::warn("TerrainSplat: empty EHF chunk grid");
            return false;
        }

        R.chunk_w = (int)parsed.chunk_w;
        R.chunk_h = (int)parsed.chunk_h;
        R.world_origin_x = world_min_x;
        R.world_origin_z = world_min_z;
        R.chunk_extent_x = (world_max_x - world_min_x) / std::max(1, R.chunk_w);
        R.chunk_extent_z = (world_max_z - world_min_z) / std::max(1, R.chunk_h);

        const int CW = R.chunk_w, CH = R.chunk_h;
        const int L  = kMaxLayers;
        std::vector<uint8_t> idx_data(size_t(CW) * CH * 4 * L, 0xFF);
        std::vector<uint8_t> bln_data(size_t(CW) * CH * 4 * L, 0);
        std::vector<float> uv_data(size_t(CW) * CH * 4 * L, 0.0f);
        std::vector<uint8_t> material_used(
            size_t(std::max(0, R.lod_count)), 0);

        size_t total_records = 0;
        size_t truncated_records = 0;
        float atlas_min_u =  1e30f, atlas_min_v =  1e30f;
        float atlas_max_u = -1e30f, atlas_max_v = -1e30f;
        size_t corner_refs_resolved = 0;
        size_t corner_refs_remapped = 0;
        size_t corner_refs_oob = 0;

        const size_t expected_chunks = size_t(CW) * size_t(CH);
        for (size_t ci = 0; ci < parsed.chunks.size() && ci < expected_chunks; ++ci) {
            const int cx = int(ci / size_t(CH));
            const int cy = int(ci % size_t(CH));
            const auto& chunk = parsed.chunks[ci];
            total_records += chunk.layers.size();

            const int layer_count =
                std::min<int>((int)chunk.layers.size(), L);
            truncated_records += chunk.layers.size() - size_t(layer_count);

            for (int li = 0; li < layer_count; ++li) {
                const auto& layer = chunk.layers[size_t(li)];
                const size_t texel = size_t(cy) * CW + size_t(cx);
                const size_t base = (size_t(li) * CW * CH + texel) * 4;
                const size_t uv_base = (size_t(li) * CW * CH + texel) * 4;
                for (int i = 0; i < 4; ++i) {
                    uint32_t mat_idx = layer.material_idx;
                    const uint32_t corner_layer_idx = layer.texture_idx[i];
                    if (corner_layer_idx < chunk.layers.size()) {
                        ++corner_refs_resolved;
                        mat_idx = chunk.layers[size_t(corner_layer_idx)].material_idx;
                        if (mat_idx != layer.material_idx) {
                            ++corner_refs_remapped;
                        }
                    } else {
                        ++corner_refs_oob;
                    }

                    const uint8_t material =
                        (mat_idx < 255u)
                            ? uint8_t(std::min<uint32_t>(
                                  mat_idx,
                                  uint32_t(std::max(0, R.lod_count - 1))))
                            : 0xFFu;
                    idx_data[base + i] = material;
                    bln_data[base + i] = layer.blend[i];
                    if (material < material_used.size()) {
                        material_used[material] = 1;
                    }
                }
                uv_data[uv_base + 0] = layer.tile_uv[0];
                uv_data[uv_base + 1] = layer.tile_uv[1];
                uv_data[uv_base + 2] = layer.mask_scale[0];
                uv_data[uv_base + 3] = layer.mask_scale[1];
                atlas_min_u = std::min(atlas_min_u, layer.tile_uv[0]);
                atlas_min_v = std::min(atlas_min_v, layer.tile_uv[1]);
                atlas_max_u = std::max(atlas_max_u, layer.tile_uv[0]);
                atlas_max_v = std::max(atlas_max_v, layer.tile_uv[1]);
            }
        }

        {
            std::ostringstream s;
            s << "TerrainSplat: EHF paint chunks " << R.chunk_w
              << "x" << R.chunk_h << " with " << total_records
              << " layer record(s); world=[" << world_min_x << ".."
              << world_max_x << ", " << world_min_z << ".." << world_max_z
              << "] cell=(" << R.chunk_extent_x << "," << R.chunk_extent_z
              << ") mesh_to_world=(" << mesh_to_world_x << ","
              << mesh_to_world_z << ")";
            OutputLog::info(s.str());
        }
        if (total_records > 0) {
            std::ostringstream s;
            s << "TerrainSplat: layer tile_uv atlas range=["
              << atlas_min_u << ".." << atlas_max_u << ", "
              << atlas_min_v << ".." << atlas_max_v << "]";
            OutputLog::info(s.str());
        }
        if (!material_used.empty()) {
            size_t used_count = 0;
            std::ostringstream s;
            s << "TerrainSplat: referenced material slots";
            for (size_t i = 0; i < material_used.size(); ++i) {
                if (!material_used[i]) continue;
                ++used_count;
                s << (used_count == 1 ? " " : ", ") << i;
            }
            s << " (" << used_count << "/" << material_used.size() << ")";
            OutputLog::info(s.str());
        }
        {
            std::ostringstream s;
            s << "TerrainSplat: corner layer refs resolved="
              << corner_refs_resolved
              << " remapped=" << corner_refs_remapped
              << " oob=" << corner_refs_oob;
            OutputLog::info(s.str());
        }
        if (truncated_records) {
            OutputLog::warn("TerrainSplat: truncated "
                + std::to_string(truncated_records)
                + " layer records beyond shader layer limit");
        }

        D3D11_TEXTURE2D_DESC td{};
        td.Width            = CW;
        td.Height           = CH;
        td.MipLevels        = 1;
        td.ArraySize        = L;
        td.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage            = D3D11_USAGE_DEFAULT;
        td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

        std::vector<D3D11_SUBRESOURCE_DATA> subs(L);
        for (int s = 0; s < L; ++s) {
            subs[s].pSysMem = idx_data.data()
                + size_t(s) * CW * CH * 4;
            subs[s].SysMemPitch = UINT(CW * 4);
            subs[s].SysMemSlicePitch = 0;
        }
        ID3D11Texture2D* idx_tex = nullptr;
        if (FAILED(device->CreateTexture2D(&td, subs.data(), &idx_tex))) {
            OutputLog::warn("TerrainSplat: failed to create chunk_idx");
            return false;
        }
        for (int s = 0; s < L; ++s) {
            subs[s].pSysMem = bln_data.data()
                + size_t(s) * CW * CH * 4;
        }
        ID3D11Texture2D* bln_tex = nullptr;
        if (FAILED(device->CreateTexture2D(&td, subs.data(), &bln_tex))) {
            idx_tex->Release();
            OutputLog::warn("TerrainSplat: failed to create chunk_blend");
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        sd.Texture2DArray.MostDetailedMip = 0;
        sd.Texture2DArray.MipLevels = 1;
        sd.Texture2DArray.FirstArraySlice = 0;
        sd.Texture2DArray.ArraySize = L;
        if (FAILED(device->CreateShaderResourceView(idx_tex, &sd,
                                                    &R.chunk_idx_array))) {
            idx_tex->Release(); bln_tex->Release();
            return false;
        }
        if (FAILED(device->CreateShaderResourceView(bln_tex, &sd,
                                                    &R.chunk_blend_array))) {
            release_srv(R.chunk_idx_array);
            idx_tex->Release(); bln_tex->Release();
            return false;
        }
        idx_tex->Release();
        bln_tex->Release();

        D3D11_TEXTURE2D_DESC uvd{};
        uvd.Width            = CW;
        uvd.Height           = CH;
        uvd.MipLevels        = 1;
        uvd.ArraySize        = L;
        uvd.Format           = DXGI_FORMAT_R32G32B32A32_FLOAT;
        uvd.SampleDesc.Count = 1;
        uvd.Usage            = D3D11_USAGE_DEFAULT;
        uvd.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

        std::vector<D3D11_SUBRESOURCE_DATA> uv_subs(L);
        for (int s = 0; s < L; ++s) {
            uv_subs[s].pSysMem = uv_data.data()
                + size_t(s) * CW * CH * 4;
            uv_subs[s].SysMemPitch = UINT(CW * 4 * sizeof(float));
            uv_subs[s].SysMemSlicePitch = 0;
        }
        ID3D11Texture2D* uv_tex = nullptr;
        if (FAILED(device->CreateTexture2D(&uvd, uv_subs.data(), &uv_tex))) {
            release_srv(R.chunk_idx_array);
            release_srv(R.chunk_blend_array);
            return false;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC uv_sd{};
        uv_sd.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        uv_sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        uv_sd.Texture2DArray.MostDetailedMip = 0;
        uv_sd.Texture2DArray.MipLevels = 1;
        uv_sd.Texture2DArray.FirstArraySlice = 0;
        uv_sd.Texture2DArray.ArraySize = L;
        if (FAILED(device->CreateShaderResourceView(uv_tex, &uv_sd,
                                                    &R.chunk_uv_array))) {
            uv_tex->Release();
            release_srv(R.chunk_idx_array);
            release_srv(R.chunk_blend_array);
            return false;
        }
        uv_tex->Release();
        OutputLog::info("TerrainSplat: paint LUT " + std::to_string(CW)
            + "x" + std::to_string(CH) + " x " + std::to_string(L)
            + " layers");
    }

    {
        const bool has_pf99 =
            !parsed.splat_indices.empty() &&
            parsed.splat_w > 0 && parsed.splat_h > 0 &&
            parsed.splat_indices.size() ==
                size_t(parsed.splat_w) * size_t(parsed.splat_h);

        R.splat_w = has_pf99 ? (int)parsed.splat_w : 1;
        R.splat_h = has_pf99 ? (int)parsed.splat_h : 1;

        std::vector<uint8_t> mask;
        if (has_pf99) {
            mask = parsed.splat_indices;
        } else {
            mask.assign(1, 0xFF);
        }

        D3D11_TEXTURE2D_DESC td{};
        td.Width            = (UINT)R.splat_w;
        td.Height           = (UINT)R.splat_h;
        td.MipLevels        = 1;
        td.ArraySize        = 1;
        td.Format           = DXGI_FORMAT_R8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage            = D3D11_USAGE_DEFAULT;
        td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem     = mask.data();
        sd.SysMemPitch = (UINT)R.splat_w;

        ID3D11Texture2D* tex = nullptr;
        if (FAILED(device->CreateTexture2D(&td, &sd, &tex))) {
            release_srv(R.chunk_idx_array);
            release_srv(R.chunk_blend_array);
            release_srv(R.chunk_uv_array);
            OutputLog::warn("TerrainSplat: failed to create PF99 splat mask");
            return false;
        }
        if (FAILED(device->CreateShaderResourceView(tex, nullptr,
                                                    &R.splat_mask))) {
            tex->Release();
            release_srv(R.chunk_idx_array);
            release_srv(R.chunk_blend_array);
            release_srv(R.chunk_uv_array);
            OutputLog::warn("TerrainSplat: failed to create PF99 mask SRV");
            return false;
        }
        tex->Release();

        if (has_pf99) {
            size_t soft = 0;
            size_t black = 0;
            size_t white = 0;
            for (uint8_t v : mask) {
                if (v == 0) ++black;
                else if (v == 255) ++white;
                else ++soft;
            }
            OutputLog::info("TerrainSplat: PF99 layer mask atlas "
                + std::to_string(R.splat_w) + "x"
                + std::to_string(R.splat_h) + " ("
                + std::to_string(R.splat_w / 33) + "x"
                + std::to_string(R.splat_h / 33)
                + " tiles @ 33x33, "
                + std::to_string(soft) + " soft pixels, "
                + std::to_string(black) + " black, "
                + std::to_string(white) + " white)");
        } else {
            OutputLog::warn("TerrainSplat: no PF99 layer mask atlas");
        }
    }

    if (build_material_weights) {
        const bool has_pf99 =
            !parsed.splat_indices.empty() &&
            parsed.splat_w > 0 && parsed.splat_h > 0 &&
            parsed.splat_indices.size() ==
                size_t(parsed.splat_w) * size_t(parsed.splat_h);
        if (!has_pf99) {
            OutputLog::warn(
                "TerrainSplat: cannot build global material weights without PF99 mask");
            Clear();
            return false;
        }

        const int CW = R.chunk_w;
        const int CH = R.chunk_h;
        const int N = std::min<int>(std::max(0, R.lod_count),
                                    kMaxMaterials);
        if (CW <= 0 || CH <= 0 || N <= 0) {
            Clear();
            return false;
        }

        R.weight_w = CW * 32 + 1;
        R.weight_h = CH * 32 + 1;
        const size_t area =
            size_t(R.weight_w) * size_t(R.weight_h);
        std::vector<float> weights(size_t(N) * area, 0.0f);

        const auto mask_sample = [&](const Level::EhfChunkLayer& layer,
                                     int px, int py) -> float
        {
            // Each chunk layer references its OWN paint mask through name_idx
            // (the engine samples that per-layer resource, not one shared
            // splat map). Prefer the layer's referenced resource; fall back
            // to the shared splat_indices. Mirrors LevelExport::mask_sample
            // so the preview bake matches the export bake.
            const std::vector<uint8_t>* map = &parsed.splat_indices;
            int mw = int(parsed.splat_w);
            int mh = int(parsed.splat_h);
            if (size_t(layer.name_idx) < parsed.paint_resources.size()) {
                const auto& pr =
                    parsed.paint_resources[size_t(layer.name_idx)];
                if (!pr.data.empty() && pr.width > 0 && pr.height > 0) {
                    map = &pr.data;
                    mw  = int(pr.width);
                    mh  = int(pr.height);
                }
            }
            if (map->empty() || mw <= 0 || mh <= 0 ||
                map->size() != size_t(mw) * size_t(mh)) {
                return 1.0f;
            }
            const int ox = std::clamp(
                int(std::floor(layer.tile_uv[0] * float(mw))), 0, mw - 1);
            const int oy = std::clamp(
                int(std::floor(layer.tile_uv[1] * float(mh))), 0, mh - 1);
            const int sx = std::clamp(ox + px, 0, mw - 1);
            const int sy = std::clamp(oy + py, 0, mh - 1);
            return float((*map)[size_t(sy) * size_t(mw) + size_t(sx)])
                 / 255.0f;
        };

        size_t global_contribs = 0;
        const size_t expected_chunks = size_t(CW) * size_t(CH);
        for (size_t ci = 0;
             ci < parsed.chunks.size() && ci < expected_chunks;
             ++ci)
        {
            const int cx = int(ci / size_t(CH));
            const int cy = int(ci % size_t(CH));
            const auto& chunk = parsed.chunks[ci];
            const int layer_count =
                std::min<int>((int)chunk.layers.size(), kMaxLayers);
            for (int li = 0; li < layer_count; ++li) {
                const auto& layer = chunk.layers[size_t(li)];
                int mat_corner[4] = {};
                for (int i = 0; i < 4; ++i) {
                    uint32_t mat_idx = layer.material_idx;
                    const uint32_t corner_layer_idx = layer.texture_idx[i];
                    if (corner_layer_idx < chunk.layers.size()) {
                        mat_idx = chunk.layers[size_t(corner_layer_idx)]
                                      .material_idx;
                    }
                    mat_corner[i] = std::clamp<int>(int(mat_idx), 0, N - 1);
                }
                const float corner_bw[4] = {
                    std::clamp(float(layer.blend[0]) / 3.0f, 0.0f, 1.0f),
                    std::clamp(float(layer.blend[1]) / 3.0f, 0.0f, 1.0f),
                    std::clamp(float(layer.blend[2]) / 3.0f, 0.0f, 1.0f),
                    std::clamp(float(layer.blend[3]) / 3.0f, 0.0f, 1.0f)
                };
                for (int py = 0; py <= 32; ++py) {
                    const float fy = float(py) / 32.0f;
                    for (int px = 0; px <= 32; ++px) {
                        const float fx = float(px) / 32.0f;
                        const float cwx[4] = {
                            (1.0f - fx) * (1.0f - fy),
                                      fx  * (1.0f - fy),
                            (1.0f - fx) *        fy,
                                      fx  *        fy
                        };
                        const float m = mask_sample(layer, px, py);
                        if (m <= 0.0001f) continue;
                        const int gx = cx * 32 + px;
                        const int gy = cy * 32 + py;
                        if (gx < 0 || gy < 0 ||
                            gx >= R.weight_w || gy >= R.weight_h) {
                            continue;
                        }
                        const size_t dst =
                            size_t(gy) * size_t(R.weight_w) + size_t(gx);
                        for (int k = 0; k < 4; ++k) {
                            const float cw = m * corner_bw[k] * cwx[k];
                            if (cw <= 0.0001f) continue;
                            weights[size_t(mat_corner[k]) * area + dst] += cw;
                        }
                        ++global_contribs;
                    }
                }
            }
        }

        size_t normalized_pixels = 0;
        size_t empty_pixels = 0;
        for (size_t p = 0; p < area; ++p) {
            float sum = 0.0f;
            for (int m = 0; m < N; ++m) {
                sum += weights[size_t(m) * area + p];
            }
            if (sum > 0.00001f) {
                const float inv = 1.0f / sum;
                for (int m = 0; m < N; ++m) {
                    weights[size_t(m) * area + p] *= inv;
                }
                ++normalized_pixels;
            } else {
                weights[p] = 1.0f;
                ++empty_pixels;
            }
        }

        D3D11_TEXTURE2D_DESC td{};
        td.Width            = UINT(R.weight_w);
        td.Height           = UINT(R.weight_h);
        td.MipLevels        = 1;
        td.ArraySize        = UINT(N);
        td.Format           = DXGI_FORMAT_R32_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage            = D3D11_USAGE_DEFAULT;
        td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

        std::vector<D3D11_SUBRESOURCE_DATA> subs(static_cast<size_t>(N));
        for (int s = 0; s < N; ++s) {
            subs[size_t(s)].pSysMem =
                weights.data() + size_t(s) * area;
            subs[size_t(s)].SysMemPitch =
                UINT(R.weight_w * sizeof(float));
            subs[size_t(s)].SysMemSlicePitch = 0;
        }

        ID3D11Texture2D* tex = nullptr;
        if (FAILED(device->CreateTexture2D(&td, subs.data(), &tex))) {
            OutputLog::warn(
                "TerrainSplat: failed to create global material weights");
            Clear();
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R32_FLOAT;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        sd.Texture2DArray.MostDetailedMip = 0;
        sd.Texture2DArray.MipLevels = 1;
        sd.Texture2DArray.FirstArraySlice = 0;
        sd.Texture2DArray.ArraySize = UINT(N);
        if (FAILED(device->CreateShaderResourceView(
                tex, &sd, &R.material_weight_array))) {
            tex->Release();
            OutputLog::warn(
                "TerrainSplat: failed to create global material weight SRV");
            Clear();
            return false;
        }
        tex->Release();

        OutputLog::info("TerrainSplat: global material weights "
            + std::to_string(R.weight_w) + "x"
            + std::to_string(R.weight_h) + " x "
            + std::to_string(N) + " materials ("
            + std::to_string(global_contribs)
            + " layer samples, "
            + std::to_string(normalized_pixels)
            + " painted pixels, "
            + std::to_string(empty_pixels)
            + " fallback pixels)");
    }

    if (!lightmap_rgba.empty() && lightmap_w > 0 && lightmap_h > 0) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width            = lightmap_w;
        td.Height           = lightmap_h;
        td.MipLevels        = 1;
        td.ArraySize        = 1;
        td.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage            = D3D11_USAGE_DEFAULT;
        td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem      = lightmap_rgba.data();
        sd.SysMemPitch  = UINT(lightmap_w * 4);

        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(device->CreateTexture2D(&td, &sd, &tex))) {
            device->CreateShaderResourceView(tex, nullptr, &R.lightmap);
            tex->Release();
        }
    }

    R.ok = true;
    return true;
}

#endif

}
