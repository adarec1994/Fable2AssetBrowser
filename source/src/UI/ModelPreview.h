#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <array>
#include "../MDL/ModelParser.h"
#include "../Level/LevelEdit.h"
#include "../Level/Skybox/CloudRuntime.h"
#include "../Level/Skybox/SkyboxTypes.h"
#include "../Level/ParticleFX.h"
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
    LevelEdit::EditXform edit_xform;

    bool is_terrain = false;
    bool is_water   = false;
    bool is_cloth   = false;
    bool alpha_test = true;
    bool cloth_sim  = false;
    std::shared_ptr<struct ClothSim> cloth;
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
        float inst_pos[3] = {0.0f, 0.0f, 0.0f};
        float inst_rot_deg[3] = {0.0f, 0.0f, 0.0f};
        bool  has_transform = false;
        uint64_t inst_hash = 0;
        uint32_t pos_file_offset = 0;
        uint32_t gdb_pos_off[3] = {0, 0, 0};
        uint32_t gdb_rot_off[3] = {0, 0, 0};
        uint8_t lev_rec_kind = 0;
    };
    std::vector<PickRange> pick_ranges;
    std::vector<float> pick_positions;
    std::vector<uint32_t> pick_indices;
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
    ID3D11PixelShader*  ps_terrain_landscape = nullptr;
    ID3D11Buffer*       cbuffer_terrain = nullptr;
    ID3D11VertexShader* vs_sky = nullptr;
    ID3D11PixelShader*  ps_sky = nullptr;
    ID3D11Buffer*       cbuffer_sky = nullptr;

    ID3D11VertexShader* vs_sky_dome = nullptr;
    ID3D11PixelShader*  ps_sky_dome = nullptr;
    ID3D11Buffer*       cbuffer_sky_dome = nullptr;
    ID3D11Texture2D*    sky_lut_tex = nullptr;
    ID3D11ShaderResourceView* sky_lut_srv = nullptr;
    ID3D11SamplerState* sampler_sky_clamp = nullptr;
    ID3D11VertexShader* vs_cloud = nullptr;
    ID3D11PixelShader*  ps_cloud = nullptr;
    ID3D11InputLayout*  layout_cloud = nullptr;
    ID3D11Buffer*       cbuffer_cloud = nullptr;
    ID3D11Buffer*       cloud_vb = nullptr;
    ID3D11Buffer*       cloud_ib = nullptr;
    ID3D11SamplerState* sampler_cloud = nullptr;
    ID3D11ShaderResourceView* cloud_density_srv[4] = {
        nullptr, nullptr, nullptr, nullptr
    };
    bool cloud_density_tried[4] = {false, false, false, false};
    ID3D11ShaderResourceView* sky_overlay_srv = nullptr;
    ID3D11ShaderResourceView* sky_sun_disc_srv = nullptr;
    ID3D11ShaderResourceView* sky_moon_srv = nullptr;
    ID3D11ShaderResourceView* sky_moon_glare_srv = nullptr;
    ID3D11ShaderResourceView* sky_sun_beams_srv = nullptr;
    ID3D11ShaderResourceView* sky_sun_glare_srv = nullptr;
    bool sky_overlay_tried = false;
    bool sky_sun_disc_tried = false;
    bool sky_moon_tried = false;
    bool sky_moon_glare_tried = false;
    bool sky_sun_beams_tried = false;
    bool sky_sun_glare_tried = false;

    ID3D11VertexShader* vs_sky_element = nullptr;
    ID3D11PixelShader*  ps_sky_element = nullptr;

    ID3D11VertexShader* vs_sky_stars = nullptr;
    ID3D11PixelShader*  ps_sky_stars = nullptr;
    ID3D11InputLayout*  layout_sky_element = nullptr;
    ID3D11Buffer*       cbuffer_sky_element = nullptr;
    ID3D11Buffer*       sky_element_vb = nullptr;
    ID3D11VertexShader* vs_water = nullptr;
    ID3D11PixelShader*  ps_water = nullptr;
    ID3D11Buffer*       cbuffer_water = nullptr;
    ID3D11VertexShader* vs_weather = nullptr;
    ID3D11PixelShader*  ps_weather = nullptr;
    ID3D11Buffer*       cbuffer_weather = nullptr;
    ID3D11Buffer*       cbuffer_fog = nullptr;
    ID3D11SamplerState* sampler_point = nullptr;
    ID3D11RasterizerState* rs = nullptr;
    ID3D11RasterizerState* rs_wire = nullptr;
    ID3D11BlendState* bs = nullptr;
    ID3D11BlendState* bsAlpha = nullptr;

    ID3D11BlendState* bs_water = nullptr;
    ID3D11DepthStencilState* dssWrite = nullptr;
    ID3D11DepthStencilState* dssNoWrite = nullptr;
    ID3D11DepthStencilState* dssNoWriteLEqual = nullptr;

    ID3D11DepthStencilState* dssWaterOnce = nullptr;
    ID3D11ShaderResourceView* default_srv = nullptr;

    ID3D11VertexShader* vs_fx = nullptr;
    ID3D11PixelShader*  ps_fx = nullptr;
    ID3D11InputLayout*  layout_fx = nullptr;
    ID3D11Buffer*       cbuffer_fx = nullptr;
    ID3D11Buffer*       fx_vb = nullptr;
    size_t              fx_vb_capacity = 0;
    ID3D11BlendState*   bs_fx_alpha = nullptr;
    ID3D11BlendState*   bs_fx_add = nullptr;

    std::unordered_map<int, ID3D11BlendState*> fx_blend_states;
    std::unordered_map<std::string, ID3D11ShaderResourceView*> fx_tex_srv;
    Fx::System          fx_system;
    float               fx_last_time = 0.0f;
    bool                fx_show = true;
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
    float main_light_colour[3] = {1.0f, 1.0f, 1.0f};
    float sky_time_of_day = -1.0f;
    bool has_cloud_theme = false;
    int cloud_layer_count = 0;
    float cloud_layer[4][4] = {
        {0.0f, 0.0f, 0.001f, 0.001f},
        {0.0f, 0.0f, 0.001f, 0.001f},
        {0.0f, 0.0f, 0.001f, 0.001f},
        {0.0f, 0.0f, 0.001f, 0.001f}
    };
    float cloud_shape[4][4] = {};
    float cloud_motion[4][4] = {};
    float cloud_light[4][4] = {};
    std::uint32_t cloud_density_token[4] = {};
    std::array<CloudRuntime::LayerRuntimeXex,
               CloudRuntime::kLayerCount> cloud_runtime{};
    std::array<CloudRuntime::DensityResourceBinding,
               CloudRuntime::kLayerCount> cloud_density_binding{};
    bool cloud_runtime_initialised = false;
    std::string cloud_density_tex_name[4];
    std::string sky_overlay_tex_name;
    std::string sky_sun_disc_tex_name;
    std::string sky_moon_tex_name;
    std::string sky_moon_glare_tex_name;
    std::string sky_sun_beams_tex_name;
    std::string sky_sun_glare_tex_name;
    float sky_moon_tiles[2] = {1.0f, 1.0f};

    float sky_element_params[16] = {1.0f, 1.0f, 0.0f, 1.0f,
                                    1.0f, 0.0f, 1.0f, 1.0f,
                                    1.0f, 1.0f, 1.0f, 1.0f,
                                    1.0f, 0.0f, 0.0f, 1.0f};
    bool has_moon_axis = false;
    float moon_axis[3] = {26.0f, 0.0f, 0.0f};
    int moon_phase = 4;
    bool has_day_night_cycle = false;
    float day_night_cycle_seconds = 180.0f;
    std::vector<MPSkyCloudKeyframe> day_night_keyframes;
    bool time_of_day_override = false;
    float time_of_day_override_value = 0.5f;
    float current_time_of_day = 0.5f;

    bool show_sky = true;
    bool show_weather = true;
    bool show_mist = true;

    bool has_weather_theme = false;
    float weather_precip[4] = {};
    float weather_wind[4] = {};
    float weather_mist_strength = 0.0f;
    float rain_intensity_mult = 1.0f;
    float snow_intensity_mult = 1.0f;

    bool has_fog_theme = false;
    float fog_range[4] = {};
    float fog_density[2] = {};

    bool has_sun_axis = false;
    float sun_axis[4] = {26.0f, 0.0f, 0.0f, 1.0f};

    bool wireframe = false;

    bool no_tilt = false;
    uint32_t selected_pick_id = 0;
    uint64_t selected_pick_hash = 0;
    std::unordered_map<uint32_t, LevelEdit::EditXform> range_edit_xforms;

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
bool MP_Build(ID3D11Device* dev, const std::vector<MDLMeshGeom>& geoms, const MDLInfo& info, ModelPreview& mp, bool append = false);
void MP_BuildLevelFx(ID3D11Device* dev, ModelPreview& mp);
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
ID3D11ShaderResourceView* create_srv_from_rgba_mipped(ID3D11Device* dev, int w, int h, const std::vector<uint8_t>& rgba);
#else
unsigned int create_gl_texture_from_rgba(int w, int h, const uint8_t* rgba);
#endif

#ifdef _WIN32

void MP_ComputeWorldPose(const ModelPreview& mp,
                         const std::vector<float>& deltas,
                         std::vector<float>& out_world_pose);
#endif
