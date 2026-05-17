#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "../MDL/ModelParser.h"
#ifdef _WIN32
#include <d3d11.h>
#endif
struct MPVertex {
    float    px, py, pz;
    float    nx, ny, nz;
    float    u, v;

    uint8_t  b0, b1, b2, b3;
    float    w0, w1, w2, w3;
};
struct FlyCam {
    float pos[3] = {0.0f, 0.0f, 5.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float move_speed = 5.0f;
    float look_sensitivity = 0.003f;
    bool is_looking = false;
    float saved_mouse_x = 0.0f;
    float saved_mouse_y = 0.0f;
};
struct MPPerMesh {
#ifdef _WIN32
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    ID3D11ShaderResourceView* srv_diffuse = nullptr;
    ID3D11ShaderResourceView* srv_normal = nullptr;
    ID3D11ShaderResourceView* srv_specular = nullptr;
    ID3D11ShaderResourceView* srv_metallic = nullptr;
    ID3D11ShaderResourceView* srv_extra = nullptr;
#else
    unsigned int vao = 0;
    unsigned int vbo = 0;
    unsigned int ibo = 0;
    unsigned int tex_diffuse = 0;
    unsigned int tex_normal = 0;
    unsigned int tex_specular = 0;
    unsigned int tex_metallic = 0;
    unsigned int tex_extra = 0;
#endif
    unsigned int index_count = 0;
    bool has_alpha = false;
    float center[3] = {0,0,0};
    float radius = 0.0f;

    std::string diffuse_tex_name;
    std::string normal_tex_name;
    std::string specular_tex_name;
    std::string metallic_tex_name;
    std::string extra_tex_name;
    bool diffuse_visible  = true;
    bool normal_visible   = true;
    bool specular_visible = true;
    bool metallic_visible = true;
    bool extra_visible    = true;

    std::string name;

    bool highlight = false;
    bool isolated  = false;

    bool is_terrain = false;

    uint32_t source_mesh_idx = 0;

    uint32_t lod_index = 0;
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
    ID3D11VertexShader* vs_terrain = nullptr;
    ID3D11PixelShader*  ps_terrain = nullptr;
    ID3D11Buffer*       cbuffer_terrain = nullptr;
    ID3D11SamplerState* sampler_point = nullptr;
    ID3D11RasterizerState* rs = nullptr;
    ID3D11RasterizerState* rs_wire = nullptr;
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
    int tex_metallic_loc = -1;
    int tex_extra_loc = -1;
#endif
    int width = 1024;
    int height = 768;
    float center[3] = {0,0,0};
    float radius = 1.0f;
    std::vector<MPPerMesh> meshes;
    bool has_model = false;

    bool wireframe = false;

    bool no_tilt = false;

    uint32_t lod_count    = 1;
    int32_t  selected_lod = -1;

#ifdef _WIN32
    ID3D11Buffer* bone_cb = nullptr;
#endif
    uint32_t              bone_count = 0;
    std::vector<int>      bone_parents;
    std::vector<float>    local_rest;
    std::vector<float>    inv_bind;
};

static constexpr uint32_t MP_MAX_BONES = 256;
extern FlyCam g_flycam;
#ifdef _WIN32
bool MP_Init(ID3D11Device* dev, ModelPreview& mp, int w, int h);
void MP_Release(ModelPreview& mp);
bool MP_Build(ID3D11Device* dev, const std::vector<MDLMeshGeom>& geoms, const MDLInfo& info, ModelPreview& mp);
void MP_Render(ID3D11Device* dev, ModelPreview& mp, const FlyCam& cam);
void MP_Resize(ID3D11Device* dev, ModelPreview& mp, int w, int h);
#else
bool MP_Init(ModelPreview& mp, int w, int h);
void MP_Release(ModelPreview& mp);
bool MP_Build(const std::vector<MDLMeshGeom>& geoms, const MDLInfo& info, ModelPreview& mp);
void MP_Render(ModelPreview& mp, const FlyCam& cam);
void MP_Resize(ModelPreview& mp, int w, int h);
unsigned int MP_GetTexture(ModelPreview& mp);
#endif
void FlyCam_Reset(FlyCam& cam, float cx, float cy, float cz, float radius);
void FlyCam_Update(FlyCam& cam, float dt, bool w, bool s, bool a, bool d, bool q, bool e, float mouse_dx, float mouse_dy);

void MP_TextureCache_Clear();

bool decode_tex_to_rgba(const std::vector<unsigned char>& blob,
                        std::vector<uint8_t>& rgba,
                        int& out_w, int& out_h, bool* out_has_alpha,
                        int mip_index = -1);
#ifdef _WIN32
ID3D11ShaderResourceView* create_srv_from_rgba(ID3D11Device* dev, int w, int h, const std::vector<uint8_t>& rgba);
#else
unsigned int create_gl_texture_from_rgba(int w, int h, const uint8_t* rgba);
#endif

#ifdef _WIN32

void MP_ComputeWorldPose(const ModelPreview& mp,
                         const std::vector<float>& deltas,
                         std::vector<float>& out_world_pose);
#endif
