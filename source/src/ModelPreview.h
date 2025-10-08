#pragma once
#include <vector>
#include <string>
#include <d3d11.h>
#include "ModelParser.h"

struct MPVertex {
    float px,py,pz;
    float nx,ny,nz;
    float u,v;
};

struct MPPerMesh {
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    UINT index_count = 0;
    ID3D11ShaderResourceView* srv = nullptr;
    bool has_alpha = false;
    float center[3] = {0,0,0};
    float radius = 0.0f;
};

struct ModelPreview {
    ID3D11Texture2D* color = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11Texture2D* depth = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;

    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11Buffer* cbuffer = nullptr;
    ID3D11SamplerState* sampler = nullptr;

    ID3D11RasterizerState* rs = nullptr;
    ID3D11BlendState* bs = nullptr;
    ID3D11BlendState* bsAlpha = nullptr;
    ID3D11DepthStencilState* dssWrite = nullptr;
    ID3D11DepthStencilState* dssNoWrite = nullptr;

    int width = 1024;
    int height = 768;
    float center[3] = {0,0,0};
    float radius = 1.0f;

    ID3D11ShaderResourceView* default_srv = nullptr;

    std::vector<MPPerMesh> meshes;
};

bool MP_Init(ID3D11Device* dev, ModelPreview& mp, int w, int h);
void MP_Release(ModelPreview& mp);
bool MP_Build(ID3D11Device* dev, const std::vector<MDLMeshGeom>& geoms, const MDLInfo& info, ModelPreview& mp);
void MP_Render(ID3D11Device* dev, ModelPreview& mp, float yaw, float pitch, float dist);
