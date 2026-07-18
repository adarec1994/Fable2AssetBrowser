static bool create_pipeline(ID3D11Device* dev, ModelPreview& mp){
    ID3DBlob* vsb=nullptr; ID3DBlob* psb=nullptr;
    if(!compile_shader(g_vs,"VS","vs_5_0",&vsb)) return false;
    const std::string ps_fog_src = std::string(g_fog_hlsl) + g_ps;
    if(!compile_shader(ps_fog_src.c_str(),"PS","ps_5_0",&psb)){ if(vsb) vsb->Release(); return false; }
    if(FAILED(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &mp.vs))){ vsb->Release(); psb->Release(); return false; }
    if(FAILED(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &mp.ps))){ vsb->Release(); psb->Release(); return false; }

    D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD",   0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BONEIDS",    0, DXGI_FORMAT_R8G8B8A8_UINT,      0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BONEWEIGHTS",0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if(FAILED(dev->CreateInputLayout(il,5,vsb->GetBufferPointer(),vsb->GetBufferSize(),&mp.layout))){ vsb->Release(); psb->Release(); return false; }
    vsb->Release(); psb->Release();

    struct CB {
        XMFLOAT4X4 mvp;
        XMFLOAT4 lightDir;
        XMFLOAT4X4 mv;
        XMFLOAT4 params;
        XMFLOAT4 edit_off;
        XMFLOAT4 edit_rot;
        XMFLOAT4 edit_piv;
    };
    D3D11_BUFFER_DESC cbd{};
    cbd.BindFlags=D3D11_BIND_CONSTANT_BUFFER; cbd.ByteWidth=sizeof(CB); cbd.Usage=D3D11_USAGE_DYNAMIC; cbd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    if(FAILED(dev->CreateBuffer(&cbd,nullptr,&mp.cbuffer))) return false;

    D3D11_BUFFER_DESC bcb{};
    bcb.BindFlags     = D3D11_BIND_CONSTANT_BUFFER;
    bcb.ByteWidth     = (UINT)(MP_MAX_BONES * sizeof(XMFLOAT4X4));
    bcb.Usage         = D3D11_USAGE_DYNAMIC;
    bcb.CPUAccessFlags= D3D11_CPU_ACCESS_WRITE;
    if(FAILED(dev->CreateBuffer(&bcb, nullptr, &mp.bone_cb))) return false;
    D3D11_SAMPLER_DESC ssd{}; ssd.Filter=D3D11_FILTER_ANISOTROPIC; ssd.MaxAnisotropy=16; ssd.AddressU=ssd.AddressV=ssd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP; ssd.MaxLOD=D3D11_FLOAT32_MAX;
    if(FAILED(dev->CreateSamplerState(&ssd,&mp.sampler))) return false;

    {
        ID3DBlob* tvsb = nullptr; ID3DBlob* tpsb = nullptr;
        ID3DBlob* tdpsb = nullptr; ID3DBlob* tlpsb = nullptr;
        ID3DBlob* tppsb = nullptr;
        const std::string tps_live_src =
            std::string(g_fog_hlsl) + g_terrain_ps_live;
        const std::string tps_direct_src =
            std::string(g_fog_hlsl) + g_terrain_ps;
        const std::string tps_landscape_src =
            std::string(g_fog_hlsl) + g_terrain_ps_landscape;
        const std::string tps_paint_src =
            std::string(g_fog_hlsl) + g_terrain_ps_paint;
        if (compile_shader(g_terrain_vs, "VS", "vs_5_0", &tvsb) &&
            compile_shader(tps_live_src.c_str(), "PS", "ps_5_0", &tpsb))
        {
            dev->CreateVertexShader(tvsb->GetBufferPointer(),
                                    tvsb->GetBufferSize(),
                                    nullptr, &mp.vs_terrain);
            dev->CreatePixelShader(tpsb->GetBufferPointer(),
                                   tpsb->GetBufferSize(),
                                   nullptr, &mp.ps_terrain);
        }
        if (compile_shader(tps_direct_src.c_str(), "PS", "ps_5_0", &tdpsb)) {
            dev->CreatePixelShader(tdpsb->GetBufferPointer(),
                                   tdpsb->GetBufferSize(),
                                   nullptr, &mp.ps_terrain_direct);
        }
        if (compile_shader(tps_landscape_src.c_str(), "PS", "ps_5_0", &tlpsb)) {
            dev->CreatePixelShader(tlpsb->GetBufferPointer(),
                                   tlpsb->GetBufferSize(),
                                   nullptr, &mp.ps_terrain_landscape);
        }
        if (compile_shader(tps_paint_src.c_str(), "PS", "ps_5_0", &tppsb)) {
            dev->CreatePixelShader(tppsb->GetBufferPointer(),
                                   tppsb->GetBufferSize(),
                                   nullptr, &mp.ps_terrain_paint);
        }
        if (tvsb) tvsb->Release();
        if (tpsb) tpsb->Release();
        if (tdpsb) tdpsb->Release();
        if (tlpsb) tlpsb->Release();
        if (tppsb) tppsb->Release();
    }
    {
        D3D11_BUFFER_DESC tcb{};
        tcb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        tcb.ByteWidth = 80 + 32 * 16;
        tcb.Usage = D3D11_USAGE_DYNAMIC;
        tcb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dev->CreateBuffer(&tcb, nullptr, &mp.cbuffer_terrain);

        D3D11_BUFFER_DESC pcb = tcb;
        pcb.ByteWidth = 6 * 16;
        dev->CreateBuffer(&pcb, nullptr, &mp.cbuffer_terrain_paint);
    }

    Skybox::CreateD3D11Resources(dev, mp);

    {
        ID3DBlob* wvsb = nullptr; ID3DBlob* wpsb = nullptr;
        const std::string wps_src = std::string(g_fog_hlsl) + g_water_ps;
        if (compile_shader(g_water_vs, "VS", "vs_5_0", &wvsb) &&
            compile_shader(wps_src.c_str(), "PS", "ps_5_0", &wpsb))
        {
            dev->CreateVertexShader(wvsb->GetBufferPointer(),
                                    wvsb->GetBufferSize(),
                                    nullptr, &mp.vs_water);
            dev->CreatePixelShader(wpsb->GetBufferPointer(),
                                   wpsb->GetBufferSize(),
                                   nullptr, &mp.ps_water);
        }
        if (wvsb) wvsb->Release();
        if (wpsb) wpsb->Release();
    }
    {
        D3D11_BUFFER_DESC wcb{};
        wcb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

        wcb.ByteWidth = 10 * 16;
        wcb.Usage = D3D11_USAGE_DYNAMIC;
        wcb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dev->CreateBuffer(&wcb, nullptr, &mp.cbuffer_water);
    }
    {
        ID3DBlob* xvsb = nullptr; ID3DBlob* xpsb = nullptr;
        if (compile_shader(g_weather_vs, "VS", "vs_5_0", &xvsb) &&
            compile_shader(g_weather_ps, "PS", "ps_5_0", &xpsb))
        {
            dev->CreateVertexShader(xvsb->GetBufferPointer(),
                                    xvsb->GetBufferSize(),
                                    nullptr, &mp.vs_weather);
            dev->CreatePixelShader(xpsb->GetBufferPointer(),
                                   xpsb->GetBufferSize(),
                                   nullptr, &mp.ps_weather);
        }
        if (xvsb) xvsb->Release();
        if (xpsb) xpsb->Release();

        D3D11_BUFFER_DESC xcb{};
        xcb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        xcb.ByteWidth = 64 + 7 * 16;
        xcb.Usage = D3D11_USAGE_DYNAMIC;
        xcb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dev->CreateBuffer(&xcb, nullptr, &mp.cbuffer_weather);

        D3D11_BUFFER_DESC fcb{};
        fcb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        fcb.ByteWidth = 4 * 16;
        fcb.Usage = D3D11_USAGE_DYNAMIC;
        fcb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dev->CreateBuffer(&fcb, nullptr, &mp.cbuffer_fog);
    }
    {
        D3D11_SAMPLER_DESC psd{};
        psd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        psd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        psd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        psd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        psd.MaxLOD = D3D11_FLOAT32_MAX;
        dev->CreateSamplerState(&psd, &mp.sampler_point);
    }
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    rd.MultisampleEnable = FALSE;
    if(FAILED(dev->CreateRasterizerState(&rd,&mp.rs))) return false;

    D3D11_RASTERIZER_DESC rd_w = rd;
    rd_w.FillMode = D3D11_FILL_WIREFRAME;
    if(FAILED(dev->CreateRasterizerState(&rd_w, &mp.rs_wire))) return false;
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

    D3D11_BLEND_DESC bdw = bda;
    bdw.RenderTarget[0].SrcBlend       = D3D11_BLEND_ONE;
    bdw.RenderTarget[0].DestBlend      = D3D11_BLEND_SRC_ALPHA;
    bdw.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
    bdw.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    if(FAILED(dev->CreateBlendState(&bdw,&mp.bs_water))) return false;
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
    D3D11_DEPTH_STENCIL_DESC dssle = dssn;
    dssle.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    if(FAILED(dev->CreateDepthStencilState(&dssle, &mp.dssNoWriteLEqual))) return false;

    D3D11_DEPTH_STENCIL_DESC dsswat = dssw;
    dsswat.StencilEnable = TRUE;
    dsswat.StencilReadMask = 0xFF;
    dsswat.StencilWriteMask = 0xFF;
    dsswat.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL;
    dsswat.FrontFace.StencilPassOp = D3D11_STENCIL_OP_INCR_SAT;
    dsswat.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    dsswat.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    dsswat.BackFace = dsswat.FrontFace;
    if(FAILED(dev->CreateDepthStencilState(&dsswat, &mp.dssWaterOnce))) return false;
    if(!create_white_srv(dev, &mp.default_srv)) return false;

    {
        ID3DBlob* fvs = nullptr; ID3DBlob* fps = nullptr;
        if (compile_shader(g_fx_vs, "VS", "vs_5_0", &fvs) &&
            compile_shader(g_fx_ps, "PS", "ps_5_0", &fps)) {
            dev->CreateVertexShader(fvs->GetBufferPointer(), fvs->GetBufferSize(),
                                    nullptr, &mp.vs_fx);
            dev->CreatePixelShader(fps->GetBufferPointer(), fps->GetBufferSize(),
                                   nullptr, &mp.ps_fx);
            D3D11_INPUT_ELEMENT_DESC el[] = {
                {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,  D3D11_INPUT_PER_VERTEX_DATA,0},
                {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,   0,12, D3D11_INPUT_PER_VERTEX_DATA,0},
                {"COLOR",   0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,20,D3D11_INPUT_PER_VERTEX_DATA,0},
            };
            dev->CreateInputLayout(el, 3, fvs->GetBufferPointer(),
                                   fvs->GetBufferSize(), &mp.layout_fx);
        }
        if (fvs) fvs->Release();
        if (fps) fps->Release();

        D3D11_BUFFER_DESC cb{};
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cb.ByteWidth = 4 * 16;
        cb.Usage = D3D11_USAGE_DYNAMIC;
        cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dev->CreateBuffer(&cb, nullptr, &mp.cbuffer_fx);

        D3D11_BLEND_DESC ba{};
        ba.RenderTarget[0].BlendEnable = TRUE;
        ba.RenderTarget[0].SrcBlend  = D3D11_BLEND_ONE;
        ba.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        ba.RenderTarget[0].BlendOp   = D3D11_BLEND_OP_ADD;
        ba.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
        ba.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        ba.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
        ba.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        dev->CreateBlendState(&ba, &mp.bs_fx_alpha);
        D3D11_BLEND_DESC badd = ba;
        badd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        dev->CreateBlendState(&badd, &mp.bs_fx_add);
    }
    return true;
}
