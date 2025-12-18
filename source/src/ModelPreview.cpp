#include <vector>
#include <string>
#include <algorithm>
#include <filesystem>
#include <optional>
#include <cstdint>
#include <cstring>
#include <cmath>
#include "ModelPreview.h"
#include "Files.h"
#include "Utils.h"
#include "BNKCore.cpp"
#include "TexParser.h"

#ifdef _WIN32
#include <initguid.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
using namespace DirectX;
#else
#include <GL/glew.h>
#endif

static inline std::string tolower_copy(std::string s){ std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }
static inline std::string basename_lower_noext(const std::string& s){
    auto b = std::filesystem::path(s).filename().string();
    auto p = b.find_last_of('.');
    if(p!=std::string::npos) b = b.substr(0,p);
    return tolower_copy(b);
}
static inline std::string force_tex_ext(const std::string& s){
    std::string base = std::filesystem::path(s).filename().string();
    auto p = base.find_last_of('.');
    if(p!=std::string::npos) base = base.substr(0,p);
    return base + ".tex";
}

static std::optional<std::string> find_any_textures_bnk(){
    if(auto p1 = find_bnk_by_filename("globals_textures.bnk"); p1) return p1;
    return find_bnk_by_filename("global_textures.bnk");
}

static inline uint8_t ex5(uint16_t v){ return (uint8_t)((v<<3)|(v>>2)); }
static inline uint8_t ex6(uint16_t v){ return (uint8_t)((v<<2)|(v>>4)); }

static void decode_bc1_block(const uint8_t* b, uint32_t* outRGBA) {
    uint16_t c0 = (uint16_t)(b[0] | (b[1]<<8));
    uint16_t c1 = (uint16_t)(b[2] | (b[3]<<8));
    uint8_t r0=ex5((c0>>11)&31), g0=ex6((c0>>5)&63),  b0=ex5(c0&31);
    uint8_t r1=ex5((c1>>11)&31), g1=ex6((c1>>5)&63),  b1=ex5(c1&31);
    uint32_t cols[4];
    cols[0] = (0xFFu<<24) | (r0<<16) | (g0<<8) | b0;
    cols[1] = (0xFFu<<24) | (r1<<16) | (g1<<8) | b1;
    if(c0 > c1){
        cols[2] = (0xFFu<<24) | (((2*r0+r1)/3)<<16) | (((2*g0+g1)/3)<<8) | ((2*b0+b1)/3);
        cols[3] = (0xFFu<<24) | (((r0+2*r1)/3)<<16) | (((g0+2*g1)/3)<<8) | ((b0+2*b1)/3);
    }else{
        cols[2] = (0xFFu<<24) | (((r0+r1)>>1)<<16) | (((g0+g1)>>1)<<8) | ((b0+b1)>>1);
        cols[3] = 0x00000000u;
    }
    const uint32_t idx = b[4] | (b[5]<<8) | (b[6]<<16) | (b[7]<<24);
    for(int py=0; py<4; ++py){
        for(int px=0; px<4; ++px){
            int s = (idx >> (2*(py*4+px))) & 3;
            outRGBA[py*4+px] = cols[s];
        }
    }
}

static void decode_bc3_block(const uint8_t* b, uint32_t* outRGBA){
    uint8_t a0=b[0], a1=b[1];
    uint64_t abits = 0;
    for(int i=0;i<6;++i) abits |= (uint64_t)b[2+i] << (8*i);
    uint8_t atab[8];
    atab[0]=a0; atab[1]=a1;
    if(a0>a1){ for(int i=1;i<=6;i++) atab[i+1]=(uint8_t)(((7-i)*a0 + i*a1)/7); }
    else{ for(int i=1;i<=4;i++) atab[i+1]=(uint8_t)(((5-i)*a0 + i*a1)/5); atab[6]=0; atab[7]=255; }
    uint32_t color[16];
    decode_bc1_block(b+8, color);
    for(int i=0;i<16;++i){
        uint8_t ai = (uint8_t)((abits>>(3*i)) & 7);
        color[i] = (color[i] & 0x00FFFFFFu) | ( ((uint32_t)atab[ai])<<24 );
    }
    for(int i=0;i<16;++i) outRGBA[i]=color[i];
}

static void swap_bc1_endian(uint8_t* data, size_t size) {
    for(size_t i = 0; i + 8 <= size; i += 8) {
        uint16_t c0 = (data[i+0] << 8) | data[i+1];
        uint16_t c1 = (data[i+2] << 8) | data[i+3];
        uint32_t idx = (data[i+4] << 24) | (data[i+5] << 16) | (data[i+6] << 8) | data[i+7];
        data[i+0] = c0 & 0xFF;
        data[i+1] = (c0 >> 8) & 0xFF;
        data[i+2] = c1 & 0xFF;
        data[i+3] = (c1 >> 8) & 0xFF;
        data[i+4] = idx & 0xFF;
        data[i+5] = (idx >> 8) & 0xFF;
        data[i+6] = (idx >> 16) & 0xFF;
        data[i+7] = (idx >> 24) & 0xFF;
    }
}

static void swap_bc3_endian(uint8_t* data, size_t size) {
    for(size_t i = 0; i + 16 <= size; i += 16) {
        uint64_t alpha_bits = 0;
        for(int j = 0; j < 6; j++) {
            alpha_bits |= ((uint64_t)data[i+2+j]) << (j*8);
        }
        uint64_t alpha_swapped = 0;
        for(int j = 0; j < 6; j++) {
            alpha_swapped |= ((alpha_bits >> (j*8)) & 0xFF) << ((5-j)*8);
        }
        for(int j = 0; j < 6; j++) {
            data[i+2+j] = (alpha_swapped >> (j*8)) & 0xFF;
        }
        swap_bc1_endian(data + i + 8, 8);
    }
}

static bool decode_tex_to_rgba(const std::vector<unsigned char>& blob, std::vector<uint8_t>& rgba, int& out_w, int& out_h, bool* out_has_alpha){
    if(out_has_alpha) *out_has_alpha = false;
    TexInfo ti{};
    if(!parse_tex_info(blob, ti) || ti.Mips.empty()) return false;
    size_t best = 0;
    for(size_t i=1;i<ti.Mips.size();++i){
        if(ti.Mips[i].CompFlag != 7) continue;
        int w = ti.Mips[i].HasWH ? (int)ti.Mips[i].MipWidth  : std::max(1, (int)ti.TextureWidth  >> (int)i);
        int h = ti.Mips[i].HasWH ? (int)ti.Mips[i].MipHeight : std::max(1, (int)ti.TextureHeight >> (int)i);
        int bw = ti.Mips[best].HasWH ? (int)ti.Mips[best].MipWidth  : std::max(1, (int)ti.TextureWidth  >> (int)best);
        int bh = ti.Mips[best].HasWH ? (int)ti.Mips[best].MipHeight : std::max(1, (int)ti.TextureHeight >> (int)best);
        if(ti.Mips[best].CompFlag != 7 || w*h > bw*bh) best = i;
    }
    const auto& m = ti.Mips[best];
    int w = m.HasWH ? (int)m.MipWidth  : std::max(1, (int)ti.TextureWidth  >> (int)best);
    int h = m.HasWH ? (int)m.MipHeight : std::max(1, (int)ti.TextureHeight >> (int)best);
    if(m.MipDataOffset + m.MipDataSizeParsed > blob.size()) return false;
    size_t bx = (size_t)((w+3)/4), by = (size_t)((h+3)/4);
    size_t sz_bc1 = bx*by*8;
    size_t sz_bc3 = bx*by*16;
    size_t sz_raw = (size_t)w*(size_t)h*4;
    rgba.resize((size_t)w*(size_t)h*4, 0xFF);
    const uint8_t* src = blob.data() + m.MipDataOffset;
    auto any_alpha_lt_255 = [&](const std::vector<uint8_t>& buf)->bool{
        const uint8_t* p = buf.data();
        size_t n = buf.size();
        for(size_t i=3;i<n;i+=4){ if(p[i] < 255){ return true; } }
        return false;
    };
    if(ti.PixelFormat == 35){
        if(m.MipDataSizeParsed < sz_bc1) return false;
        std::vector<uint8_t> swapped(src, src + sz_bc1);
        swap_bc1_endian(swapped.data(), swapped.size());
        size_t off=0;
        for(size_t byy=0; byy<by; ++byy){
            for(size_t bxx=0; bxx<bx; ++bxx){
                uint32_t block[16];
                decode_bc1_block(swapped.data()+off, block); off += 8;
                for(int py=0; py<4; ++py){
                    int yy = (int)byy*4 + py; if(yy>=h) break;
                    for(int px=0; px<4; ++px){
                        int xx=(int)bxx*4 + px; if(xx>=w) break;
                        ((uint32_t*)rgba.data())[yy*w+xx] = block[py*4+px];
                    }
                }
            }
        }
        out_w = w; out_h = h;
        if(out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
        return true;
    }
    if(ti.PixelFormat == 39){
        if(m.MipDataSizeParsed < sz_bc3) return false;
        std::vector<uint8_t> swapped(src, src + sz_bc3);
        swap_bc3_endian(swapped.data(), swapped.size());
        size_t off=0;
        for(size_t byy=0; byy<by; ++byy){
            for(size_t bxx=0; bxx<bx; ++bxx){
                uint32_t block[16];
                decode_bc3_block(swapped.data()+off, block); off += 16;
                for(int py=0; py<4; ++py){
                    int yy = (int)byy*4 + py; if(yy>=h) break;
                    for(int px=0; px<4; ++px){
                        int xx=(int)bxx*4 + px; if(xx>=w) break;
                        ((uint32_t*)rgba.data())[yy*w+xx] = block[py*4+px];
                    }
                }
            }
        }
        out_w = w; out_h = h;
        if(out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
        return true;
    }
    if(ti.PixelFormat == 40) { return false; }
    if(m.MipDataSizeParsed < sz_raw) return false;
    memcpy(rgba.data(), src, sz_raw);
    out_w = w; out_h = h;
    if(out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
    return true;
}

static bool extract_tex_bytes_by_candidate(const std::vector<std::string>& candidates, std::vector<unsigned char>& out){
    auto pOpt = find_any_textures_bnk();
    if(!pOpt) return false;
    BNKReader r(*pOpt);
    std::vector<std::string> wanted;
    for(const auto& c : candidates){
        if(c.empty()) continue;
        wanted.push_back(tolower_copy(c));
        std::string fname = std::filesystem::path(c).filename().string();
        wanted.push_back(tolower_copy(fname));
        wanted.push_back(tolower_copy(force_tex_ext(c)));
        wanted.push_back(tolower_copy(force_tex_ext(fname)));
        wanted.push_back(basename_lower_noext(c));
    }
    std::sort(wanted.begin(), wanted.end());
    wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());
    int best_idx = -1;
    size_t best_area = 0;
    for(size_t i=0;i<r.list_files().size();++i){
        const auto& e = r.list_files()[i];
        std::string fn = std::filesystem::path(e.name).filename().string();
        std::string fn_low = tolower_copy(fn);
        std::string fn_base_noext = basename_lower_noext(fn);
        bool match = false;
        for(const auto& w : wanted){
            if(fn_low == w || fn_base_noext == w){ match = true; break; }
        }
        if(!match) continue;
        std::vector<unsigned char> blob;
        try{
            auto dir = std::filesystem::temp_directory_path()/ "f2_tex_pick";
            std::error_code ec; std::filesystem::create_directories(dir, ec);
            auto outp = dir/("tex_"+std::to_string((uint64_t)i)+".bin");
            extract_one(*pOpt, (int)i, outp.string());
            blob = read_all_bytes(outp);
            std::filesystem::remove(outp, ec);
        }catch(...){ continue; }
        if(blob.empty()) continue;
        TexInfo ti{};
        if(!parse_tex_info(blob, ti)) continue;
        bool has_uncompressed = false;
        for(const auto& mip : ti.Mips){ if(mip.CompFlag == 7){ has_uncompressed = true; break; } }
        if(!has_uncompressed) continue;
        size_t area = (size_t)ti.TextureWidth * (size_t)ti.TextureHeight;
        if(area > best_area){ best_area = area; best_idx = (int)i; out.swap(blob); }
    }
    return best_idx >= 0 && !out.empty();
}

#ifdef _WIN32

static void mp_release_mesh(MPPerMesh& m){
    if(m.vb){ m.vb->Release(); m.vb=nullptr; }
    if(m.ib){ m.ib->Release(); m.ib=nullptr; }
    if(m.srv_diffuse){ m.srv_diffuse->Release(); m.srv_diffuse=nullptr; }
    if(m.srv_normal){ m.srv_normal->Release(); m.srv_normal=nullptr; }
    if(m.srv_specular){ m.srv_specular->Release(); m.srv_specular=nullptr; }
    if(m.srv_unk){ m.srv_unk->Release(); m.srv_unk=nullptr; }
    if(m.srv_tint){ m.srv_tint->Release(); m.srv_tint=nullptr; }
    m.index_count = 0;
}

static void mp_release(ModelPreview& mp){
    for(auto& m: mp.meshes) mp_release_mesh(m);
    mp.meshes.clear();
    if(mp.vs){ mp.vs->Release(); mp.vs=nullptr; }
    if(mp.ps){ mp.ps->Release(); mp.ps=nullptr; }
    if(mp.layout){ mp.layout->Release(); mp.layout=nullptr; }
    if(mp.cbuffer){ mp.cbuffer->Release(); mp.cbuffer=nullptr; }
    if(mp.sampler){ mp.sampler->Release(); mp.sampler=nullptr; }
    if(mp.rs){ mp.rs->Release(); mp.rs=nullptr; }
    if(mp.bs){ mp.bs->Release(); mp.bs=nullptr; }
    if(mp.bsAlpha){ mp.bsAlpha->Release(); mp.bsAlpha=nullptr; }
    if(mp.rtv){ mp.rtv->Release(); mp.rtv=nullptr; }
    if(mp.srv){ mp.srv->Release(); mp.srv=nullptr; }
    if(mp.color){ mp.color->Release(); mp.color=nullptr; }
    if(mp.dsv){ mp.dsv->Release(); mp.dsv=nullptr; }
    if(mp.depth){ mp.depth->Release(); mp.depth=nullptr; }
    if(mp.default_srv){ mp.default_srv->Release(); mp.default_srv=nullptr; }
    if(mp.dssWrite){ mp.dssWrite->Release(); mp.dssWrite=nullptr; }
    if(mp.dssNoWrite){ mp.dssNoWrite->Release(); mp.dssNoWrite=nullptr; }
}

static bool compile_shader(const char* src, const char* entry, const char* profile, ID3DBlob** blob){
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, entry, profile, flags, 0, blob, &err);
    if(err){ err->Release(); }
    return SUCCEEDED(hr);
}

static const char* g_vs = R"(
cbuffer CB : register(b0){
    float4x4 mvp;
    float4   lightDir;
    float4x4 mv;
    float4   params;
}
struct VSIN{ float3 p:POSITION; float3 n:NORMAL; float2 t:TEXCOORD0; };
struct VSOUT{ float4 p:SV_Position; float3 n:NORMAL; float2 t:TEXCOORD0; };
VSOUT VS(VSIN i){
    VSOUT o;
    o.p = mul(float4(i.p,1), mvp);
    float3 n = mul(i.n, (float3x3)mv);
    o.n = normalize(n);
    o.t = float2(i.t.x, 1.0 - i.t.y);
    return o;
}
)";

static const char* g_ps = R"(
cbuffer CB : register(b0){
    float4x4 mvp;
    float4   lightDir;
    float4x4 mv;
    float4   params;
}
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);
Texture2D tex3 : register(t3);
Texture2D tex4 : register(t4);
SamplerState smp : register(s0);
struct VSOUT{ float4 p:SV_Position; float3 n:NORMAL; float2 t:TEXCOORD0; };
float3 hemiAmbient(float3 n) {
    float up = saturate(n.y*0.5 + 0.5);
    float3 sky    = float3(0.75, 0.77, 0.80);
    float3 ground = float3(0.50, 0.48, 0.46);
    return lerp(ground, sky, up);
}
float4 PS(VSOUT i) : SV_Target {
    float3 N_geo = normalize(i.n);
    float3 N_m = tex1.Sample(smp, i.t).rgb * 2.0 - 1.0;
    N_m = normalize(N_m);
    float3 N = normalize(N_geo + N_m * 0.5);
    float3 albedo   = tex0.Sample(smp, i.t).rgb;
    float  alpha    = tex0.Sample(smp, i.t).a;
    float3 specTex  = tex2.Sample(smp, i.t).rgb;
    float3 tint     = tex4.Sample(smp, i.t).rgb;
    albedo *= tint;
    float3 gi = hemiAmbient(N);
    float3 color = albedo * gi;
    color = pow(color, 1.0/2.2);
    return float4(color, alpha);
}
)";

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

static ID3D11ShaderResourceView* create_srv_from_rgba(ID3D11Device* dev, int w, int h, const std::vector<uint8_t>& rgba){
    D3D11_TEXTURE2D_DESC td{}; td.Width=w; td.Height=h; td.MipLevels=0; td.ArraySize=1; td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET; td.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
    ID3D11Texture2D* t=nullptr; if(FAILED(dev->CreateTexture2D(&td,nullptr,&t))) return nullptr;
    ID3D11DeviceContext* ctx=nullptr; dev->GetImmediateContext(&ctx);
    ctx->UpdateSubresource(t, 0, nullptr, rgba.data(), w*4, 0);
    ID3D11ShaderResourceView* v=nullptr; if(FAILED(dev->CreateShaderResourceView(t,nullptr,&v))){ t->Release(); ctx->Release(); return nullptr; }
    ctx->GenerateMips(v);
    ctx->Release();
    t->Release(); return v;
}

static bool srv_from_tex_blob_auto(ID3D11Device* dev, const std::vector<unsigned char>& blob, ID3D11ShaderResourceView** out_srv, bool* out_has_alpha){
    *out_srv = nullptr;
    std::vector<uint8_t> rgba;
    int w, h;
    if(!decode_tex_to_rgba(blob, rgba, w, h, out_has_alpha)) return false;
    *out_srv = create_srv_from_rgba(dev, w, h, rgba);
    return (*out_srv != nullptr);
}

static bool create_target(ID3D11Device* dev, ModelPreview& mp, int w, int h){
    mp_release(mp);
    mp.width = w; mp.height = h;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w; td.Height = h; td.MipLevels=1; td.ArraySize=1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count=1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if(FAILED(dev->CreateTexture2D(&td, nullptr, &mp.color))) return false;
    if(FAILED(dev->CreateRenderTargetView(mp.color, nullptr, &mp.rtv))) return false;
    if(FAILED(dev->CreateShaderResourceView(mp.color, nullptr, &mp.srv))) return false;
    D3D11_TEXTURE2D_DESC dd{};
    dd.Width=w; dd.Height=h; dd.MipLevels=1; dd.ArraySize=1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc.Count=1;
    dd.Usage = D3D11_USAGE_DEFAULT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if(FAILED(dev->CreateTexture2D(&dd,nullptr,&mp.depth))) return false;
    if(FAILED(dev->CreateDepthStencilView(mp.depth,nullptr,&mp.dsv))) return false;
    ID3DBlob* vsb=nullptr; ID3DBlob* psb=nullptr;
    if(!compile_shader(g_vs,"VS","vs_5_0",&vsb)) return false;
    if(!compile_shader(g_ps,"PS","ps_5_0",&psb)){ if(vsb) vsb->Release(); return false; }
    if(FAILED(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &mp.vs))){ vsb->Release(); psb->Release(); return false; }
    if(FAILED(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &mp.ps))){ vsb->Release(); psb->Release(); return false; }
    D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,  D3D11_INPUT_PER_VERTEX_DATA,0},
        {"NORMAL",  0,DXGI_FORMAT_R32G32B32_FLOAT,0,12, D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,   0,24, D3D11_INPUT_PER_VERTEX_DATA,0},
    };
    if(FAILED(dev->CreateInputLayout(il,3,vsb->GetBufferPointer(),vsb->GetBufferSize(),&mp.layout))){ vsb->Release(); psb->Release(); return false; }
    vsb->Release(); psb->Release();
    struct CB { XMFLOAT4X4 mvp; XMFLOAT4 lightDir; XMFLOAT4X4 mv; XMFLOAT4 params; };
    D3D11_BUFFER_DESC cbd{};
    cbd.BindFlags=D3D11_BIND_CONSTANT_BUFFER; cbd.ByteWidth=sizeof(CB); cbd.Usage=D3D11_USAGE_DYNAMIC; cbd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    if(FAILED(dev->CreateBuffer(&cbd,nullptr,&mp.cbuffer))) return false;
    D3D11_SAMPLER_DESC ssd{}; ssd.Filter=D3D11_FILTER_ANISOTROPIC; ssd.MaxAnisotropy=16; ssd.AddressU=ssd.AddressV=ssd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP; ssd.MaxLOD=D3D11_FLOAT32_MAX;
    if(FAILED(dev->CreateSamplerState(&ssd,&mp.sampler))) return false;
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    rd.MultisampleEnable = FALSE;
    if(FAILED(dev->CreateRasterizerState(&rd,&mp.rs))) return false;
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable=FALSE;
    bd.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    if(FAILED(dev->CreateBlendState(&bd,&mp.bs))) return false;
    D3D11_BLEND_DESC bda{};
    bda.RenderTarget[0].BlendEnable           = TRUE;
    bda.RenderTarget[0].SrcBlend              = D3D11_BLEND_SRC_ALPHA;
    bda.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
    bda.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
    bda.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
    bda.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_INV_SRC_ALPHA;
    bda.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
    bda.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if(FAILED(dev->CreateBlendState(&bda,&mp.bsAlpha))) return false;
    D3D11_DEPTH_STENCIL_DESC dssw{};
    dssw.DepthEnable = TRUE;
    dssw.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dssw.DepthFunc = D3D11_COMPARISON_LESS;
    if(FAILED(dev->CreateDepthStencilState(&dssw, &mp.dssWrite))) return false;
    D3D11_DEPTH_STENCIL_DESC dssn{};
    dssn.DepthEnable = TRUE;
    dssn.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dssn.DepthFunc = D3D11_COMPARISON_LESS;
    if(FAILED(dev->CreateDepthStencilState(&dssn, &mp.dssNoWrite))) return false;
    if(!create_white_srv(dev, &mp.default_srv)) return false;
    return true;
}

bool MP_Init(ID3D11Device* dev, ModelPreview& mp, int w, int h){
    return create_target(dev, mp, w, h);
}

void MP_Release(ModelPreview& mp){
    mp_release(mp);
}

bool MP_Build(ID3D11Device* dev, const std::vector<MDLMeshGeom>& geoms, const MDLInfo& info, ModelPreview& mp){
    for(auto& m : mp.meshes){
        if(m.vb){m.vb->Release();}
        if(m.ib){m.ib->Release();}
        if(m.srv_diffuse){m.srv_diffuse->Release();}
        if(m.srv_normal){m.srv_normal->Release();}
        if(m.srv_specular){m.srv_specular->Release();}
        if(m.srv_unk){m.srv_unk->Release();}
        if(m.srv_tint){m.srv_tint->Release();}
    }
    mp.meshes.clear();
    float minx=1e9f,miny=1e9f,minz=1e9f,maxx=-1e9f,maxy=-1e9f,maxz=-1e9f;
    for(const auto& g: geoms){
        for(size_t i=0;i+2<g.positions.size();i+=3){
            float x=g.positions[i],y=g.positions[i+1],z=g.positions[i+2];
            if(x<minx)minx=x; if(y<miny)miny=y; if(z<minz)minz=z;
            if(x>maxx)maxx=x; if(y>maxy)maxy=y; if(z>maxz)maxz=z;
        }
    }
    if(!(minx<maxx)){ minx=-1;maxx=1;miny=-1;maxy=1;minz=-1;maxz=1; }
    mp.center[0]=(minx+maxx)*0.5f; mp.center[1]=(miny+maxy)*0.5f; mp.center[2]=(minz+maxz)*0.5f;
    mp.radius = std::max(std::max(maxx-minx,maxy-miny),maxz-minz)*0.5f; if(mp.radius<0.0001f) mp.radius=1.0f;
    for(size_t i=0;i<geoms.size();++i){
        const auto& g = geoms[i];
        size_t vcount = g.positions.size()/3;
        if(vcount==0 || g.indices.empty()) continue;
        std::vector<MPVertex> vtx(vcount);
        bool hasN = (g.normals.size()==vcount*3);
        bool hasT = (g.uvs.size()==vcount*2);
        for(size_t v=0; v<vcount; ++v){
            vtx[v].px = g.positions[v*3+0];
            vtx[v].py = g.positions[v*3+1];
            vtx[v].pz = g.positions[v*3+2];
            vtx[v].nx = hasN ? g.normals[v*3+0] : 0.0f;
            vtx[v].ny = hasN ? g.normals[v*3+1] : 1.0f;
            vtx[v].nz = hasN ? g.normals[v*3+2] : 0.0f;
            vtx[v].u  = hasT ? g.uvs[v*2+0] : 0.0f;
            vtx[v].v  = hasT ? g.uvs[v*2+1] : 0.0f;
        }
        MPPerMesh m;
        D3D11_BUFFER_DESC vb{}; vb.BindFlags=D3D11_BIND_VERTEX_BUFFER; vb.ByteWidth=(UINT)(vtx.size()*sizeof(MPVertex)); vb.Usage=D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA vsd{}; vsd.pSysMem=vtx.data();
        if(FAILED(dev->CreateBuffer(&vb,&vsd,&m.vb))) continue;
        D3D11_BUFFER_DESC ib{}; ib.BindFlags=D3D11_BIND_INDEX_BUFFER; ib.ByteWidth=(UINT)(g.indices.size()*sizeof(uint32_t)); ib.Usage=D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA isd{}; isd.pSysMem=g.indices.data();
        if(FAILED(dev->CreateBuffer(&ib,&isd,&m.ib))) { m.vb->Release(); continue; }
        m.index_count = (UINT)g.indices.size();
        bool hasA = false;
        if(!g.diffuse_tex_name.empty()){
            std::vector<unsigned char> tex_buf;
            if(build_any_tex_buffer_for_name(g.diffuse_tex_name, tex_buf)){
                if(srv_from_tex_blob_auto(dev, tex_buf, &m.srv_diffuse, &hasA)){}
            }
        }
        if (!m.srv_diffuse && mp.default_srv) { m.srv_diffuse = mp.default_srv; m.srv_diffuse->AddRef(); }
        if (!m.srv_normal  && mp.default_srv) { m.srv_normal  = mp.default_srv; m.srv_normal->AddRef(); }
        if (!m.srv_specular&& mp.default_srv) { m.srv_specular= mp.default_srv; m.srv_specular->AddRef(); }
        if (!m.srv_unk     && mp.default_srv) { m.srv_unk     = mp.default_srv; m.srv_unk->AddRef(); }
        if (!m.srv_tint    && mp.default_srv) { m.srv_tint    = mp.default_srv; m.srv_tint->AddRef(); }
        m.has_alpha = hasA;
        mp.meshes.push_back(m);
    }
    return true;
}

void MP_Render(ID3D11Device* dev, ModelPreview& mp, float yaw, float pitch, float dist){
    ID3D11DeviceContext* ctx=nullptr; dev->GetImmediateContext(&ctx); if(!ctx) return;
    D3D11_VIEWPORT vp{}; vp.TopLeftX=0; vp.TopLeftY=0; vp.Width=(FLOAT)mp.width; vp.Height=(FLOAT)mp.height; vp.MinDepth=0; vp.MaxDepth=1;
    ctx->RSSetViewports(1,&vp);
    float clear[4] = {0.18f,0.22f,0.28f,1.0f};
    ctx->OMSetRenderTargets(1,&mp.rtv, mp.dsv);
    ctx->ClearRenderTargetView(mp.rtv, clear);
    ctx->ClearDepthStencilView(mp.dsv, D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL, 1.0f, 0);
    ctx->IASetInputLayout(mp.layout);
    ctx->VSSetShader(mp.vs,nullptr,0);
    ctx->PSSetShader(mp.ps,nullptr,0);
    ctx->PSSetSamplers(0,1,&mp.sampler);
    ctx->RSSetState(mp.rs);
    float r = std::max(0.5f, dist) * mp.radius * 2.2f;
    float cy=cosf(yaw), sy=sinf(yaw);
    float cp=cosf(pitch), sp=sinf(pitch);
    XMVECTOR C   = XMVectorSet(mp.center[0], mp.center[1], mp.center[2], 1);
    XMVECTOR eye = XMVectorSet(mp.center[0] + r*cp*sy, mp.center[1] + r*sp, mp.center[2] + r*cp*cy, 1);
    XMVECTOR at  = C;
    XMVECTOR up  = XMVectorSet(0,1,0,0);
    XMMATRIX V = XMMatrixLookAtLH(eye, at, up);
    float fov = XMConvertToRadians(60.0f);
    float aspect = (float)mp.width / (float)mp.height;
    XMMATRIX P = XMMatrixPerspectiveFovLH(fov, aspect, 0.05f, r*8.0f);
    const float tiltX = -XM_PIDIV2;
    XMMATRIX Tm = XMMatrixTranslation(-mp.center[0], -mp.center[1], -mp.center[2]);
    XMMATRIX R  = XMMatrixRotationX(tiltX);
    XMMATRIX Tp = XMMatrixTranslation( mp.center[0],  mp.center[1],  mp.center[2]);
    XMMATRIX W  = Tm * R * Tp;
    XMMATRIX MVP = XMMatrixTranspose(W * V * P);
    XMMATRIX MV  = XMMatrixTranspose(W * V);
    XMVECTOR lightDirV = XMVector3Normalize(XMVectorSubtract(eye, at));
    lightDirV = XMVectorAdd(lightDirV, XMVectorSet(0.2f, 0.3f, 0.0f, 0.0f));
    lightDirV = XMVector3Normalize(lightDirV);
    XMFLOAT4 lightDirF; XMStoreFloat4(&lightDirF, lightDirV);
    struct CB { XMFLOAT4X4 mvp; XMFLOAT4 lightDir; XMFLOAT4X4 mv; XMFLOAT4 params; } cb;
    XMStoreFloat4x4(&cb.mvp, MVP);
    XMStoreFloat4x4(&cb.mv,  MV);
    cb.lightDir = lightDirF;
    cb.params   = XMFLOAT4(0.4f, 48.0f, 0.0f, 0.0f);
    D3D11_MAPPED_SUBRESOURCE ms{};
    if(SUCCEEDED(ctx->Map(mp.cbuffer,0,D3D11_MAP_WRITE_DISCARD,0,&ms))){
        memcpy(ms.pData, &cb, sizeof(cb));
        ctx->Unmap(mp.cbuffer,0);
    }
    ctx->VSSetConstantBuffers(0,1,&mp.cbuffer);
    ctx->PSSetConstantBuffers(0,1,&mp.cbuffer);
    float blend_factor[4] = {0,0,0,0};
    ctx->OMSetDepthStencilState(mp.dssWrite, 0);
    for(const auto& m : mp.meshes){
        if(!m.vb || !m.ib || m.index_count==0 || m.has_alpha) continue;
        ctx->OMSetBlendState(mp.bs, blend_factor, 0xFFFFFFFF);
        UINT stride=sizeof(MPVertex), offset=0;
        ctx->IASetVertexBuffers(0,1,&m.vb,&stride,&offset);
        ctx->IASetIndexBuffer(m.ib, DXGI_FORMAT_R32_UINT, 0);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11ShaderResourceView* srvs[5] = { m.srv_diffuse ? m.srv_diffuse : mp.default_srv, m.srv_normal ? m.srv_normal : mp.default_srv, m.srv_specular ? m.srv_specular : mp.default_srv, m.srv_unk ? m.srv_unk : mp.default_srv, m.srv_tint ? m.srv_tint : mp.default_srv };
        ctx->PSSetShaderResources(0, 5, srvs);
        ctx->DrawIndexed(m.index_count,0,0);
        ID3D11ShaderResourceView* nullsrvs[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
        ctx->PSSetShaderResources(0, 5, nullsrvs);
    }
    ctx->OMSetDepthStencilState(mp.dssNoWrite, 0);
    for(const auto& m : mp.meshes){
        if(!m.vb || !m.ib || m.index_count==0 || !m.has_alpha) continue;
        ctx->OMSetBlendState(mp.bsAlpha, blend_factor, 0xFFFFFFFF);
        UINT stride=sizeof(MPVertex), offset=0;
        ctx->IASetVertexBuffers(0,1,&m.vb,&stride,&offset);
        ctx->IASetIndexBuffer(m.ib, DXGI_FORMAT_R32_UINT, 0);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11ShaderResourceView* srvs[5] = { m.srv_diffuse ? m.srv_diffuse : mp.default_srv, m.srv_normal ? m.srv_normal : mp.default_srv, m.srv_specular ? m.srv_specular : mp.default_srv, m.srv_unk ? m.srv_unk : mp.default_srv, m.srv_tint ? m.srv_tint : mp.default_srv };
        ctx->PSSetShaderResources(0, 5, srvs);
        ctx->DrawIndexed(m.index_count,0,0);
        ID3D11ShaderResourceView* nullsrvs[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
        ctx->PSSetShaderResources(0, 5, nullsrvs);
    }
    ctx->Release();
}

#else

static const char* gl_vs = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
uniform mat4 uMVP;
uniform mat4 uMV;
out vec3 vNormal;
out vec2 vTexCoord;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vNormal = normalize(mat3(uMV) * aNormal);
    vTexCoord = vec2(aTexCoord.x, 1.0 - aTexCoord.y);
}
)";

static const char* gl_fs = R"(
#version 330 core
in vec3 vNormal;
in vec2 vTexCoord;
uniform sampler2D uTexDiffuse;
uniform sampler2D uTexNormal;
uniform sampler2D uTexSpecular;
uniform sampler2D uTexUnk;
uniform sampler2D uTexTint;
out vec4 FragColor;
vec3 hemiAmbient(vec3 n) {
    float up = clamp(n.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 sky = vec3(0.75, 0.77, 0.80);
    vec3 ground = vec3(0.50, 0.48, 0.46);
    return mix(ground, sky, up);
}
void main() {
    vec3 N_geo = normalize(vNormal);
    vec3 N_m = texture(uTexNormal, vTexCoord).rgb * 2.0 - 1.0;
    N_m = normalize(N_m);
    vec3 N = normalize(N_geo + N_m * 0.5);
    vec4 diffuseSample = texture(uTexDiffuse, vTexCoord);
    vec3 albedo = diffuseSample.rgb;
    float alpha = diffuseSample.a;
    vec3 tint = texture(uTexTint, vTexCoord).rgb;
    albedo *= tint;
    vec3 gi = hemiAmbient(N);
    vec3 color = albedo * gi;
    color = pow(color, vec3(1.0/2.2));
    FragColor = vec4(color, alpha);
}
)";

static unsigned int compile_gl_shader(const char* src, GLenum type) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) { glDeleteShader(shader); return 0; }
    return shader;
}

static unsigned int create_gl_program(const char* vs_src, const char* fs_src) {
    unsigned int vs = compile_gl_shader(vs_src, GL_VERTEX_SHADER);
    if (!vs) return 0;
    unsigned int fs = compile_gl_shader(fs_src, GL_FRAGMENT_SHADER);
    if (!fs) { glDeleteShader(vs); return 0; }
    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    int success;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) { glDeleteProgram(prog); return 0; }
    return prog;
}

static unsigned int create_gl_texture_from_rgba(int w, int h, const uint8_t* data) {
    unsigned int tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    float maxAniso = 0.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
    if (maxAniso > 0.0f) { glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(maxAniso, 16.0f)); }
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

static unsigned int create_white_tex() {
    uint32_t px = 0xFFFFFFFF;
    return create_gl_texture_from_rgba(1, 1, (const uint8_t*)&px);
}

static void mp_release_mesh_gl(MPPerMesh& m) {
    if (m.vao) { glDeleteVertexArrays(1, &m.vao); m.vao = 0; }
    if (m.vbo) { glDeleteBuffers(1, &m.vbo); m.vbo = 0; }
    if (m.ibo) { glDeleteBuffers(1, &m.ibo); m.ibo = 0; }
    if (m.tex_diffuse) { glDeleteTextures(1, &m.tex_diffuse); m.tex_diffuse = 0; }
    if (m.tex_normal) { glDeleteTextures(1, &m.tex_normal); m.tex_normal = 0; }
    if (m.tex_specular) { glDeleteTextures(1, &m.tex_specular); m.tex_specular = 0; }
    if (m.tex_unk) { glDeleteTextures(1, &m.tex_unk); m.tex_unk = 0; }
    if (m.tex_tint) { glDeleteTextures(1, &m.tex_tint); m.tex_tint = 0; }
    m.index_count = 0;
}

static void mp_release_gl(ModelPreview& mp) {
    for (auto& m : mp.meshes) mp_release_mesh_gl(m);
    mp.meshes.clear();
    if (mp.shader_program) { glDeleteProgram(mp.shader_program); mp.shader_program = 0; }
    if (mp.fbo) { glDeleteFramebuffers(1, &mp.fbo); mp.fbo = 0; }
    if (mp.color_tex) { glDeleteTextures(1, &mp.color_tex); mp.color_tex = 0; }
    if (mp.depth_rbo) { glDeleteRenderbuffers(1, &mp.depth_rbo); mp.depth_rbo = 0; }
    if (mp.default_tex) { glDeleteTextures(1, &mp.default_tex); mp.default_tex = 0; }
}

bool MP_Init(ModelPreview& mp, int w, int h) {
    mp_release_gl(mp);
    mp.width = w;
    mp.height = h;
    mp.shader_program = create_gl_program(gl_vs, gl_fs);
    if (!mp.shader_program) return false;
    mp.mvp_loc = glGetUniformLocation(mp.shader_program, "uMVP");
    mp.mv_loc = glGetUniformLocation(mp.shader_program, "uMV");
    mp.tex_diffuse_loc = glGetUniformLocation(mp.shader_program, "uTexDiffuse");
    mp.tex_normal_loc = glGetUniformLocation(mp.shader_program, "uTexNormal");
    mp.tex_specular_loc = glGetUniformLocation(mp.shader_program, "uTexSpecular");
    mp.tex_unk_loc = glGetUniformLocation(mp.shader_program, "uTexUnk");
    mp.tex_tint_loc = glGetUniformLocation(mp.shader_program, "uTexTint");
    glGenFramebuffers(1, &mp.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, mp.fbo);
    glGenTextures(1, &mp.color_tex);
    glBindTexture(GL_TEXTURE_2D, mp.color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mp.color_tex, 0);
    glGenRenderbuffers(1, &mp.depth_rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, mp.depth_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, mp.depth_rbo);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    mp.default_tex = create_white_tex();
    return true;
}

void MP_Release(ModelPreview& mp) {
    mp_release_gl(mp);
}

static unsigned int load_tex_from_name(const std::string& name, bool* out_has_alpha) {
    if (name.empty()) return 0;
    std::vector<unsigned char> tex_buf;
    if (build_any_tex_buffer_for_name(name, tex_buf)) {
        std::vector<uint8_t> rgba;
        int w, h;
        if (decode_tex_to_rgba(tex_buf, rgba, w, h, out_has_alpha)) {
            return create_gl_texture_from_rgba(w, h, rgba.data());
        }
    }
    return 0;
}

bool MP_Build(const std::vector<MDLMeshGeom>& geoms, const MDLInfo& info, ModelPreview& mp) {
    for (auto& m : mp.meshes) mp_release_mesh_gl(m);
    mp.meshes.clear();
    float minx = 1e9f, miny = 1e9f, minz = 1e9f, maxx = -1e9f, maxy = -1e9f, maxz = -1e9f;
    for (const auto& g : geoms) {
        for (size_t i = 0; i + 2 < g.positions.size(); i += 3) {
            float x = g.positions[i], y = g.positions[i + 1], z = g.positions[i + 2];
            if (x < minx) minx = x; if (y < miny) miny = y; if (z < minz) minz = z;
            if (x > maxx) maxx = x; if (y > maxy) maxy = y; if (z > maxz) maxz = z;
        }
    }
    if (!(minx < maxx)) { minx = -1; maxx = 1; miny = -1; maxy = 1; minz = -1; maxz = 1; }
    mp.center[0] = (minx + maxx) * 0.5f; mp.center[1] = (miny + maxy) * 0.5f; mp.center[2] = (minz + maxz) * 0.5f;
    mp.radius = std::max(std::max(maxx - minx, maxy - miny), maxz - minz) * 0.5f;
    if (mp.radius < 0.0001f) mp.radius = 1.0f;
    for (size_t i = 0; i < geoms.size(); ++i) {
        const auto& g = geoms[i];
        size_t vcount = g.positions.size() / 3;
        if (vcount == 0 || g.indices.empty()) continue;
        std::vector<MPVertex> vtx(vcount);
        bool hasN = (g.normals.size() == vcount * 3);
        bool hasT = (g.uvs.size() == vcount * 2);
        for (size_t v = 0; v < vcount; ++v) {
            vtx[v].px = g.positions[v * 3 + 0];
            vtx[v].py = g.positions[v * 3 + 1];
            vtx[v].pz = g.positions[v * 3 + 2];
            vtx[v].nx = hasN ? g.normals[v * 3 + 0] : 0.0f;
            vtx[v].ny = hasN ? g.normals[v * 3 + 1] : 1.0f;
            vtx[v].nz = hasN ? g.normals[v * 3 + 2] : 0.0f;
            vtx[v].u = hasT ? g.uvs[v * 2 + 0] : 0.0f;
            vtx[v].v = hasT ? g.uvs[v * 2 + 1] : 0.0f;
        }
        MPPerMesh m;
        glGenVertexArrays(1, &m.vao);
        glGenBuffers(1, &m.vbo);
        glGenBuffers(1, &m.ibo);
        glBindVertexArray(m.vao);
        glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
        glBufferData(GL_ARRAY_BUFFER, vtx.size() * sizeof(MPVertex), vtx.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, g.indices.size() * sizeof(uint32_t), g.indices.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MPVertex), (void*)offsetof(MPVertex, px));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MPVertex), (void*)offsetof(MPVertex, nx));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(MPVertex), (void*)offsetof(MPVertex, u));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
        m.index_count = (unsigned int)g.indices.size();
        bool hasA = false;
        if (!g.diffuse_tex_name.empty()) { m.tex_diffuse = load_tex_from_name(g.diffuse_tex_name, &hasA); }
        if (!m.tex_diffuse) m.tex_diffuse = mp.default_tex;
        if (!m.tex_normal) m.tex_normal = mp.default_tex;
        if (!m.tex_specular) m.tex_specular = mp.default_tex;
        if (!m.tex_unk) m.tex_unk = mp.default_tex;
        if (!m.tex_tint) m.tex_tint = mp.default_tex;
        m.has_alpha = hasA;
        mp.meshes.push_back(m);
    }
    return true;
}

static void mat4_identity(float* m) { memset(m, 0, 16 * sizeof(float)); m[0] = m[5] = m[10] = m[15] = 1.0f; }
static void mat4_perspective(float* m, float fov, float aspect, float znear, float zfar) {
    float f = 1.0f / tanf(fov * 0.5f);
    memset(m, 0, 16 * sizeof(float));
    m[0] = f / aspect; m[5] = f; m[10] = (zfar + znear) / (znear - zfar); m[11] = -1.0f; m[14] = (2.0f * zfar * znear) / (znear - zfar);
}
static void mat4_lookat(float* m, float ex, float ey, float ez, float cx, float cy, float cz, float ux, float uy, float uz) {
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float fl = sqrtf(fx * fx + fy * fy + fz * fz); fx /= fl; fy /= fl; fz /= fl;
    float sx = fy * uz - fz * uy, sy = fz * ux - fx * uz, sz = fx * uy - fy * ux;
    float sl = sqrtf(sx * sx + sy * sy + sz * sz); sx /= sl; sy /= sl; sz /= sl;
    float uux = sy * fz - sz * fy, uuy = sz * fx - sx * fz, uuz = sx * fy - sy * fx;
    m[0] = sx; m[4] = sy; m[8] = sz; m[12] = -(sx * ex + sy * ey + sz * ez);
    m[1] = uux; m[5] = uuy; m[9] = uuz; m[13] = -(uux * ex + uuy * ey + uuz * ez);
    m[2] = -fx; m[6] = -fy; m[10] = -fz; m[14] = (fx * ex + fy * ey + fz * ez);
    m[3] = 0; m[7] = 0; m[11] = 0; m[15] = 1;
}
static void mat4_rotateX(float* m, float angle) { mat4_identity(m); float c = cosf(angle), s = sinf(angle); m[5] = c; m[6] = s; m[9] = -s; m[10] = c; }
static void mat4_translate(float* m, float x, float y, float z) { mat4_identity(m); m[12] = x; m[13] = y; m[14] = z; }
static void mat4_mult(float* out, const float* a, const float* b) {
    float tmp[16];
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) tmp[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] + a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
    memcpy(out, tmp, 16 * sizeof(float));
}

void MP_Render(ModelPreview& mp, float yaw, float pitch, float dist) {
    glBindFramebuffer(GL_FRAMEBUFFER, mp.fbo);
    glViewport(0, 0, mp.width, mp.height);
    glClearColor(0.18f, 0.22f, 0.28f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDisable(GL_CULL_FACE);
    glUseProgram(mp.shader_program);
    float r = std::max(0.5f, dist) * mp.radius * 2.2f;
    float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
    float ex = mp.center[0] + r * cp * sy, ey = mp.center[1] + r * sp, ez = mp.center[2] + r * cp * cy;
    float V[16], P[16], W[16], Tm[16], R[16], Tp[16], tmp[16];
    mat4_lookat(V, ex, ey, ez, mp.center[0], mp.center[1], mp.center[2], 0, 1, 0);
    float fov = 60.0f * 3.14159265f / 180.0f, aspect = (float)mp.width / (float)mp.height;
    mat4_perspective(P, fov, aspect, 0.05f, r * 8.0f);
    const float tiltX = 3.14159265f / 2.0f;
    mat4_translate(Tm, -mp.center[0], -mp.center[1], -mp.center[2]);
    mat4_rotateX(R, tiltX);
    mat4_translate(Tp, mp.center[0], mp.center[1], mp.center[2]);
    mat4_mult(tmp, R, Tm); mat4_mult(W, Tp, tmp);
    float MV[16], MVP[16];
    mat4_mult(MV, V, W); mat4_mult(MVP, P, MV);
    glUniformMatrix4fv(mp.mvp_loc, 1, GL_FALSE, MVP);
    glUniformMatrix4fv(mp.mv_loc, 1, GL_FALSE, MV);
    glDepthMask(GL_TRUE);
    for (const auto& m : mp.meshes) {
        if (!m.vao || m.index_count == 0 || m.has_alpha) continue;
        glDisable(GL_BLEND);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m.tex_diffuse ? m.tex_diffuse : mp.default_tex); glUniform1i(mp.tex_diffuse_loc, 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, m.tex_normal ? m.tex_normal : mp.default_tex); glUniform1i(mp.tex_normal_loc, 1);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, m.tex_specular ? m.tex_specular : mp.default_tex); glUniform1i(mp.tex_specular_loc, 2);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, m.tex_unk ? m.tex_unk : mp.default_tex); glUniform1i(mp.tex_unk_loc, 3);
        glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, m.tex_tint ? m.tex_tint : mp.default_tex); glUniform1i(mp.tex_tint_loc, 4);
        glBindVertexArray(m.vao); glDrawElements(GL_TRIANGLES, m.index_count, GL_UNSIGNED_INT, 0); glBindVertexArray(0);
    }
    glDepthMask(GL_FALSE);
    for (const auto& m : mp.meshes) {
        if (!m.vao || m.index_count == 0 || !m.has_alpha) continue;
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m.tex_diffuse ? m.tex_diffuse : mp.default_tex); glUniform1i(mp.tex_diffuse_loc, 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, m.tex_normal ? m.tex_normal : mp.default_tex); glUniform1i(mp.tex_normal_loc, 1);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, m.tex_specular ? m.tex_specular : mp.default_tex); glUniform1i(mp.tex_specular_loc, 2);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, m.tex_unk ? m.tex_unk : mp.default_tex); glUniform1i(mp.tex_unk_loc, 3);
        glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, m.tex_tint ? m.tex_tint : mp.default_tex); glUniform1i(mp.tex_tint_loc, 4);
        glBindVertexArray(m.vao); glDrawElements(GL_TRIANGLES, m.index_count, GL_UNSIGNED_INT, 0); glBindVertexArray(0);
    }
    glDepthMask(GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

unsigned int MP_GetTexture(ModelPreview& mp) { return mp.color_tex; }

#endif