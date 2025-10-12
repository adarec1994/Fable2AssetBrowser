// src/ModelPreview.cpp
#include <vector>
#include <string>
#include <algorithm>
#include <filesystem>
#include <optional>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include "ModelPreview.h"
#include "Files.h"
#include "Utils.h"
#include "BNKCore.cpp"
#include "TexParser.h"

using namespace DirectX;

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
    if(mp.rtv){ mp.rtv->Release(); mp.rtv=nullptr; }
    if(mp.srv){ mp.srv->Release(); mp.srv=nullptr; }
    if(mp.color){ mp.color->Release(); mp.color=nullptr; }
    if(mp.dsv){ mp.dsv->Release(); mp.dsv=nullptr; }
    if(mp.depth){ mp.depth->Release(); mp.depth=nullptr; }
    if(mp.default_srv){ mp.default_srv->Release(); mp.default_srv=nullptr; }
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
    float4   params;   // x: ambientK, y: specPower, z: unused, w: unused
}
Texture2D tex0 : register(t0); // diffuse (with alpha)
Texture2D tex1 : register(t1); // normal
Texture2D tex2 : register(t2); // specular
Texture2D tex3 : register(t3); // unk
Texture2D tex4 : register(t4); // tint
SamplerState smp : register(s0);

struct VSOUT{ float4 p:SV_Position; float3 n:NORMAL; float2 t:TEXCOORD0; };

float3 hemiAmbient(float3 n) {
    // simple hemisphere ambient: up sky vs. ground
    float up = saturate(n.y*0.5 + 0.5);
    float3 sky    = float3(0.65, 0.67, 0.70);
    float3 ground = float3(0.25, 0.24, 0.23);
    return lerp(ground, sky, up);
}

float4 PS(VSOUT i) : SV_Target {
    float3 N_geo = normalize(i.n);
    float3 L = normalize(lightDir.xyz);

    // normal map (tangent basis is approximated by using model space here,
    // good enough for preview)
    float3 N_m = tex1.Sample(smp, i.t).rgb * 2.0 - 1.0;
    N_m = normalize(N_m);
    float3 N = normalize(N_geo + N_m * 0.5);

    float3 albedo   = tex0.Sample(smp, i.t).rgb;
    float  alpha    = tex0.Sample(smp, i.t).a;
    float3 specTex  = tex2.Sample(smp, i.t).rgb;
    float3 tint     = tex4.Sample(smp, i.t).rgb;

    albedo *= tint;

    float ndotl = saturate(dot(N, L));
    float3 amb  = hemiAmbient(N) * params.x;          // ambientK (params.x)
    float3 diff = albedo * (0.6 * ndotl);             // increased diffuse contribution
    float3 V    = normalize(float3(0,0,1));           // fake view dir
    float3 H    = normalize(L + V);
    float  s    = pow(saturate(dot(N, H)), params.y); // spec power (params.y)
    float3 spec = specTex * s * 0.35;

    float3 color = albedo * amb + diff + spec;

    // simple gamma-ish lift for preview readability
    color = pow(color, 1.0/1.8);

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

    D3D11_SAMPLER_DESC sd{}; sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP; sd.MaxLOD=D3D11_FLOAT32_MAX;
    if(FAILED(dev->CreateSamplerState(&sd,&mp.sampler))) return false;

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    rd.MultisampleEnable = FALSE;
    if(FAILED(dev->CreateRasterizerState(&rd,&mp.rs))) return false;

    // Opaque blend state
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable=FALSE;
    bd.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    if(FAILED(dev->CreateBlendState(&bd,&mp.bs))) return false;

    // Alpha blend state
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

    if(!create_white_srv(dev, &mp.default_srv)) return false;

    return true;
}

static ID3D11ShaderResourceView* create_srv_from_rgba(ID3D11Device* dev, int w, int h, const std::vector<uint8_t>& rgba){
    D3D11_TEXTURE2D_DESC td{}; td.Width=w; td.Height=h; td.MipLevels=1; td.ArraySize=1; td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_IMMUTABLE; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem=rgba.data(); sd.SysMemPitch=(UINT)(w*4);
    ID3D11Texture2D* t=nullptr; if(FAILED(dev->CreateTexture2D(&td,&sd,&t))) return nullptr;
    ID3D11ShaderResourceView* v=nullptr; if(FAILED(dev->CreateShaderResourceView(t,nullptr,&v))){ t->Release(); return nullptr; }
    t->Release(); return v;
}

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
            if(fn_low == w || fn_base_noext == w){
                match = true;
                break;
            }
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
        for(const auto& mip : ti.Mips){
            if(mip.CompFlag == 7){
                has_uncompressed = true;
                break;
            }
        }
        if(!has_uncompressed) continue;

        size_t area = (size_t)ti.TextureWidth * (size_t)ti.TextureHeight;
        if(area > best_area){
            best_area = area; best_idx = (int)i; out.swap(blob);
        }
    }
    return best_idx >= 0 && !out.empty();
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

static bool srv_from_tex_blob_auto(ID3D11Device* dev,
                                   const std::vector<unsigned char>& blob,
                                   ID3D11ShaderResourceView** out_srv,
                                   bool* out_has_alpha)
{
    *out_srv=nullptr;
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

    std::vector<uint8_t> rgba((size_t)w*(size_t)h*4, 0xFF);
    const uint8_t* src = blob.data() + m.MipDataOffset;

    auto any_alpha_lt_255 = [&](const std::vector<uint8_t>& buf)->bool{
        const uint8_t* p = buf.data();
        size_t n = buf.size();
        for(size_t i=3;i<n;i+=4){
            if(p[i] < 255){ return true; }
        }
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
        if(out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
        *out_srv = create_srv_from_rgba(dev, w, h, rgba);
        return (*out_srv != nullptr);
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
        if(out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
        *out_srv = create_srv_from_rgba(dev, w, h, rgba);
        return (*out_srv != nullptr);
    }

    if(ti.PixelFormat == 40) {
        return false;
    }

    if(m.MipDataSizeParsed < sz_raw) return false;
    memcpy(rgba.data(), src, sz_raw);
    if(out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
    *out_srv = create_srv_from_rgba(dev, w, h, rgba);
    return (*out_srv != nullptr);
}

static bool build_mesh_textures(ID3D11Device* dev,
                                const MDLInfo& info,
                                size_t mesh_index,
                                ID3D11ShaderResourceView** out_diffuse,
                                ID3D11ShaderResourceView** out_normal,
                                ID3D11ShaderResourceView** out_specular,
                                ID3D11ShaderResourceView** out_unk,
                                ID3D11ShaderResourceView** out_tint,
                                bool* out_has_alpha)
{
    *out_diffuse = nullptr;
    *out_normal = nullptr;
    *out_specular = nullptr;
    *out_unk = nullptr;
    *out_tint = nullptr;
    if(out_has_alpha) *out_has_alpha = false;

    if (mesh_index >= info.Meshes.size()) return true;
    const auto& mesh = info.Meshes[mesh_index];
    if(mesh.Materials.empty()) return true;
    const auto& mat = mesh.Materials[0];

    auto load_texture = [&](const std::string& tex_name, ID3D11ShaderResourceView** out_srv, bool want_alpha){
        if(tex_name.empty()) return;

        std::vector<std::string> candidates;
        candidates.push_back(tex_name);
        std::string fname = std::filesystem::path(tex_name).filename().string();
        if(!fname.empty()) candidates.push_back(fname);
        if (tex_name.find(".tex") == std::string::npos) {
            candidates.push_back(tex_name + ".tex");
            if(!fname.empty()) candidates.push_back(fname + ".tex");
        }
        std::string basename = std::filesystem::path(tex_name).stem().string();
        if (!basename.empty()) {
            candidates.push_back(basename);
            candidates.push_back(basename + ".tex");
        }

        std::vector<unsigned char> blob;
        for (const auto& candidate : candidates) {
            if (build_tex_buffer_for_name(candidate, blob)) {
                bool hasA = false;
                if (srv_from_tex_blob_auto(dev, blob, out_srv, &hasA)) {
                    if(want_alpha && out_has_alpha && hasA) *out_has_alpha = true;
                    if (*out_srv) return;
                }
            }
        }

        if (extract_tex_bytes_by_candidate(candidates, blob)) {
            bool hasA = false;
            if (srv_from_tex_blob_auto(dev, blob, out_srv, &hasA)) {
                if(want_alpha && out_has_alpha && hasA) *out_has_alpha = true;
            }
        }
    };

    load_texture(mat.TextureName,     out_diffuse, true);
    load_texture(mat.NormalMapName,   out_normal,  false);
    load_texture(mat.SpecularMapName, out_specular,false);
    load_texture(mat.UnkName,         out_unk,     false);
    load_texture(mat.TintName,        out_tint,    false);

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

    mp.meshes.resize(geoms.size());
    for(size_t i=0;i<geoms.size();++i){
        const auto& g = geoms[i];
        auto& m = mp.meshes[i];

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

        D3D11_BUFFER_DESC vb{}; vb.BindFlags=D3D11_BIND_VERTEX_BUFFER; vb.ByteWidth=(UINT)(vtx.size()*sizeof(MPVertex)); vb.Usage=D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA vsd{}; vsd.pSysMem=vtx.data();
        if(FAILED(dev->CreateBuffer(&vb,&vsd,&m.vb))) return false;

        std::vector<uint32_t> idx = g.indices;
        D3D11_BUFFER_DESC ib{}; ib.BindFlags=D3D11_BIND_INDEX_BUFFER; ib.ByteWidth=(UINT)(idx.size()*sizeof(uint32_t)); ib.Usage=D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA isd{}; isd.pSysMem=idx.data();
        if(FAILED(dev->CreateBuffer(&ib,&isd,&m.ib))) return false;
        m.index_count = (UINT)idx.size();

        bool hasA = false;
        build_mesh_textures(dev, info, i, &m.srv_diffuse, &m.srv_normal, &m.srv_specular, &m.srv_unk, &m.srv_tint, &hasA);

        if (!m.srv_diffuse && mp.default_srv) { m.srv_diffuse = mp.default_srv; m.srv_diffuse->AddRef(); }
        if (!m.srv_normal  && mp.default_srv) { m.srv_normal  = mp.default_srv; m.srv_normal->AddRef(); }
        if (!m.srv_specular&& mp.default_srv) { m.srv_specular= mp.default_srv; m.srv_specular->AddRef(); }
        if (!m.srv_unk     && mp.default_srv) { m.srv_unk     = mp.default_srv; m.srv_unk->AddRef(); }
        if (!m.srv_tint    && mp.default_srv) { m.srv_tint    = mp.default_srv; m.srv_tint->AddRef(); }

        m.has_alpha = hasA;
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

    struct CB { XMFLOAT4X4 mvp; XMFLOAT4 lightDir; XMFLOAT4X4 mv; XMFLOAT4 params; } cb;
    XMStoreFloat4x4(&cb.mvp, MVP);
    XMStoreFloat4x4(&cb.mv,  MV);
    cb.lightDir = XMFLOAT4(-0.4f, 0.85f, 0.45f, 0.0f);   // a bit steeper light
    cb.params   = XMFLOAT4(0.4f, 48.0f, 0.0f, 0.0f);    // ambientK, specPower

    D3D11_MAPPED_SUBRESOURCE ms{};
    if(SUCCEEDED(ctx->Map(mp.cbuffer,0,D3D11_MAP_WRITE_DISCARD,0,&ms))){
        memcpy(ms.pData, &cb, sizeof(cb));
        ctx->Unmap(mp.cbuffer,0);
    }
    ctx->VSSetConstantBuffers(0,1,&mp.cbuffer);
    ctx->PSSetConstantBuffers(0,1,&mp.cbuffer);

    float blend_factor[4] = {0,0,0,0};

    for(const auto& m : mp.meshes){
        if(!m.vb || !m.ib || m.index_count==0) continue;

        ctx->OMSetBlendState(m.has_alpha ? mp.bsAlpha : mp.bs, blend_factor, 0xFFFFFFFF);

        UINT stride=sizeof(MPVertex), offset=0;
        ctx->IASetVertexBuffers(0,1,&m.vb,&stride,&offset);
        ctx->IASetIndexBuffer(m.ib, DXGI_FORMAT_R32_UINT, 0);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        ID3D11ShaderResourceView* srvs[5] = {
            m.srv_diffuse ? m.srv_diffuse : mp.default_srv,
            m.srv_normal ? m.srv_normal : mp.default_srv,
            m.srv_specular ? m.srv_specular : mp.default_srv,
            m.srv_unk ? m.srv_unk : mp.default_srv,
            m.srv_tint ? m.srv_tint : mp.default_srv
        };
        ctx->PSSetShaderResources(0, 5, srvs);

        ctx->DrawIndexed(m.index_count,0,0);

        ID3D11ShaderResourceView* nullsrvs[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
        ctx->PSSetShaderResources(0, 5, nullsrvs);
    }

    ctx->Release();
}


