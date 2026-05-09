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
    // 4 bone IDs (truncated from MDLMeshGeom's uint16) + 4 weights. We
    // cap the per-mesh max bone count at 256 so an 8-bit index per slot
    // is enough. Static / unskinned meshes get (0,0,0,0) ids and a
    // (1,0,0,0) weight — combined with bone[0] = identity in the cbuffer
    // for non-skeletal models, this skins to a no-op.
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
    // Texture toggle state — when false, the corresponding sampler is fed
    // the default white SRV instead of the real texture.
    std::string diffuse_tex_name;
    std::string normal_tex_name;
    std::string specular_tex_name;
    std::string tint_tex_name;
    bool diffuse_visible  = true;
    bool normal_visible   = true;
    bool specular_visible = true;
    bool tint_visible     = true;
    // Submesh display name — copied from MDLMeshGeom.name at build time.
    // Drives the per-section header in the Materials overlay.
    std::string name;
    // ---- Per-mesh selection flags driven by the Materials overlay ----
    // `highlight`: paint this submesh green in the rendered image (a
    // shader tint, not a vertex/material change). `isolated`: when ANY
    // mesh has this set MP_Render skips drawing the others. Spec from
    // the Materials overlay: only ONE mesh can have either flag at a
    // time, and the two flags are mutually exclusive globally — so the
    // checkboxes act as radios across every submesh + slot.
    bool highlight = false;
    bool isolated  = false;
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
    bool has_model = false;

    // ---- Skinning (populated by MP_Build, consumed by MP_Render) ----
    // bone_count is the number of bones we actually upload to the GPU;
    // capped at MP_MAX_BONES below. local_rest holds each bone's TRS as
    // 11 floats (rotation quat[4], translation[3], scale[3], unused[1])
    // — same layout MDLInfo.BoneTransforms uses. inv_bind[i] is the
    // inverse of the bone's rest world matrix; multiplied against the
    // per-frame pose matrix it produces the skin matrix the shader
    // wants. bone_parents[i] is the original ParentID (or -1 for roots).
#ifdef _WIN32
    ID3D11Buffer* bone_cb = nullptr;
#endif
    uint32_t              bone_count = 0;
    std::vector<int>      bone_parents;
    std::vector<float>    local_rest;     // [bone_count * 11]
    std::vector<float>    inv_bind;       // [bone_count * 16] row-major
};

// Cap on bones the skinning cbuffer holds. Fable 2 character skeletons
// are well under this — typical humanoid rigs run 60–120 bones.
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
bool decode_tex_to_rgba(const std::vector<unsigned char>& blob, std::vector<uint8_t>& rgba, int& out_w, int& out_h, bool* out_has_alpha);
#ifdef _WIN32
ID3D11ShaderResourceView* create_srv_from_rgba(ID3D11Device* dev, int w, int h, const std::vector<uint8_t>& rgba);
#else
unsigned int create_gl_texture_from_rgba(int w, int h, const uint8_t* rgba);
#endif

#ifdef _WIN32
// Compute world-space pose matrices for every bone in mp's cached
// skeleton, applying the per-bone rotation deltas in `deltas` (length
// expected: mp.bone_count*4, quaternion xyzw per bone — pass an empty
// vector to get the rest pose). Output is row-major 4x4 matrices,
// length mp.bone_count*16. Used by the skeleton overlay so the joint
// positions track the user's edits in lock-step with the skinned mesh.
void MP_ComputeWorldPose(const ModelPreview& mp,
                         const std::vector<float>& deltas,
                         std::vector<float>& out_world_pose);
#endif