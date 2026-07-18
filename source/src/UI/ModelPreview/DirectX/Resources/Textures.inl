static bool create_white_srv(ID3D11Device* dev, ID3D11ShaderResourceView** out_srv){
    *out_srv = nullptr;
    UINT px = 0xFFFFFFFFu;
    D3D11_TEXTURE2D_DESC td{}; td.Width=1; td.Height=1; td.MipLevels=1; td.ArraySize=1; td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_IMMUTABLE; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem=&px; sd.SysMemPitch=4;
    ID3D11Texture2D* tex=nullptr; if(FAILED(dev->CreateTexture2D(&td,&sd,&tex))) return false;
    ID3D11ShaderResourceView* srv=nullptr; if(FAILED(dev->CreateShaderResourceView(tex,nullptr,&srv))){ tex->Release(); return false; }
    tex->Release(); *out_srv=srv; return true;
}
ID3D11ShaderResourceView* create_srv_from_rgba(ID3D11Device* dev, int w, int h, const std::vector<uint8_t>& rgba){
    constexpr int kMaxUploadDim = 8192;
    if (!dev || w <= 0 || h <= 0 || w > kMaxUploadDim || h > kMaxUploadDim) {
        OutputLog::warn("texture upload skipped: invalid RGBA dimensions " +
                        std::to_string(w) + "x" + std::to_string(h));
        return nullptr;
    }

    const uint64_t expected =
        uint64_t(w) * uint64_t(h) * uint64_t(4);
    if (expected == 0 ||
        expected > std::numeric_limits<UINT>::max() ||
        rgba.size() < expected) {
        OutputLog::warn("texture upload skipped: invalid RGBA payload " +
                        std::to_string(w) + "x" + std::to_string(h) +
                        " bytes=" + std::to_string(rgba.size()));
        return nullptr;
    }

    D3D11_TEXTURE2D_DESC td{};
    td.Width = (UINT)w;
    td.Height = (UINT)h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sd{};
    sd.pSysMem = rgba.data();
    sd.SysMemPitch = (UINT)w * 4u;
    sd.SysMemSlicePitch = (UINT)expected;

    ID3D11Texture2D* t = nullptr;
    if (FAILED(dev->CreateTexture2D(&td, &sd, &t))) {
        OutputLog::warn("texture upload failed: CreateTexture2D " +
                        std::to_string(w) + "x" + std::to_string(h));
        return nullptr;
    }
    ID3D11ShaderResourceView* v = nullptr;
    if (FAILED(dev->CreateShaderResourceView(t, nullptr, &v))) {
        OutputLog::warn("texture upload failed: CreateShaderResourceView " +
                        std::to_string(w) + "x" + std::to_string(h));
        t->Release();
        return nullptr;
    }
    t->Release();
    return v;
}

ID3D11ShaderResourceView* create_srv_from_rgba_mipped(
        ID3D11Device* dev, int w, int h, const std::vector<uint8_t>& rgba){
    constexpr int kMaxUploadDim = 8192;
    if (!dev || w <= 0 || h <= 0 || w > kMaxUploadDim || h > kMaxUploadDim) {
        return nullptr;
    }
    const uint64_t expected = uint64_t(w) * uint64_t(h) * 4ull;
    if (expected == 0 || rgba.size() < expected) return nullptr;

    std::vector<std::vector<uint8_t>> mips;
    std::vector<std::pair<int,int>> dims;
    mips.emplace_back(rgba.begin(), rgba.begin() + size_t(expected));
    dims.emplace_back(w, h);
    while (dims.back().first > 1 || dims.back().second > 1) {
        const int sw = dims.back().first;
        const int sh = dims.back().second;
        const int dw = std::max(1, sw / 2);
        const int dh = std::max(1, sh / 2);
        const std::vector<uint8_t>& src = mips.back();
        std::vector<uint8_t> dst(size_t(dw) * dh * 4);
        for (int y = 0; y < dh; ++y) {
            const int y0 = std::min(y * 2, sh - 1);
            const int y1 = std::min(y0 + 1, sh - 1);
            for (int x = 0; x < dw; ++x) {
                const int x0 = std::min(x * 2, sw - 1);
                const int x1 = std::min(x0 + 1, sw - 1);
                const uint8_t* a = &src[(size_t(y0) * sw + x0) * 4];
                const uint8_t* b = &src[(size_t(y0) * sw + x1) * 4];
                const uint8_t* c = &src[(size_t(y1) * sw + x0) * 4];
                const uint8_t* d = &src[(size_t(y1) * sw + x1) * 4];
                uint8_t* o = &dst[(size_t(y) * dw + x) * 4];
                for (int k = 0; k < 4; ++k) {
                    o[k] = uint8_t((int(a[k]) + b[k] + c[k] + d[k] + 2) / 4);
                }
            }
        }
        mips.push_back(std::move(dst));
        dims.emplace_back(dw, dh);
    }

    D3D11_TEXTURE2D_DESC td{};
    td.Width = (UINT)w;
    td.Height = (UINT)h;
    td.MipLevels = (UINT)mips.size();
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    std::vector<D3D11_SUBRESOURCE_DATA> sd(mips.size());
    for (size_t i = 0; i < mips.size(); ++i) {
        sd[i].pSysMem = mips[i].data();
        sd[i].SysMemPitch = (UINT)dims[i].first * 4u;
        sd[i].SysMemSlicePitch = 0;
    }

    ID3D11Texture2D* t = nullptr;
    if (FAILED(dev->CreateTexture2D(&td, sd.data(), &t)) || !t) return nullptr;
    ID3D11ShaderResourceView* v = nullptr;
    if (FAILED(dev->CreateShaderResourceView(t, nullptr, &v))) {
        t->Release();
        return nullptr;
    }
    t->Release();
    return v;
}

static bool srv_from_tex_blob_auto(ID3D11Device* dev, const std::vector<unsigned char>& blob, ID3D11ShaderResourceView** out_srv, bool* out_has_alpha,
                                   int* out_w = nullptr, int* out_h = nullptr){
    *out_srv = nullptr;
    std::vector<uint8_t> rgba;
    int w, h;
    if(!decode_tex_to_rgba(blob, rgba, w, h, out_has_alpha)) return false;
    *out_srv = create_srv_from_rgba(dev, w, h, rgba);
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return (*out_srv != nullptr);
}

static const char* g_fx_vs = R"(
cbuffer FxCB : register(b0){ float4x4 vp; }
struct VSIN { float3 p:POSITION; float2 t:TEXCOORD0; float4 c:COLOR0; };
struct VSOUT{ float4 p:SV_Position; float2 t:TEXCOORD0; float4 c:COLOR0; };
VSOUT VS(VSIN i){ VSOUT o; o.p=mul(float4(i.p,1.0),vp); o.t=i.t; o.c=i.c; return o; }
)";

static const char* g_fx_ps = R"(
Texture2D tex0 : register(t0);
SamplerState smp : register(s0);
struct VSOUT{ float4 p:SV_Position; float2 t:TEXCOORD0; float4 c:COLOR0; };
float4 PS(VSOUT i) : SV_Target {
    float4 s = tex0.Sample(smp, i.t);
    float exposure = 1.0;
    float3 rgb = i.c.a * exposure * i.c.rgb * s.rgb * s.rgb;
    float  a   = s.a * i.c.a;
    return float4(rgb, a);
}
)";
