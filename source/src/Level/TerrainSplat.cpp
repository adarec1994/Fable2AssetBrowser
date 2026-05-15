#include "TerrainSplat.h"
#include "EhfChunkParser.h"
#include "EhfLodThumbnails.h"
#include "../UI/OutputLog.h"
#include <sstream>

#ifdef _WIN32
#include <d3d11.h>
#endif

#include <algorithm>
#include <cstring>

namespace TerrainSplat {

namespace {

Resources& storage() {
    static Resources r;
    return r;
}

#ifdef _WIN32
void release_srv(ID3D11ShaderResourceView*& srv) {
    if (srv) { srv->Release(); srv = nullptr; }
}

/* Read an SRV's underlying Texture2D back to a flat RGBA8 vector via
   a STAGING copy.  We need this to seed slices of the LOD texture
   array from the existing per-LOD diffuse SRVs (created at full
   resolution earlier in PendingLoads).                              */
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

    /* Build a STAGING texture (CPU-readable). */
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
    /* Copy mip 0 only. */
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

/* Box-filter resize RGBA8 src (sw×sh) → dst (dw×dh).  Cheap, good
   enough for our purposes (the array slices feed into a mip chain
   so any aliasing here gets blurred out anyway).                  */
void resize_rgba8(const std::vector<uint8_t>& src, int sw, int sh,
                  std::vector<uint8_t>& dst, int dw, int dh)
{
    dst.assign(size_t(dw) * size_t(dh) * 4, 0);
    const float sx_scale = float(sw) / float(dw);
    const float sy_scale = float(sh) / float(dh);
    for (int y = 0; y < dh; ++y) {
        const int sy0 = std::min(sh - 1, int(y * sy_scale));
        for (int x = 0; x < dw; ++x) {
            const int sx0 = std::min(sw - 1, int(x * sx_scale));
            const uint8_t* p = src.data() + (size_t(sy0) * sw + sx0) * 4;
            uint8_t* q = dst.data() + (size_t(y) * dw + x) * 4;
            q[0] = p[0]; q[1] = p[1]; q[2] = p[2]; q[3] = p[3];
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
    release_srv(s.chunk_idx_array);
    release_srv(s.chunk_blend_array);
    release_srv(s.lightmap);
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
           float                                                mesh_to_world_z)
{
    Clear();
    auto& R = storage();

    if (!device || parsed.chunks.empty() || lod_thumbs.empty()) {
        return false;
    }

    /* --- 1) Chunk grid placement (assumes uniform chunks). ---------
       .ehf origin/extent format is (X-horiz, Z-horiz, Y-height) where
       Y-height (offset 2) is the chunk's per-corner min/max height.
       For texturing we only care about the 2 horizontal axes.
       `extent` is the MAX-CORNER world position (not the chunk size),
       so the actual chunk size = extent - origin.                    */
    R.chunk_w = (int)parsed.chunk_w;
    R.chunk_h = (int)parsed.chunk_h;
    R.world_origin_x = parsed.chunks[0].origin[1];
    R.world_origin_z = parsed.chunks[0].origin[0];
    R.chunk_extent_x = parsed.chunks[0].extent[1] - parsed.chunks[0].origin[1];
    R.chunk_extent_z = parsed.chunks[0].extent[0] - parsed.chunks[0].origin[0];
    R.lod_count = (int)lod_thumbs.size();
    R.mesh_to_world_x = mesh_to_world_x;
    R.mesh_to_world_z = mesh_to_world_z;

    {
        std::ostringstream s;
        s << "TerrainSplat: chunk0 origin=("
          << parsed.chunks[0].origin[0] << ","
          << parsed.chunks[0].origin[1] << ","
          << parsed.chunks[0].origin[2]
          << ") extent=(" << parsed.chunks[0].extent[0] << ","
          << parsed.chunks[0].extent[1] << ","
          << parsed.chunks[0].extent[2]
          << ")  grid=" << R.chunk_w << "x" << R.chunk_h
          << "  chunk_size=(" << R.chunk_extent_x << ","
          << R.chunk_extent_z << ")"
          << "  mesh_to_world=(" << mesh_to_world_x << ","
          << mesh_to_world_z << ")";
        OutputLog::info(s.str());
    }

    /* --- 2) Build LOD diffuse Texture2DArray. ------------------------ */
    {
        const int W = kCommonLodSize, H = kCommonLodSize;
        const int N = R.lod_count;
        if (N <= 0) return false;

        /* Compute mip levels (full chain). */
        int max_mip = 1;
        for (int s = W; s > 1; s >>= 1) ++max_mip;

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
            OutputLog::warn("TerrainSplat: failed to create LOD array");
            return false;
        }

        ID3D11DeviceContext* ctx = nullptr;
        device->GetImmediateContext(&ctx);

        /* Seed mip 0 of each slice. */
        int seeded = 0;
        for (int s = 0; s < N; ++s) {
            ID3D11ShaderResourceView* src_srv =
                lod_thumbs[s].srv_base_diffuse;
            if (!src_srv) continue;

            std::vector<uint8_t> src_rgba;
            int sw = 0, sh = 0;
            if (!readback_srv_rgba(device, src_srv, src_rgba, sw, sh)) continue;
            if (src_rgba.empty() || sw <= 0 || sh <= 0) continue;

            std::vector<uint8_t> resized;
            resize_rgba8(src_rgba, sw, sh, resized, W, H);

            const UINT sub = D3D11CalcSubresource(0, s, max_mip);
            ctx->UpdateSubresource(arr_tex, sub, nullptr,
                                   resized.data(),
                                   UINT(W * 4), UINT(W * H * 4));
            ++seeded;
        }

        if (seeded == 0) {
            arr_tex->Release(); ctx->Release();
            OutputLog::warn("TerrainSplat: no LOD slices seeded");
            return false;
        }

        /* Create SRV for the array, then generate mip chain. */
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        sd.Texture2DArray.MostDetailedMip = 0;
        sd.Texture2DArray.MipLevels = max_mip;
        sd.Texture2DArray.FirstArraySlice = 0;
        sd.Texture2DArray.ArraySize = N;
        ID3D11ShaderResourceView* arr_srv = nullptr;
        if (FAILED(device->CreateShaderResourceView(arr_tex, &sd, &arr_srv))) {
            arr_tex->Release(); ctx->Release();
            OutputLog::warn("TerrainSplat: failed to create LOD array SRV");
            return false;
        }
        ctx->GenerateMips(arr_srv);
        arr_tex->Release();
        ctx->Release();

        R.lod_diffuse_array = arr_srv;
        OutputLog::info("TerrainSplat: LOD array " + std::to_string(N)
            + " slices @ " + std::to_string(W) + "x" + std::to_string(H)
            + " (" + std::to_string(seeded) + " seeded)");
    }

    /* --- 3) Build chunk_idx and chunk_blend Texture2DArrays. ------- */
    {
        const int CW = R.chunk_w, CH = R.chunk_h;
        const int L  = kMaxLayers;
        std::vector<uint8_t> idx_data(size_t(CW) * CH * 4 * L, 0xFF);
        std::vector<uint8_t> bln_data(size_t(CW) * CH * 4 * L, 0);

        for (int cy = 0; cy < CH; ++cy) {
            for (int cx = 0; cx < CW; ++cx) {
                const auto& chunk =
                    parsed.chunks[size_t(cy) * CW + cx];
                const int layers = std::min((int)chunk.layers.size(), L);
                for (int li = 0; li < layers; ++li) {
                    const auto& Lr = chunk.layers[li];
                    const size_t base = (size_t(li) * CW * CH
                                       + size_t(cy) * CW + cx) * 4;
                    idx_data[base + 0] = Lr.texture_idx[0];
                    idx_data[base + 1] = Lr.texture_idx[1];
                    idx_data[base + 2] = Lr.texture_idx[2];
                    idx_data[base + 3] = Lr.texture_idx[3];
                    bln_data[base + 0] = Lr.blend[0];
                    bln_data[base + 1] = Lr.blend[1];
                    bln_data[base + 2] = Lr.blend[2];
                    bln_data[base + 3] = Lr.blend[3];
                }
                /* Unused layers stay at idx=0xFF (sentinel), blend=0. */
            }
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
        OutputLog::info("TerrainSplat: chunk LUT " + std::to_string(CW)
            + "x" + std::to_string(CH) + " × " + std::to_string(L)
            + " layers");
    }

    /* --- 4) Lightmap SRV. ------------------------------------------ */
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
