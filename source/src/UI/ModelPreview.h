#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <memory>
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
    bool is_water   = false;
    bool is_cloth   = false;
    bool alpha_test = true;   // diffuse.a<0.25 discard (off for engine geom)
    bool cloth_sim  = false;  // render via cloth solver (dynamic VB, no GPU skin)
    std::shared_ptr<struct ClothSim> cloth;  // per-mesh soft-body state
    float water_params[38] = {};
    bool has_water_theme = false;
    float water_opacity = 1.0f;
    float water_shallow_colour[3] = {0.155f, 0.285f, 0.235f};
    float water_deep_colour[3] = {0.010f, 0.075f, 0.085f};
    float water_theme_params[10] = {};

    uint32_t source_mesh_idx = 0;

    uint32_t lod_index = 0;

    struct PickRange {
        uint32_t selection_id = 0;
        uint32_t index_start = 0;
        uint32_t index_count = 0;
        float center[3] = {0.0f, 0.0f, 0.0f};
        float radius = 0.0f;
    };
    std::vector<PickRange> pick_ranges;
    std::vector<float> pick_positions;
    std::vector<uint32_t> pick_indices;
};

struct MPSkyCloudKeyframe {
    float time_of_day = 0.0f;
    float sky_top_colour[3] = {0.42f, 0.56f, 0.76f};
    float sky_bottom_colour[3] = {0.55f, 0.60f, 0.65f};
    float sky_sunset_colour[3] = {1.0f, 0.47f, 0.22f};
    float sky_params[4] = {1.0f, 0.35f, 1.0f, 1.0f};
    bool has_cloud_theme = false;
    int cloud_layer_count = 0;
    float cloud_layer[4][4] = {};
    float cloud_shape[4][4] = {};
    float cloud_motion[4][4] = {};
    float cloud_light[4][4] = {};
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
    ID3D11PixelShader*  ps_terrain_direct = nullptr;
    ID3D11Buffer*       cbuffer_terrain = nullptr;
    ID3D11VertexShader* vs_sky = nullptr;
    ID3D11PixelShader*  ps_sky = nullptr;
    ID3D11Buffer*       cbuffer_sky = nullptr;
    ID3D11ShaderResourceView* cloud_density_srv[4] = {
        nullptr, nullptr, nullptr, nullptr
    };
    bool cloud_density_tried[4] = {false, false, false, false};
    ID3D11ShaderResourceView* sky_overlay_srv = nullptr;
    ID3D11ShaderResourceView* sky_sun_disc_srv = nullptr;
    ID3D11ShaderResourceView* sky_moon_srv = nullptr;
    ID3D11ShaderResourceView* sky_moon_glare_srv = nullptr;
    bool sky_overlay_tried = false;
    bool sky_sun_disc_tried = false;
    bool sky_moon_tried = false;
    bool sky_moon_glare_tried = false;
    ID3D11VertexShader* vs_water = nullptr;
    ID3D11PixelShader*  ps_water = nullptr;
    ID3D11Buffer*       cbuffer_water = nullptr;
    ID3D11SamplerState* sampler_point = nullptr;
    ID3D11RasterizerState* rs = nullptr;
    ID3D11RasterizerState* rs_wire = nullptr;
    ID3D11BlendState* bs = nullptr;
    ID3D11BlendState* bsAlpha = nullptr;
    ID3D11DepthStencilState* dssWrite = nullptr;
    ID3D11DepthStencilState* dssNoWrite = nullptr;
    ID3D11DepthStencilState* dssNoWriteLEqual = nullptr;
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
    bool has_sky_theme = false;
    float sky_top_colour[3] = {0.42f, 0.56f, 0.76f};
    float sky_bottom_colour[3] = {0.55f, 0.60f, 0.65f};
    float sky_sunset_colour[3] = {1.0f, 0.47f, 0.22f};
    float sky_params[4] = {1.0f, 0.35f, 1.0f, 1.0f};
    float sky_time_of_day = -1.0f;
    bool has_cloud_theme = false;
    int cloud_layer_count = 0;
    float cloud_layer[4][4] = {
        {0.35f, 350.0f, 2.5f, 2.0f},
        {0.22f, 420.0f, 4.5f, 3.0f},
        {0.12f, 520.0f, 7.0f, 4.0f},
        {0.08f, 650.0f, 10.0f, 6.0f}
    };
    float cloud_shape[4][4] = {
        {1024.0f, 1024.0f, 0.0f, 0.0f},
        {1536.0f, 1536.0f, 0.0f, 0.0f},
        {2048.0f, 2048.0f, 0.0f, 0.0f},
        {3072.0f, 3072.0f, 0.0f, 0.0f}
    };
    float cloud_motion[4][4] = {
        {0.0f, 0.0f,  0.010f,  0.004f},
        {0.2f, 0.4f, -0.006f,  0.008f},
        {0.6f, 0.1f,  0.004f, -0.005f},
        {0.8f, 0.7f, -0.012f,  0.003f}
    };
    float cloud_light[4][4] = {
        {0.70f, 0.45f, 0.45f, 0.35f},
        {0.62f, 0.42f, 0.40f, 0.30f},
        {0.55f, 0.38f, 0.35f, 0.25f},
        {0.48f, 0.34f, 0.30f, 0.22f}
    };
    std::string cloud_density_tex_name[4];
    std::string sky_overlay_tex_name;
    std::string sky_sun_disc_tex_name;
    std::string sky_moon_tex_name;
    std::string sky_moon_glare_tex_name;
    bool has_day_night_cycle = false;
    float day_night_cycle_seconds = 180.0f;
    std::vector<MPSkyCloudKeyframe> day_night_keyframes;
    bool time_of_day_override = false;
    float time_of_day_override_value = 0.5f;
    float current_time_of_day = 0.5f;

    bool wireframe = false;

    bool no_tilt = false;
    uint32_t selected_pick_id = 0;

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
