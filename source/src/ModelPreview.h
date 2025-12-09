#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "ModelParser.h"

#ifdef _WIN32
#include <d3d11.h>
#endif

struct MPVertex {
    float px,py,pz;
    float nx,ny,nz;
    float u,v;
};

struct MPPerMesh {
#ifdef _WIN32
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    ID3D11ShaderResourceView* srv_diffuse = nullptr;
    ID3D11ShaderResourceView* srv_normal = nullptr;
    ID3D11ShaderResourceView* srv_specular = nullptr;
    ID3D11ShaderResourceView* srv_unk = nullptr;
    ID3D11ShaderResourceView* srv_tint = nullptr;
#else
    unsigned int vao = 0;
    unsigned int vbo = 0;
    unsigned int ibo = 0;
    unsigned int tex_diffuse = 0;
    unsigned int tex_normal = 0;
    unsigned int tex_specular = 0;
    unsigned int tex_unk = 0;
    unsigned int tex_tint = 0;
#endif
    unsigned int index_count = 0;
    bool has_alpha = false;
    float center[3] = {0,0,0};
    float radius = 0.0f;
};

struct ModelPreview {
#ifdef _WIN32
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

    ID3D11ShaderResourceView* default_srv = nullptr;
#else
    unsigned int fbo = 0;
    unsigned int color_tex = 0;
    unsigned int depth_rbo = 0;

    unsigned int shader_program = 0;
    unsigned int default_tex = 0;

    int mvp_loc = -1;
    int mv_loc = -1;
    int light_dir_loc = -1;
    int params_loc = -1;
    int tex_diffuse_loc = -1;
    int tex_normal_loc = -1;
    int tex_specular_loc = -1;
    int tex_unk_loc = -1;
    int tex_tint_loc = -1;
#endif

    int width = 1024;
    int height = 768;
    float center[3] = {0,0,0};
    float radius = 1.0f;

    std::vector<MPPerMesh> meshes;
};

#ifdef _WIN32
bool MP_Init(ID3D11Device* dev, ModelPreview& mp, int w, int h);
void MP_Release(ModelPreview& mp);
bool MP_Build(ID3D11Device* dev, const std::vector<MDLMeshGeom>& geoms, const MDLInfo& info, ModelPreview& mp);
void MP_Render(ID3D11Device* dev, ModelPreview& mp, float yaw, float pitch, float dist);
#else
bool MP_Init(ModelPreview& mp, int w, int h);
void MP_Release(ModelPreview& mp);
bool MP_Build(const std::vector<MDLMeshGeom>& geoms, const MDLInfo& info, ModelPreview& mp);
void MP_Render(ModelPreview& mp, float yaw, float pitch, float dist);
unsigned int MP_GetTexture(ModelPreview& mp);
#endif
