#include "RenderPanel.h"
#include "../../Utilities/State.h"
#include "../ModelPreview.h"
#include "../../textures/export/TextureExport.h"
#include "../../Level/TerrainTextureRegistry.h"
#include "../../Level/EhfLodThumbnails.h"
#include "../../Level/TerrainEdit.h"
#include "../../Level/LevelLoader.h"
#include "../../animations/AnimBank.h"
#include "../../animations/AnimDataFile.h"
#include "../../animations/AnimPlayer.h"
#include "../IconButton.h"
#include "IconsFontAwesome6.h"
#include "../OutputLog.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <string>
#include <sstream>
#include <filesystem>

#ifdef _WIN32
#include <d3d11.h>
#include <windows.h>
#include <DirectXMath.h>
#else
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#endif

extern ModelPreview g_mp;
extern bool g_mp_initialized;
extern FlyCam g_flycam;

void render_panel_handle_flycam(float dt);

bool g_skel_overlay_show = false;

int g_highlight_mesh_idx    = -1;
int g_isolate_mesh_idx      = -1;
int g_selected_level_mesh_idx = -1;

#ifdef _WIN32
ID3D11ShaderResourceView* g_tex_popout_srv = nullptr;
#else
unsigned int g_tex_popout_gl = 0;
#endif
std::string g_tex_popout_name;
bool        g_tex_popout_open    = false;

int         g_tex_popout_mesh_idx = -1;

bool        g_tex_popout_show_uvs = false;

namespace {
struct TerrainEditUI {
    int   tool             = 0;
    float brush_size       = 32.f;
    float brush_strength   = 1.f;
    bool  has_changes      = false;
    bool  open_save_confirm= false;
    bool  hover_valid      = false;
    float hover_x = 0.f, hover_y = 0.f, hover_z = 0.f;
};
static TerrainEditUI g_te_ui;
}

#ifdef _WIN32
ID3D11ShaderResourceView* g_heightmap_popout_srv = nullptr;
#endif
std::string          g_heightmap_popout_name;
int                  g_heightmap_popout_w    = 0;
int                  g_heightmap_popout_h    = 0;
bool                 g_heightmap_popout_open = false;
std::vector<uint8_t> g_heightmap_popout_rgba;

std::atomic<bool>    g_pending_heightmap_view_load{false};
std::vector<uint8_t> g_pending_heightmap_view_rgba;
int                  g_pending_heightmap_view_w = 0;
int                  g_pending_heightmap_view_h = 0;
std::string          g_pending_heightmap_view_name;

namespace UI {

namespace {


void draw_placeholder() {

    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin,
                      ImVec2(origin.x + region.x, origin.y + region.y),
                      IM_COL32(20, 22, 28, 255));
    ImGui::Dummy(region);
}

void draw_lua_in_panel() {
    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin,
                      ImVec2(origin.x + region.x, origin.y + region.y),
                      IM_COL32(18, 18, 22, 255));

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.92f, 0.70f, 1.0f));
    ImGui::TextUnformatted(S.lua_preview_title.empty()
                               ? "(no script)"
                               : S.lua_preview_title.c_str());
    ImGui::PopStyleColor();

    {
        const float btn_w = ImGui::CalcTextSize("Close").x +
                            ImGui::GetStyle().FramePadding.x * 2.0f + 8.0f;
        ImGui::SameLine(ImGui::GetContentRegionAvail().x +
                        ImGui::GetCursorPosX() - btn_w);
        if (ImGui::SmallButton("Close##lua_render")) {
            S.show_lua_render = false;
        }
    }
    ImGui::Separator();

    ImGui::BeginChild("##lua_render_body", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (S.lua_preview_loading) {
        ImGui::TextDisabled("Decompiling...");
    } else if (S.lua_preview_content.empty()) {
        ImGui::TextDisabled("(empty)");
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(0.85f, 0.92f, 0.82f, 1.0f));
        ImGui::TextUnformatted(S.lua_preview_content.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
}

#ifdef _WIN32

namespace {
ID3D11SamplerState*  g_tex_point_sampler = nullptr;
ID3D11DeviceContext* g_tex_preview_ctx   = nullptr;

void ensure_point_sampler(ID3D11Device* device) {
    if (g_tex_point_sampler || !device) return;
    if (!g_tex_preview_ctx) {

        device->GetImmediateContext(&g_tex_preview_ctx);
    }
    D3D11_SAMPLER_DESC desc{};
    desc.Filter   = D3D11_FILTER_MIN_MAG_MIP_POINT;
    desc.AddressU = desc.AddressV = desc.AddressW =
        D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.MinLOD   = 0.0f;
    desc.MaxLOD   = D3D11_FLOAT32_MAX;
    device->CreateSamplerState(&desc, &g_tex_point_sampler);
}

void bind_point_sampler_cb(const ImDrawList* ,
                           const ImDrawCmd*  ) {
    if (g_tex_preview_ctx && g_tex_point_sampler) {
        g_tex_preview_ctx->PSSetSamplers(0, 1, &g_tex_point_sampler);
    }
}
}

void draw_texture_in_panel(ID3D11Device* device) {
    ensure_point_sampler(device);

    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(origin,
                      ImVec2(origin.x + region.x, origin.y + region.y),
                      IM_COL32(20, 22, 28, 255));

    if (!S.texture_window_srv || S.texture_window_width <= 0 || S.texture_window_height <= 0) {
        const char* msg = S.texture_window_name.empty()
            ? "Texture decode failed"
            : S.texture_window_name.c_str();
        ImVec2 sz = ImGui::CalcTextSize(msg);
        ImVec2 pos(origin.x + (region.x - sz.x) * 0.5f,
                   origin.y + (region.y - sz.y) * 0.5f);
        dl->AddText(pos, IM_COL32(255, 90, 90, 230), msg);
        ImGui::Dummy(region);
        return;
    }

    float tw = (float)S.texture_window_width;
    float th = (float)S.texture_window_height;
    float scale = std::min(region.x / tw, region.y / th);
    if (scale > 4.0f) scale = 4.0f;
    float dw = tw * scale;
    float dh = th * scale;
    float x0 = origin.x + (region.x - dw) * 0.5f;
    float y0 = origin.y + (region.y - dh) * 0.5f;

    if (g_tex_point_sampler) {
        dl->AddCallback(bind_point_sampler_cb, nullptr);
    }
    dl->AddImage((ImTextureID)S.texture_window_srv,
                 ImVec2(x0, y0),
                 ImVec2(x0 + dw, y0 + dh));
    if (g_tex_point_sampler) {
        dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }

    {
        ImGui::SetCursorScreenPos(ImVec2(x0, y0));
        ImGui::InvisibleButton("##tex_preview_hit", ImVec2(dw, dh));
        if (S.tex_info_ok && !S.texture_blob.empty() &&
            ImGui::BeginPopupContextItem()) {
            tex_export_menu_blob(S.texture_window_name,
                                 S.texture_blob,
                                 S.texture_mip_index);
            ImGui::EndPopup();
        }
    }

    if (S.tex_info_ok && !S.texture_blob.empty()) {
        const int total = (int)S.tex_info.Mips.size();

        if (S.texture_mip_index < 0) S.texture_mip_index = 0;
        if (S.texture_mip_index >= std::max(1, total))
            S.texture_mip_index = std::max(0, total - 1);

        int mw = 0, mh = 0;
        if (S.texture_mip_index >= 0 && S.texture_mip_index < total) {
            const auto& mm = S.tex_info.Mips[(size_t)S.texture_mip_index];
            mw = mm.HasWH ? (int)mm.MipWidth
                          : std::max(1, (int)S.tex_info.TextureWidth  >> S.texture_mip_index);
            mh = mm.HasWH ? (int)mm.MipHeight
                          : std::max(1, (int)S.tex_info.TextureHeight >> S.texture_mip_index);
        }

        const float kOverlayW = 230.0f;
        ImGui::SetNextWindowPos(ImVec2(origin.x + region.x - kOverlayW - 8.0f,
                                       origin.y + 6.0f));
        ImGui::SetNextWindowSize(ImVec2(kOverlayW, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.78f);
        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##tex_mip_selector", nullptr, fl)) {

            if (total > 1) {
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Mip");
                ImGui::SameLine();
                if (ImGui::ArrowButton("##mip_prev", ImGuiDir_Left)) {
                    if (S.texture_mip_index > 0) {
                        S.texture_mip_index--;
                        S.pending_texture_mip_change = true;
                    }
                }
                ImGui::SameLine();
                ImGui::Text("%d / %d", S.texture_mip_index, total - 1);
                ImGui::SameLine();
                if (ImGui::ArrowButton("##mip_next", ImGuiDir_Right)) {
                    if (S.texture_mip_index < total - 1) {
                        S.texture_mip_index++;
                        S.pending_texture_mip_change = true;
                    }
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(%dx%d)", mw, mh);
            } else {

                ImGui::TextDisabled("%dx%d", mw, mh);
            }

            if (ImGui::Checkbox("R", &S.tex_show_r))
                S.pending_texture_mip_change = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("G", &S.tex_show_g))
                S.pending_texture_mip_change = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("B", &S.tex_show_b))
                S.pending_texture_mip_change = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("A", &S.tex_show_a))
                S.pending_texture_mip_change = true;
        }
        ImGui::End();
    }

    ImGui::Dummy(region);
}

void apply_orbit_to_flycam() {
    float r = std::max(g_mp.radius, 0.5f) * std::max(S.cam_dist, 0.1f);
    float yaw   = S.cam_yaw;
    float pitch = S.cam_pitch;
    float cy = cosf(pitch);
    float sy = sinf(pitch);
    float cx = cosf(yaw);
    float sx = sinf(yaw);

    g_flycam.pos[0] = g_mp.center[0] + r * cy * sx;
    g_flycam.pos[1] = g_mp.center[1] + r * sy;
    g_flycam.pos[2] = g_mp.center[2] + r * cy * cx;

    g_flycam.yaw   = yaw + 3.14159265f;
    g_flycam.pitch = -pitch;
    g_flycam.is_looking = false;
}

static void project_bones_to_screen(
    const std::vector<float>& world_pose,
    uint32_t bone_count,
    const ImVec2& origin,
    const ImVec2& region,
    std::vector<ImVec2>& out_screen,
    std::vector<uint8_t>& out_visible)
{
    using namespace DirectX;

    out_screen.assign(bone_count, ImVec2(0, 0));
    out_visible.assign(bone_count, 0);
    if (bone_count == 0) return;

    float cy = cosf(g_flycam.yaw);
    float sy = sinf(g_flycam.yaw);
    float cp = cosf(g_flycam.pitch);
    float sp = sinf(g_flycam.pitch);
    XMVECTOR eye = XMVectorSet(g_flycam.pos[0], g_flycam.pos[1], g_flycam.pos[2], 1);
    XMVECTOR at  = XMVectorSet(g_flycam.pos[0] + sy * cp,
                               g_flycam.pos[1] + sp,
                               g_flycam.pos[2] + cy * cp, 1);
    XMVECTOR up  = XMVectorSet(0, 1, 0, 0);
    XMMATRIX V = XMMatrixLookAtLH(eye, at, up);

    float fov       = XMConvertToRadians(60.0f);
    float aspect    = region.x / std::max(1.0f, region.y);
    float far_plane = std::max(g_mp.radius * 100.0f, 100.0f);
    XMMATRIX P = XMMatrixPerspectiveFovLH(fov, aspect, 0.05f, far_plane);

    const float tiltX = -XM_PIDIV2;
    XMMATRIX Tm = XMMatrixTranslation(-g_mp.center[0], -g_mp.center[1], -g_mp.center[2]);
    XMMATRIX Rx = XMMatrixRotationX(tiltX);
    XMMATRIX Tp = XMMatrixTranslation( g_mp.center[0],  g_mp.center[1],  g_mp.center[2]);
    XMMATRIX W  = Tm * Rx * Tp;
    XMMATRIX WVP = W * V * P;

    for (uint32_t i = 0; i < bone_count; ++i) {
        XMFLOAT4X4 wf;
        std::memcpy(&wf, &world_pose[(size_t)i * 16], sizeof(float) * 16);
        XMMATRIX Wp = XMLoadFloat4x4(&wf);
        XMVECTOR pos = XMVector3Transform(XMVectorSet(0, 0, 0, 1), Wp);
        XMVECTOR clip = XMVector4Transform(XMVectorSetW(pos, 1.0f), WVP);
        float w = XMVectorGetW(clip);
        if (w <= 0.05f) continue;
        float ndcx = XMVectorGetX(clip) / w;
        float ndcy = XMVectorGetY(clip) / w;
        if (ndcx < -1.5f || ndcx > 1.5f) continue;
        if (ndcy < -1.5f || ndcy > 1.5f) continue;
        out_screen[i].x = origin.x + (ndcx * 0.5f + 0.5f) * region.x;
        out_screen[i].y = origin.y + (1.0f - (ndcy * 0.5f + 0.5f)) * region.y;
        out_visible[i] = 1;
    }
}

#ifdef _WIN32
static void draw_gdb_placements_overlay(const ImVec2& origin,
                                        const ImVec2& region)
{
    using namespace DirectX;
    if (g_level_gdb_placements.empty()) return;
    if (!S.show_gdb_placements) return;

    float cy = cosf(g_flycam.yaw);
    float sy = sinf(g_flycam.yaw);
    float cp = cosf(g_flycam.pitch);
    float sp = sinf(g_flycam.pitch);
    XMVECTOR eye = XMVectorSet(g_flycam.pos[0], g_flycam.pos[1], g_flycam.pos[2], 1);
    XMVECTOR at  = XMVectorSet(g_flycam.pos[0] + sy * cp,
                               g_flycam.pos[1] + sp,
                               g_flycam.pos[2] + cy * cp, 1);
    XMVECTOR up  = XMVectorSet(0, 1, 0, 0);
    XMMATRIX V = XMMatrixLookAtLH(eye, at, up);
    float fov = XMConvertToRadians(60.0f);
    float aspect = region.x / std::max(1.0f, region.y);
    float far_plane = std::max(g_mp.radius * 100.0f, 1000.0f);
    XMMATRIX P = XMMatrixPerspectiveFovLH(fov, aspect, 0.05f, far_plane);
    XMMATRIX VP = V * P;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col_fixed = IM_COL32(255, 80, 80, 230);
    const ImU32 col_var   = IM_COL32(120, 220, 255, 180);

    const int   gw = g_pending_terrain_ghf_width;
    const int   gh = g_pending_terrain_ghf_height;
    const float tile = g_pending_terrain_ghf_tile_size > 0.0f
                         ? g_pending_terrain_ghf_tile_size : 0.5f;
    const auto& heights = g_pending_terrain_ghf_heights;
    const bool  have_terrain = (gw > 0 && gh > 0 &&
                                heights.size() == size_t(gw) * size_t(gh));
    auto sample_height = [&](float wx, float wy) -> float {
        if (!have_terrain) return 0.0f;
        const float gx = wx / tile;
        const float gy = wy / tile;
        int ix = int(gx); int iy = int(gy);
        if (ix < 0) ix = 0; else if (ix >= gw) ix = gw - 1;
        if (iy < 0) iy = 0; else if (iy >= gh) iy = gh - 1;
        return heights[size_t(iy) * size_t(gw) + size_t(ix)];
    };

    size_t drawn = 0;
    for (const auto& gp : g_level_gdb_placements) {
        const float rx = gp.x;
        const float ry = sample_height(gp.x, gp.y) + 1.0f;
        const float rz = gp.y;
        XMVECTOR clip = XMVector4Transform(XMVectorSet(rx, ry, rz, 1.0f), VP);
        float w = XMVectorGetW(clip);
        if (w <= 0.05f) continue;
        float ndcx = XMVectorGetX(clip) / w;
        float ndcy = XMVectorGetY(clip) / w;
        if (ndcx < -1.2f || ndcx > 1.2f) continue;
        if (ndcy < -1.2f || ndcy > 1.2f) continue;
        ImVec2 sp_screen;
        sp_screen.x = origin.x + (ndcx * 0.5f + 0.5f) * region.x;
        sp_screen.y = origin.y + (1.0f - (ndcy * 0.5f + 0.5f)) * region.y;

        const bool fixed = (gp.marker == 0x00004B40);
        const ImU32 col  = fixed ? col_fixed : col_var;
        const float r    = fixed ? 4.0f : 2.5f;
        dl->AddCircleFilled(sp_screen, r, col);
        if (fixed) {
            dl->AddCircle(sp_screen, r + 1.0f, IM_COL32(0, 0, 0, 200), 12, 1.0f);
        }
        ++drawn;
    }

    char buf[96];
    std::snprintf(buf, sizeof(buf),
                  "gdb placements: %zu shown / %zu total",
                  drawn, g_level_gdb_placements.size());
    dl->AddText(ImVec2(origin.x + 14, origin.y + region.y - 22),
                IM_COL32(220, 220, 220, 200), buf);
}
#endif

void draw_skeleton_overlay(const ImVec2& origin, const ImVec2& region) {
    if (g_mp.bone_count == 0) return;

    std::vector<float> world_pose;
    MP_ComputeWorldPose(g_mp, S.bone_rot_deltas, world_pose);

    std::vector<ImVec2>  screen;
    std::vector<uint8_t> visible;
    project_bones_to_screen(world_pose, g_mp.bone_count, origin, region,
                            screen, visible);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 line_col   = IM_COL32(255, 220,  80, 220);
    const ImU32 dot_col    = IM_COL32(255, 240, 120, 255);
    const ImU32 sel_line   = IM_COL32(120, 220, 255, 255);
    const ImU32 sel_dot    = IM_COL32( 90, 240, 255, 255);

    const uint32_t n = g_mp.bone_count;

    for (uint32_t i = 0; i < n; ++i) {
        if (!visible[i]) continue;
        int pid = (i < g_mp.bone_parents.size()) ? g_mp.bone_parents[i] : -1;
        if (pid < 0 || pid >= (int)n) continue;
        if (!visible[(uint32_t)pid]) continue;
        bool sel = (S.selected_bone == (int)i || S.selected_bone == pid);
        dl->AddLine(screen[(uint32_t)pid], screen[i],
                    sel ? sel_line : line_col,
                    sel ? 2.0f : 1.5f);
    }

    for (uint32_t i = 0; i < n; ++i) {
        if (!visible[i]) continue;
        if ((int)i == S.selected_bone) {
            dl->AddCircleFilled(screen[i], 5.0f, sel_dot);
            dl->AddCircle      (screen[i], 7.5f, IM_COL32(0, 0, 0, 220), 0, 2.0f);
        } else {
            dl->AddCircleFilled(screen[i], 2.5f, dot_col);
        }
    }

    if (S.selected_bone >= 0 && S.selected_bone < (int)S.mdl_info.Bones.size()) {
        const std::string& bn = S.mdl_info.Bones[(size_t)S.selected_bone].Name;
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s  [%s]",
                      S.bone_rotate_mode ? "ROTATE" : "selected",
                      bn.c_str());
        ImVec2 ts = ImGui::CalcTextSize(buf);
        ImVec2 tp(origin.x + region.x - ts.x - 12.0f, origin.y + 8.0f);
        dl->AddRectFilled(ImVec2(tp.x - 6, tp.y - 4),
                          ImVec2(tp.x + ts.x + 6, tp.y + ts.y + 4),
                          IM_COL32(20, 22, 28, 200), 4.0f);
        dl->AddText(tp,
                    S.bone_rotate_mode ? IM_COL32(120, 220, 255, 255)
                                       : IM_COL32(220, 230, 240, 240),
                    buf);
    }
}

static int pick_bone_at(const ImVec2& mouse, const ImVec2& origin,
                        const ImVec2& region, float radius_px) {
    if (g_mp.bone_count == 0) return -1;
    std::vector<float> world_pose;
    MP_ComputeWorldPose(g_mp, S.bone_rot_deltas, world_pose);
    std::vector<ImVec2>  screen;
    std::vector<uint8_t> visible;
    project_bones_to_screen(world_pose, g_mp.bone_count, origin, region,
                            screen, visible);

    int   best = -1;
    float best_d2 = radius_px * radius_px;
    for (uint32_t i = 0; i < g_mp.bone_count; ++i) {
        if (!visible[i]) continue;
        float dx = screen[i].x - mouse.x;
        float dy = screen[i].y - mouse.y;
        float d2 = dx * dx + dy * dy;
        if (d2 < best_d2) { best_d2 = d2; best = (int)i; }
    }
    return best;
}

int pick_level_mesh_at(const ImVec2& mouse,
                       const ImVec2& origin,
                       const ImVec2& region) {
    if (!g_mp.has_model || !g_mp.no_tilt || g_mp.meshes.empty()) return -1;
    const float fw = std::max(1.0f, region.x);
    const float fh = std::max(1.0f, region.y);

    const float mx = mouse.x - origin.x;
    const float my = mouse.y - origin.y;
    if (mx < 0.0f || my < 0.0f || mx > fw || my > fh) return -1;

    const float fov         = 60.0f * 3.14159265f / 180.0f;
    const float aspect      = fw / fh;
    const float tan_half    = std::tan(0.5f * fov);
    const float u_view      = (2.0f * mx / fw - 1.0f) * aspect * tan_half;
    const float v_view      = (1.0f - 2.0f * my / fh) * tan_half;

    const float cy = std::cos(g_flycam.yaw);
    const float sy = std::sin(g_flycam.yaw);
    const float cp = std::cos(g_flycam.pitch);
    const float sp = std::sin(g_flycam.pitch);

    const float fx = sy * cp, fy = sp, fz = cy * cp;
    const float rx = cy,      ry = 0.0f, rz = -sy;
    const float ux = -sp * sy, uy = cp, uz = -sp * cy;

    float dx = rx * u_view + ux * v_view + fx;
    float dy = ry * u_view + uy * v_view + fy;
    float dz = rz * u_view + uz * v_view + fz;
    const float dlen = std::sqrt(dx*dx + dy*dy + dz*dz);
    if (dlen <= 1e-6f) return -1;
    dx /= dlen; dy /= dlen; dz /= dlen;

    const float ox = g_flycam.pos[0];
    const float oy = g_flycam.pos[1];
    const float oz = g_flycam.pos[2];

    int   best     = -1;
    float best_t   = std::numeric_limits<float>::infinity();
    for (size_t i = 0; i < g_mp.meshes.size(); ++i) {
        const auto& m = g_mp.meshes[i];
        if (m.index_count == 0 || m.radius <= 0.0f) continue;
        if (g_mp.selected_lod >= 0 &&
            m.lod_index != (uint32_t)g_mp.selected_lod) continue;
        if (m.name.rfind("engine_level:", 0) == 0) continue;

        const float lx = ox - m.center[0];
        const float ly = oy - m.center[1];
        const float lz = oz - m.center[2];
        const float l_dot_d = lx*dx + ly*dy + lz*dz;
        const float l_len2  = lx*lx + ly*ly + lz*lz;
        const float r2      = m.radius * m.radius;
        const float c       = l_len2 - r2;
        const float disc    = l_dot_d * l_dot_d - c;
        if (disc < 0.0f) continue;
        const float sq = std::sqrt(disc);
        float t = -l_dot_d - sq;
        if (t < 0.0f) t = -l_dot_d + sq;
        if (t < 0.0f) continue;
        if (t < best_t) { best_t = t; best = (int)i; }
    }
    return best;
}

void draw_model_in_panel(ID3D11Device* device) {
    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    int w = std::max(1, (int)region.x);
    int h = std::max(1, (int)region.y);

    if (::g_selected_level_mesh_idx >= (int)g_mp.meshes.size() ||
        !g_mp.has_model || !g_mp.no_tilt) {
        ::g_selected_level_mesh_idx = -1;
    }

    if (!g_mp_initialized) {
        MP_Init(device, g_mp, w, h);
        g_mp_initialized = true;
    }
    MP_Resize(device, g_mp, w, h);

    ImGui::InvisibleButton("##model_render", region);
    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();

    bool skel_visible = ::g_skel_overlay_show && (g_mp.bone_count > 0);

    static float s_rot_snapshot[4]    = {0, 0, 0, 1};
    static int   s_rot_snapshot_bone  = -1;
    static bool  s_rot_snapshot_valid = false;

    auto cancel_rotate = [&]() {
        if (s_rot_snapshot_valid &&
            s_rot_snapshot_bone >= 0 &&
            s_rot_snapshot_bone < (int)g_mp.bone_count &&
            (size_t)s_rot_snapshot_bone * 4 + 4 <= S.bone_rot_deltas.size()) {
            for (int k = 0; k < 4; ++k) {
                S.bone_rot_deltas[(size_t)s_rot_snapshot_bone * 4 + (size_t)k]
                    = s_rot_snapshot[k];
            }
        }
        s_rot_snapshot_valid = false;
        S.bone_rotate_mode   = false;
    };
    auto confirm_rotate = [&]() {
        s_rot_snapshot_valid = false;
        S.bone_rotate_mode   = false;
    };

    bool rotate_active = (skel_visible && S.bone_rotate_mode &&
                          S.selected_bone >= 0 &&
                          S.selected_bone < (int)g_mp.bone_count);

    if (rotate_active) {

        if (hovered) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            if (d.x != 0.0f || d.y != 0.0f) {
                const float kRotSensitivity = 0.01f;
                float a_y = d.x * kRotSensitivity;
                float a_x = d.y * kRotSensitivity;

                using namespace DirectX;
                XMVECTOR qx = XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), a_x);
                XMVECTOR qy = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), a_y);
                XMVECTOR delta = XMQuaternionMultiply(qx, qy);

                int b = S.selected_bone;
                XMVECTOR cur = XMVectorSet(
                    S.bone_rot_deltas[(size_t)b * 4 + 0],
                    S.bone_rot_deltas[(size_t)b * 4 + 1],
                    S.bone_rot_deltas[(size_t)b * 4 + 2],
                    S.bone_rot_deltas[(size_t)b * 4 + 3]);

                XMVECTOR nxt = XMQuaternionNormalize(XMQuaternionMultiply(cur, delta));
                XMFLOAT4 nf;
                XMStoreFloat4(&nf, nxt);
                S.bone_rot_deltas[(size_t)b * 4 + 0] = nf.x;
                S.bone_rot_deltas[(size_t)b * 4 + 1] = nf.y;
                S.bone_rot_deltas[(size_t)b * 4 + 2] = nf.z;
                S.bone_rot_deltas[(size_t)b * 4 + 3] = nf.w;
            }
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            cancel_rotate();
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            confirm_rotate();
        }
    }

    if (skel_visible && !rotate_active && hovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImVec2 mp = ImGui::GetIO().MousePos;
        int picked = pick_bone_at(mp, origin, region, 12.0f);
        S.selected_bone = picked;
    }

    if (g_mp.no_tilt && hovered && !rotate_active &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f))
    {
        const int picked = pick_level_mesh_at(ImGui::GetIO().MousePos, origin, region);
        ::g_selected_level_mesh_idx = picked;
    }

    if (S.terrain_mode) {
        const float dt = ImGui::GetIO().DeltaTime;
        if (hovered || g_flycam.is_looking) {
            ::render_panel_handle_flycam(dt);
        }
    } else {
        if (!rotate_active && active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            const float kOrbitSensitivity = 0.008f;
            S.cam_yaw   += d.x * kOrbitSensitivity;
            S.cam_pitch += d.y * kOrbitSensitivity;

            const float kPitchLimit = 1.5f;
            if (S.cam_pitch >  kPitchLimit) S.cam_pitch =  kPitchLimit;
            if (S.cam_pitch < -kPitchLimit) S.cam_pitch = -kPitchLimit;
        }

        if (hovered) {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {
                S.cam_dist *= (wheel > 0.0f) ? 0.9f : 1.111f;
                if (S.cam_dist < 0.3f)  S.cam_dist = 0.3f;
                if (S.cam_dist > 50.0f) S.cam_dist = 50.0f;
            }
        }
    }

    if (skel_visible && hovered && ImGui::IsKeyPressed(S.key_rotate_mode)) {
        if (S.selected_bone >= 0 && S.selected_bone < (int)g_mp.bone_count &&
            (size_t)S.selected_bone * 4 + 4 <= S.bone_rot_deltas.size()) {
            if (!S.bone_rotate_mode) {
                int b = S.selected_bone;
                for (int k = 0; k < 4; ++k) {
                    s_rot_snapshot[k] =
                        S.bone_rot_deltas[(size_t)b * 4 + (size_t)k];
                }
                s_rot_snapshot_bone  = b;
                s_rot_snapshot_valid = true;
                S.bone_rotate_mode   = true;
            } else {
                confirm_rotate();
            }
        }
    }

    for (size_t i = 0; i < g_mp.meshes.size(); ++i) {
        g_mp.meshes[i].highlight = ((int)i == ::g_highlight_mesh_idx)
                                || ((int)i == ::g_selected_level_mesh_idx);
        g_mp.meshes[i].isolated  = ((int)i == ::g_isolate_mesh_idx);
    }

    if (!S.terrain_mode) apply_orbit_to_flycam();
    MP_Render(device, g_mp, g_flycam);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (g_mp.srv) {
        dl->AddImage((ImTextureID)g_mp.srv,
                     origin,
                     ImVec2(origin.x + region.x, origin.y + region.y));
    }

#ifdef _WIN32
    if (S.terrain_mode) {
        draw_gdb_placements_overlay(origin, region);
    }
#endif

    if (hovered && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (S.bone_rotate_mode) {
            cancel_rotate();
        } else if (g_mp.no_tilt && ::g_selected_level_mesh_idx >= 0) {
            ::g_selected_level_mesh_idx = -1;
        } else if (g_mp.no_tilt) {
            // In level/terrain mode, ESC with no selection is a no-op
            // — we don't want to nuke the loaded level on a stray ESC.
        } else {
            MP_Release(g_mp);
            g_mp.has_model = false;
            g_mp_initialized = false;
            S.show_model_preview = false;
            S.model_preview_open = false;
            S.selected_bone = -1;
        }
    }

    dl->AddRectFilled(ImVec2(origin.x + 6, origin.y + 6),
                      ImVec2(origin.x + 196, origin.y + 70),
                      IM_COL32(20, 22, 28, 200), 4.0f);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 12));
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Controls");
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 30));
    ImGui::TextDisabled("L-Drag  rotate");
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 46));
    ImGui::TextDisabled("Wheel  zoom  /  ESC  close");

    float next_overlay_y = origin.y + 76.0f;

    bool has_skeleton = g_mp.has_model && g_mp.bone_count > 0;
    if (has_skeleton) {

        static float s_skel_alpha    = 0.30f;

        const float kIdleAlpha   = 0.30f;
        const float kHoverAlpha  = 1.00f;

        const ImVec2 win_pos (origin.x + 6, origin.y + 76);
        const ImVec2 win_size(190, 0);
        ImGui::SetNextWindowPos(win_pos);
        ImGui::SetNextWindowSize(win_size, ImGuiCond_Always);

        ImGui::SetNextWindowBgAlpha(s_skel_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_skel_alpha);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##skeleton_overlay", nullptr, fl)) {

            bool hovering = ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                ImGuiHoveredFlags_ChildWindows);
            float target = hovering ? kHoverAlpha : kIdleAlpha;

            s_skel_alpha += (target - s_skel_alpha) * 0.18f;
            if (std::fabs(s_skel_alpha - target) < 0.005f) s_skel_alpha = target;

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Skeleton");
            ImGui::Checkbox("Show", &::g_skel_overlay_show);
            if (S.selected_bone >= 0 && S.selected_bone < (int)S.mdl_info.Bones.size()) {

                ImGui::TextDisabled(S.bone_rotate_mode
                                        ? "RMB cancel  /  LMB confirm"
                                        : "R: rotate selected");
            }

            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            next_overlay_y = wp.y + ws.y + 6.0f;
        }
        ImGui::End();
        ImGui::PopStyleVar();

        if (!::g_skel_overlay_show) {
            S.selected_bone     = -1;
            S.bone_rotate_mode  = false;
        }

        if (::g_skel_overlay_show) {
            draw_skeleton_overlay(origin, region);
        }
    } else {

        ::g_skel_overlay_show = false;
        S.selected_bone       = -1;
        S.bone_rotate_mode    = false;
    }

    if (g_mp.has_model) {
        static float s_wire_alpha = 0.30f;
        const float kIdleAlpha   = 0.30f;
        const float kHoverAlpha  = 1.00f;

        const ImVec2 wire_pos (origin.x + 6, next_overlay_y);
        const ImVec2 wire_size(190, 0);
        ImGui::SetNextWindowPos(wire_pos);
        ImGui::SetNextWindowSize(wire_size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(s_wire_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_wire_alpha);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##wireframe_overlay", nullptr, fl)) {
            bool hovering = ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                ImGuiHoveredFlags_ChildWindows);
            float target = hovering ? kHoverAlpha : kIdleAlpha;
            s_wire_alpha += (target - s_wire_alpha) * 0.18f;
            if (std::fabs(s_wire_alpha - target) < 0.005f) s_wire_alpha = target;

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Wireframe");
            ImGui::Checkbox("Show", &g_mp.wireframe);

            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            next_overlay_y = wp.y + ws.y + 6.0f;
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    if (g_mp.has_model && g_mp.lod_count > 1) {
        static float s_lod_alpha = 0.30f;
        const float kIdleAlpha   = 0.30f;
        const float kHoverAlpha  = 1.00f;

        const ImVec2 lod_pos (origin.x + 6, next_overlay_y);
        const ImVec2 lod_size(190, 0);
        ImGui::SetNextWindowPos(lod_pos);
        ImGui::SetNextWindowSize(lod_size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(s_lod_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_lod_alpha);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##lod_overlay", nullptr, fl)) {
            bool hovering = ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                ImGuiHoveredFlags_ChildWindows);
            float target = hovering ? kHoverAlpha : kIdleAlpha;
            s_lod_alpha += (target - s_lod_alpha) * 0.18f;
            if (std::fabs(s_lod_alpha - target) < 0.005f) s_lod_alpha = target;

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "LOD");

            const int lod_count = (int)g_mp.lod_count;
            int current = g_mp.selected_lod;
            if (current < -1 || current >= lod_count) current = 0;

            if (ImGui::RadioButton("All", current == -1)) {
                g_mp.selected_lod = -1;
            }
            for (int i = 0; i < lod_count; ++i) {
                ImGui::SameLine();
                char lbl[16];
                std::snprintf(lbl, sizeof(lbl), "%d", i);
                if (ImGui::RadioButton(lbl, current == i)) {
                    g_mp.selected_lod = i;
                }
            }

            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            next_overlay_y = wp.y + ws.y + 6.0f;
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    if (g_mp.has_model && !g_mp.meshes.empty()) {
        static float s_mat_alpha = 0.30f;
        const float kIdleAlpha   = 0.30f;
        const float kHoverAlpha  = 1.00f;

        const float kMatW = 296.0f;
        float max_h = std::max(160.0f,
                               region.y - (next_overlay_y - origin.y) - 20.0f);

        ImGui::SetNextWindowPos(ImVec2(origin.x + 6, next_overlay_y));
        ImGui::SetNextWindowSizeConstraints(ImVec2(kMatW, 0.0f),
                                            ImVec2(kMatW, max_h));
        ImGui::SetNextWindowBgAlpha(s_mat_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_mat_alpha);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##materials_overlay", nullptr, fl)) {
            bool hovering = ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                ImGuiHoveredFlags_ChildWindows);
            float target = hovering ? kHoverAlpha : kIdleAlpha;
            s_mat_alpha += (target - s_mat_alpha) * 0.18f;
            if (std::fabs(s_mat_alpha - target) < 0.005f) s_mat_alpha = target;

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Materials");
            ImGui::Separator();

            const ImVec2 thumb_size(48, 48);

            if (!g_mp.no_tilt) for (size_t mi = 0; mi < g_mp.meshes.size(); ++mi) {
                auto& mesh = g_mp.meshes[mi];

                if (g_mp.selected_lod >= 0 &&
                    mesh.lod_index != (uint32_t)g_mp.selected_lod) {
                    continue;
                }

                ImGui::PushID((int)mi);

                ImGui::TextUnformatted(mesh.name.c_str());

                bool h   = (::g_highlight_mesh_idx == (int)mi);
                bool iso = (::g_isolate_mesh_idx   == (int)mi);

                if (ImGui::Checkbox("Highlight", &h)) {
                    if (h) {
                        ::g_highlight_mesh_idx = (int)mi;
                        ::g_isolate_mesh_idx   = -1;
                    } else if (::g_highlight_mesh_idx == (int)mi) {
                        ::g_highlight_mesh_idx = -1;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Isolate", &iso)) {
                    if (iso) {
                        ::g_isolate_mesh_idx   = (int)mi;
                        ::g_highlight_mesh_idx = -1;
                    } else if (::g_isolate_mesh_idx == (int)mi) {
                        ::g_isolate_mesh_idx = -1;
                    }
                }

                struct ThumbSpec {
                    const char*               slot_id;
                    ID3D11ShaderResourceView* srv;
                    const std::string*        name;
                    bool*                     visible;
                };
                ThumbSpec thumbs[5] = {
                    {"diffuse",  mesh.srv_diffuse,  &mesh.diffuse_tex_name,  &mesh.diffuse_visible},
                    {"normal",   mesh.srv_normal,   &mesh.normal_tex_name,   &mesh.normal_visible},
                    {"specular", mesh.srv_specular, &mesh.specular_tex_name, &mesh.specular_visible},
                    {"metallic", mesh.srv_metallic, &mesh.metallic_tex_name, &mesh.metallic_visible},
                    {"extra",    mesh.srv_extra,    &mesh.extra_tex_name,    &mesh.extra_visible},
                };
                bool any_thumb = false;
                for (int ti = 0; ti < 5; ++ti) {
                    const ThumbSpec& t = thumbs[ti];
                    if (!t.srv || t.srv == g_mp.default_srv) continue;
                    if (t.name->empty()) continue;
                    if (any_thumb) ImGui::SameLine();
                    any_thumb = true;
                    ImGui::PushID(t.slot_id);

                    ImGui::BeginGroup();

                    ImVec4 tint = (*t.visible) ? ImVec4(1, 1, 1, 1)
                                               : ImVec4(0.45f, 0.45f, 0.45f, 1);
                    if (ImGui::ImageButton("##t",
                                           (ImTextureID)t.srv,
                                           thumb_size,
                                           ImVec2(0, 0), ImVec2(1, 1),
                                           ImVec4(0, 0, 0, 0), tint)) {
                        ::g_tex_popout_srv      = t.srv;
                        ::g_tex_popout_name     = *t.name;
                        ::g_tex_popout_open     = true;

                        ::g_tex_popout_mesh_idx = (int)mi;
                    }

                    if (ImGui::BeginPopupContextItem()) {
                        const auto* terrain_tex =
                            TerrainTextureRegistry::Find(*t.name);
                        if (terrain_tex) {
                            tex_export_menu_rgba(*t.name,
                                                 terrain_tex->rgba,
                                                 terrain_tex->width,
                                                 terrain_tex->height);
                        } else {
                            const std::string& preferred_bnk =
                                (S.selected_nested_index != -1 &&
                                 !S.selected_nested_temp_path.empty())
                                    ? S.selected_nested_temp_path
                                    : S.selected_bnk;
                            tex_export_menu_named(*t.name, *t.name,
                                                  preferred_bnk, 0);
                        }
                        ImGui::EndPopup();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s\n[%s]",
                                          t.name->c_str(), t.slot_id);
                    }

                    ImGui::Checkbox("##vis", t.visible);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Show %s in render", t.slot_id);
                    }
                    ImGui::EndGroup();
                    ImGui::PopID();
                }
                if (!any_thumb) {
                    ImGui::TextDisabled("(no textures)");
                }

                ImGui::Separator();
                ImGui::PopID();
            }

            if (g_mp.no_tilt && ::g_selected_level_mesh_idx >= 0 &&
                ::g_selected_level_mesh_idx < (int)g_mp.meshes.size())
            {
                const int mi   = ::g_selected_level_mesh_idx;
                auto&     mesh = g_mp.meshes[mi];

                ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f),
                                   "Selected mesh");
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear##sel_clear")) {
                    ::g_selected_level_mesh_idx = -1;
                }
                ImGui::TextWrapped("%s",
                    mesh.name.empty() ? "(unnamed)" : mesh.name.c_str());
                ImGui::Separator();

                ImGui::PushID(0x20000 + mi);

                struct ThumbSpec {
                    const char*               slot_id;
                    ID3D11ShaderResourceView* srv;
                    const std::string*        name;
                    bool*                     visible;
                };
                ThumbSpec thumbs[5] = {
                    {"diffuse",  mesh.srv_diffuse,  &mesh.diffuse_tex_name,  &mesh.diffuse_visible},
                    {"normal",   mesh.srv_normal,   &mesh.normal_tex_name,   &mesh.normal_visible},
                    {"specular", mesh.srv_specular, &mesh.specular_tex_name, &mesh.specular_visible},
                    {"metallic", mesh.srv_metallic, &mesh.metallic_tex_name, &mesh.metallic_visible},
                    {"extra",    mesh.srv_extra,    &mesh.extra_tex_name,    &mesh.extra_visible},
                };
                bool any_thumb = false;
                for (int ti = 0; ti < 5; ++ti) {
                    const ThumbSpec& t = thumbs[ti];
                    if (!t.srv || t.srv == g_mp.default_srv) continue;
                    if (t.name->empty()) continue;
                    if (any_thumb) ImGui::SameLine();
                    any_thumb = true;
                    ImGui::PushID(t.slot_id);
                    ImGui::BeginGroup();
                    ImVec4 tint = (*t.visible) ? ImVec4(1, 1, 1, 1)
                                               : ImVec4(0.45f, 0.45f, 0.45f, 1);
                    if (ImGui::ImageButton("##t",
                                           (ImTextureID)t.srv,
                                           thumb_size,
                                           ImVec2(0, 0), ImVec2(1, 1),
                                           ImVec4(0, 0, 0, 0), tint)) {
                        ::g_tex_popout_srv      = t.srv;
                        ::g_tex_popout_name     = *t.name;
                        ::g_tex_popout_open     = true;
                        ::g_tex_popout_mesh_idx = mi;
                    }
                    if (ImGui::BeginPopupContextItem()) {
                        const auto* terrain_tex =
                            TerrainTextureRegistry::Find(*t.name);
                        if (terrain_tex) {
                            tex_export_menu_rgba(*t.name,
                                                 terrain_tex->rgba,
                                                 terrain_tex->width,
                                                 terrain_tex->height);
                        } else {
                            const std::string& preferred_bnk =
                                (S.selected_nested_index != -1 &&
                                 !S.selected_nested_temp_path.empty())
                                    ? S.selected_nested_temp_path
                                    : S.selected_bnk;
                            tex_export_menu_named(*t.name, *t.name,
                                                  preferred_bnk, 0);
                        }
                        ImGui::EndPopup();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s\n[%s]",
                                          t.name->c_str(), t.slot_id);
                    }
                    ImGui::Checkbox("##vis", t.visible);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Show %s in render", t.slot_id);
                    }
                    ImGui::EndGroup();
                    ImGui::PopID();
                }
                if (!any_thumb) {
                    ImGui::TextDisabled("(no textures)");
                }
                ImGui::Separator();
                ImGui::PopID();
            } else if (g_mp.no_tilt) {
                const auto& lod = EhfLodThumbnails::Get();
                if (!lod.empty()) {
                    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f),
                        ".ehf LOD palette");
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%zu materials)", lod.size());
                    ImGui::Separator();

                    auto basename = [](const std::string& s) -> std::string {
                        if (s.empty()) return {};
                        size_t pos = s.find_last_of("/\\");
                        return (pos == std::string::npos)
                            ? s : s.substr(pos + 1);
                    };

                    auto thumb_or_placeholder =
                        [&](ID3D11ShaderResourceView* srv,
                            const std::string& path,
                            const char* slot_tag,
                            int slot_idx)
                    {
                        const ImVec2 sz(48, 48);
                        ImGui::PushID(slot_idx);
                        if (srv) {
                            if (ImGui::ImageButton("##t",
                                (ImTextureID)srv, sz,
                                ImVec2(0, 0), ImVec2(1, 1),
                                ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, 1)))
                            {
                                ::g_tex_popout_srv      = srv;
                                ::g_tex_popout_name     = path;
                                ::g_tex_popout_open     = true;
                                ::g_tex_popout_mesh_idx = -1;
                            }
                            if (ImGui::BeginPopupContextItem()) {
                                const std::string& preferred_bnk =
                                    (S.selected_nested_index != -1 &&
                                     !S.selected_nested_temp_path.empty())
                                        ? S.selected_nested_temp_path
                                        : S.selected_bnk;
                                tex_export_menu_named(path, path,
                                                      preferred_bnk, 0);
                                ImGui::EndPopup();
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s\n[%s]",
                                                  path.c_str(), slot_tag);
                            }
                        } else {
                            ImGui::Dummy(sz);
                            if (!path.empty() && ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("decode failed: %s\n[%s]",
                                                  path.c_str(), slot_tag);
                            }
                        }
                        ImGui::PopID();
                    };

                    for (size_t i = 0; i < lod.size(); ++i) {
                        const auto& e = lod[i];
                        ImGui::PushID(int(0x10000 + i));

                        const std::string title = "[" + std::to_string(i)
                            + "] " + basename(e.base_diffuse_path);
                        ImGui::TextUnformatted(title.c_str());

                        thumb_or_placeholder(e.srv_base_diffuse,
                                             e.base_diffuse_path,
                                             "base diffuse", 0);
                        ImGui::SameLine();
                        thumb_or_placeholder(e.srv_base_normal,
                                             e.base_normal_path,
                                             "base normal", 1);
                        ImGui::SameLine();
                        thumb_or_placeholder(e.srv_detail_diffuse,
                                             e.detail_diffuse_path,
                                             "detail diffuse", 2);
                        ImGui::SameLine();
                        thumb_or_placeholder(e.srv_detail_normal,
                                             e.detail_normal_path,
                                             "detail normal", 3);

                        ImGui::Separator();
                        ImGui::PopID();
                    }
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    } else {

        ::g_highlight_mesh_idx       = -1;
        ::g_isolate_mesh_idx         = -1;
        ::g_selected_level_mesh_idx  = -1;
        ::g_tex_popout_open          = false;
        ::g_tex_popout_srv           = nullptr;
        ::g_tex_popout_name.clear();
        ::g_tex_popout_mesh_idx      = -1;
    }

    if (S.dev_mode && g_mp.has_model && g_mp.no_tilt) {
        enum TerrainTool {
            TT_NONE = 0,
            TT_RAISE,
            TT_LOWER,
            TT_SMOOTH,
            TT_FLATTEN,
        };
        int&   s_tool          = g_te_ui.tool;
        float& s_brush_size    = g_te_ui.brush_size;
        float& s_brush_strength= g_te_ui.brush_strength;
        bool&  s_has_changes   = g_te_ui.has_changes;
        bool&  s_open_save_confirm = g_te_ui.open_save_confirm;

        auto upload_after_edit = [&]() {
            if (!g_mp.meshes.empty()) {
                TerrainEdit::ApplyToGpu(device, &g_mp.meshes[0]);
            }
            s_has_changes = TerrainEdit::IsDirty();
        };

        const float kEditW    = 300.0f;
        const float kEditPad  = 6.0f;
        const ImVec2 edit_pos(origin.x + region.x - kEditW - kEditPad,
                              origin.y + kEditPad);

        ImGui::SetNextWindowPos(edit_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints(ImVec2(kEditW, 0.0f),
                                            ImVec2(kEditW, region.y - 2*kEditPad));
        ImGui::SetNextWindowBgAlpha(0.88f);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_AlwaysAutoResize;

        if (ImGui::Begin((std::string(ICON_FA_MOUNTAIN) +
                          "  Edit Terrain###edit_terrain").c_str(),
                         nullptr, fl))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f),
                "%s  Save", ICON_FA_FLOPPY_DISK);
            ImGui::Separator();
            if (s_has_changes) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                    "%s Unsaved changes", ICON_FA_TRIANGLE_EXCLAMATION);
            } else {
                ImGui::TextDisabled("(no pending changes)");
            }
            ImGui::BeginDisabled(!s_has_changes);
            if (ImGui::Button((std::string(ICON_FA_FLOPPY_DISK)
                              + "  Save to .iso").c_str(),
                              ImVec2(-1, 0)))
            {
                s_open_save_confirm = true;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered() && !s_has_changes) {
                ImGui::SetTooltip("No changes to save");
            }

            if (s_open_save_confirm) {
                ImGui::OpenPopup("Confirm Save");
                s_open_save_confirm = false;
            }
            if (ImGui::BeginPopupModal("Confirm Save", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                    "%s  Save modified terrain heights",
                    ICON_FA_TRIANGLE_EXCLAMATION);
                ImGui::TextWrapped(
                    "This will write the modified .ghf as a gzip "
                    "file under  <fable_root>/edited_heightfields/.\n"
                    "Your source ISO is NOT touched.");
                ImGui::Separator();
                ImGui::TextWrapped(
                    "Direct BNK / ISO injection is still on the "
                    "TODO list — for now you'll need to splice the "
                    "saved .ghf back into the ISO externally.");
                ImGui::Spacing();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button,
                    ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                    ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
                if (ImGui::Button((std::string(ICON_FA_FLOPPY_DISK)
                                  + "  Save Anyway").c_str(),
                                  ImVec2(160, 0)))
                {
                    std::string msg;
                    if (TerrainEdit::Save(msg)) {
                        OutputLog::success(
                            "Edit Terrain: saved modified .ghf → "
                            + msg);
                        s_has_changes = false;
                    } else {
                        OutputLog::error(
                            "Edit Terrain: save failed: " + msg);
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor(2);
                ImGui::EndPopup();
            }

            ImGui::Spacing();
            ImGui::Spacing();

            ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f),
                "%s  Tools", ICON_FA_PAINTBRUSH);
            ImGui::Separator();

            const bool edit_ready = TerrainEdit::IsLoaded();
            ImGui::BeginDisabled(!edit_ready);

            auto tool_btn = [&](const char* label, int tool_id,
                                const ImVec2& sz)
            {
                const bool active = (s_tool == tool_id);
                if (active) {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                        ImVec4(0.30f, 0.55f, 0.30f, 1.f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(0.40f, 0.65f, 0.40f, 1.f));
                }
                if (ImGui::Button(label, sz)) {
                    s_tool = active ? TT_NONE : tool_id;
                }
                if (active) ImGui::PopStyleColor(2);
            };

            const ImVec2 btn_size(ImGui::GetContentRegionAvail().x * 0.49f, 0);
            tool_btn((std::string(ICON_FA_ARROW_UP) + "  Raise##tool").c_str(),
                     TT_RAISE, btn_size);
            ImGui::SameLine();
            tool_btn((std::string(ICON_FA_ARROW_DOWN) + "  Lower##tool").c_str(),
                     TT_LOWER, btn_size);
            tool_btn((std::string(ICON_FA_DROPLET) + "  Smooth##tool").c_str(),
                     TT_SMOOTH, btn_size);
            ImGui::SameLine();
            tool_btn((std::string(ICON_FA_GRIP_LINES) + "  Flatten##tool").c_str(),
                     TT_FLATTEN, btn_size);

            ImGui::Spacing();
            if (ImGui::Button((std::string(ICON_FA_ARROW_ROTATE_LEFT)
                + "  Reset all changes").c_str(),
                ImVec2(-1, 0)))
            {
                TerrainEdit::Reset();
                upload_after_edit();
                s_tool = TT_NONE;
            }

            ImGui::EndDisabled();
            if (!edit_ready && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Terrain edit state not initialized "
                                  "(no .ghf loaded?)");
            }

            ImGui::Spacing();
            ImGui::Spacing();

            ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f),
                "%s  Brush", ICON_FA_BRUSH);
            ImGui::Separator();
            ImGui::SliderFloat("Size##brush",     &s_brush_size,
                               1.0f, 256.0f, "%.0f wu");
            ImGui::SliderFloat("Strength##brush", &s_brush_strength,
                               0.05f, 10.0f, "%.2f wu");

            ImGui::Spacing();
            if (s_tool == TT_NONE) {
                ImGui::TextDisabled("Select a tool, then left-click + "
                                    "drag on the terrain to apply.");
            } else {
                ImGui::TextColored(ImVec4(0.7f, 1.f, 0.7f, 1.f),
                    "Click + drag on terrain to apply.");
            }

            ImGui::BeginDisabled(!edit_ready || s_tool == TT_NONE);
            if (ImGui::Button((std::string(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT)
                + "  Apply to ALL").c_str(),
                ImVec2(-1, 0)))
            {
                switch (s_tool) {
                    case TT_RAISE:   TerrainEdit::RaiseAll(s_brush_strength); break;
                    case TT_LOWER:   TerrainEdit::LowerAll(s_brush_strength); break;
                    case TT_SMOOTH:  TerrainEdit::SmoothAll(); break;
                    case TT_FLATTEN: {
                        const auto& st = TerrainEdit::Get();
                        float sum = 0.f;
                        for (float h : st.heights_current) sum += h;
                        const float avg = st.heights_current.empty() ? 0.f :
                            sum / float(st.heights_current.size());
                        TerrainEdit::FlattenAll(avg);
                        break;
                    }
                    default: break;
                }
                upload_after_edit();
            }
            ImGui::EndDisabled();
        }
        ImGui::End();

        if (TerrainEdit::IsLoaded() && s_tool != TT_NONE) {
            ImVec2 mp_pos  = ImGui::GetIO().MousePos;
            const bool over_view =
                mp_pos.x >= origin.x   && mp_pos.x < origin.x + region.x &&
                mp_pos.y >= origin.y   && mp_pos.y < origin.y + region.y;
            const bool imgui_captured = ImGui::GetIO().WantCaptureMouse;

            g_te_ui.hover_valid = false;
            if (over_view && g_mp.width > 0 && g_mp.height > 0) {
                using namespace DirectX;

                const float cy = cosf(g_flycam.yaw);
                const float sy = sinf(g_flycam.yaw);
                const float cp = cosf(g_flycam.pitch);
                const float sp = sinf(g_flycam.pitch);
                const float forward[3] = { sy * cp, sp, cy * cp };
                XMVECTOR eye = XMVectorSet(g_flycam.pos[0],
                    g_flycam.pos[1], g_flycam.pos[2], 1);
                XMVECTOR at  = XMVectorSet(g_flycam.pos[0] + forward[0],
                    g_flycam.pos[1] + forward[1],
                    g_flycam.pos[2] + forward[2], 1);
                XMVECTOR up  = XMVectorSet(0, 1, 0, 0);
                XMMATRIX V = XMMatrixLookAtLH(eye, at, up);
                const float fov = XMConvertToRadians(60.0f);
                const float aspect = (float)g_mp.width / (float)g_mp.height;
                const float far_plane = g_mp.radius * 100.0f;
                XMMATRIX P = XMMatrixPerspectiveFovLH(fov, aspect,
                                                     0.05f, far_plane);
                XMMATRIX VP = V * P;
                XMVECTOR det;
                XMMATRIX inv_VP = XMMatrixInverse(&det, VP);

                const float u = (mp_pos.x - origin.x) / region.x;
                const float v = (mp_pos.y - origin.y) / region.y;
                const float ndc_x =  u * 2.f - 1.f;
                const float ndc_y =  1.f - v * 2.f;

                XMVECTOR near_pt = XMVector4Transform(
                    XMVectorSet(ndc_x, ndc_y, 0.f, 1.f), inv_VP);
                XMVECTOR far_pt  = XMVector4Transform(
                    XMVectorSet(ndc_x, ndc_y, 1.f, 1.f), inv_VP);
                near_pt = XMVectorScale(near_pt,
                    1.f / XMVectorGetW(near_pt));
                far_pt  = XMVectorScale(far_pt,
                    1.f / XMVectorGetW(far_pt));
                const float ox = XMVectorGetX(near_pt);
                const float oy = XMVectorGetY(near_pt);
                const float oz = XMVectorGetZ(near_pt);
                const float dx = XMVectorGetX(far_pt) - ox;
                const float dy = XMVectorGetY(far_pt) - oy;
                const float dz = XMVectorGetZ(far_pt) - oz;

                float hx, hy, hz;
                if (TerrainEdit::Raycast(ox, oy, oz, dx, dy, dz,
                                         hx, hy, hz))
                {
                    g_te_ui.hover_valid = true;
                    g_te_ui.hover_x = hx;
                    g_te_ui.hover_y = hy;
                    g_te_ui.hover_z = hz;

                    const int kSeg = 48;
                    ImVec2 last_screen{};
                    ImDrawList* dlay = ImGui::GetForegroundDrawList();
                    const float radius = s_brush_size;
                    for (int i = 0; i <= kSeg; ++i) {
                        const float ang =
                            (float)i / (float)kSeg * 6.2831853f;
                        const float wx = hx + cosf(ang) * radius;
                        const float wz = hz + sinf(ang) * radius;
                        const float wy =
                            TerrainEdit::SampleHeightAtWorldXZ(wx, wz);
                        XMVECTOR wpt = XMVectorSet(wx, wy, wz, 1.f);
                        XMVECTOR cs  = XMVector4Transform(wpt, VP);
                        const float ws = XMVectorGetW(cs);
                        if (ws <= 0.f) { last_screen = ImVec2(0,0); continue; }
                        const float nx = XMVectorGetX(cs) / ws;
                        const float ny = XMVectorGetY(cs) / ws;
                        const float sx = origin.x + (nx * 0.5f + 0.5f) * region.x;
                        const float sy = origin.y + (1.f - (ny * 0.5f + 0.5f)) * region.y;
                        const ImVec2 sc(sx, sy);
                        if (i > 0) {
                            dlay->AddLine(last_screen, sc,
                                IM_COL32(255, 215, 0, 220), 1.5f);
                        }
                        last_screen = sc;
                    }
                    XMVECTOR cpt = XMVector4Transform(
                        XMVectorSet(hx, hy, hz, 1.f), VP);
                    const float cw = XMVectorGetW(cpt);
                    if (cw > 0.f) {
                        const float cnx = XMVectorGetX(cpt) / cw;
                        const float cny = XMVectorGetY(cpt) / cw;
                        const float csx = origin.x
                            + (cnx * 0.5f + 0.5f) * region.x;
                        const float csy = origin.y
                            + (1.f - (cny * 0.5f + 0.5f)) * region.y;
                        dlay->AddCircleFilled(ImVec2(csx, csy), 3.f,
                            IM_COL32(255, 215, 0, 255));
                    }

                    if (!imgui_captured &&
                        ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    {
                        TerrainEdit::BrushTool bt =
                            TerrainEdit::BrushTool::None;
                        switch (s_tool) {
                            case TT_RAISE:   bt = TerrainEdit::BrushTool::Raise; break;
                            case TT_LOWER:   bt = TerrainEdit::BrushTool::Lower; break;
                            case TT_SMOOTH:  bt = TerrainEdit::BrushTool::Smooth; break;
                            case TT_FLATTEN: bt = TerrainEdit::BrushTool::Flatten; break;
                            default: break;
                        }
                        const float target_h =
                            (s_tool == TT_FLATTEN)
                            ? TerrainEdit::SampleHeightAtWorldXZ(hx, hz)
                            : 0.f;
                        TerrainEdit::ApplyBrush(bt, hx, hz,
                            s_brush_size, s_brush_strength, target_h);
                        upload_after_edit();
                    }
                }
            }
        }
    }

    if (g_mp.has_model && g_mp.bone_count > 0 && !S.anim_clips.empty()) {
        static float s_anim_alpha = 0.30f;
        const float kIdleAlpha   = 0.30f;
        const float kHoverAlpha  = 1.00f;

        const float kAnimW   = 280.0f;
        const float kAnimPad = 6.0f;

        const float anim_h = std::max(160.0f, region.y - 2 * kAnimPad);
        const ImVec2 anim_pos(origin.x + region.x - kAnimW - kAnimPad,
                              origin.y + kAnimPad);
        const ImVec2 anim_size(kAnimW, anim_h);

        ImGui::SetNextWindowPos(anim_pos);
        ImGui::SetNextWindowSize(anim_size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(s_anim_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_anim_alpha);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin("##anims_overlay", nullptr, fl)) {

            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            ImVec2 mp = ImGui::GetIO().MousePos;
            bool in_rect = mp.x >= wp.x && mp.x < wp.x + ws.x &&
                           mp.y >= wp.y && mp.y < wp.y + ws.y;
            static bool s_was_hovering = false;
            bool hovering = in_rect;

            if (!hovering && s_was_hovering &&
                ImGui::GetIO().MouseDown[0]) {
                hovering = true;
            }
            s_was_hovering = hovering;
            float target = hovering ? kHoverAlpha : kIdleAlpha;
            s_anim_alpha += (target - s_anim_alpha) * 0.18f;
            if (std::fabs(s_anim_alpha - target) < 0.005f) s_anim_alpha = target;

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Animations");
            ImGui::Separator();

            {
                auto& pl = Anim::global_player();
                const auto* cur = pl.clip();
                if (cur) {
                    const float dur_s = Anim::clip_duration_seconds(*cur);
                    const bool playing =
                        (pl.state() == Anim::AnimPlayer::State::Playing);
                    const bool paused  =
                        (pl.state() == Anim::AnimPlayer::State::Paused);

                    const float btn_lg = 36.0f;
                    const float btn_sm = 26.0f;
                    const float gap    = 10.0f;
                    const float row_w  = ImGui::GetContentRegionAvail().x;
                    const float group_w = btn_sm + gap + btn_lg + gap + btn_sm;
                    const float group_x = (row_w - group_w) * 0.5f;
                    const float row_y   = ImGui::GetCursorPosY();
                    const float sm_y    = row_y + (btn_lg - btn_sm) * 0.5f;

                    ImGui::SetCursorPos(ImVec2(group_x, sm_y));
                    if (UI::icon_button("##anim_stop", ICON_FA_STOP,
                                        btn_sm, false)) {
                        pl.stop();
                    }

                    ImGui::SetCursorPos(ImVec2(group_x + btn_sm + gap, row_y));
                    const char* play_glyph = playing ? ICON_FA_PAUSE : ICON_FA_PLAY;

                    float play_dx = playing ? 0.0f : 0.17f;
                    if (UI::icon_button("##anim_playpause", play_glyph,
                                        btn_lg, true, false, play_dx)) {
                        if (playing) pl.pause();
                        else if (paused) pl.resume();
                        else pl.play(cur, pl.is_loop());
                    }

                    ImGui::SetCursorPos(ImVec2(
                        group_x + btn_sm + gap + btn_lg + gap, sm_y));
                    bool loop = pl.is_loop();
                    if (UI::icon_button("##anim_loop", ICON_FA_REPEAT,
                                        btn_sm, false, loop)) {
                        pl.set_loop(!loop);
                    }

                    ImGui::Dummy(ImVec2(0, btn_lg + 4.0f));

                    ImGui::Text("%.2fs / %.2fs", pl.time(), dur_s);

                    {
                        const float scrub_h = 18.0f;
                        ImGui::InvisibleButton("##anim_scrub",
                                               ImVec2(-1, scrub_h));
                        ImVec2 r0 = ImGui::GetItemRectMin();
                        ImVec2 r1 = ImGui::GetItemRectMax();
                        bool active = ImGui::IsItemActive();
                        ImDrawList* dl = ImGui::GetWindowDrawList();

                        dl->AddRectFilled(r0, r1,
                                          IM_COL32(20, 22, 28, 255), 4.0f);

                        const float w = r1.x - r0.x;
                        const float cy = (r0.y + r1.y) * 0.5f;
                        const float prog = (dur_s > 0.0f)
                            ? (pl.time() / dur_s) : 0.0f;
                        const float playhead_x = r0.x + w * prog;

                        dl->AddRectFilled(r0,
                                          ImVec2(playhead_x, r1.y),
                                          IM_COL32(120, 200, 255, 200),
                                          4.0f);

                        bool hovered_event = false;
                        std::string ev_tip;
                        const ImVec2 mp = ImGui::GetIO().MousePos;
                        for (const auto& ev : cur->events) {
                            if (dur_s <= 0.0f) break;
                            float t = ev.time / dur_s;
                            if (t < 0.0f || t > 1.0f) continue;
                            float ex = r0.x + w * t;
                            dl->AddLine(ImVec2(ex, r0.y + 2),
                                        ImVec2(ex, r1.y - 2),
                                        IM_COL32(255, 200, 90, 230),
                                        1.5f);

                            if (ImGui::IsItemHovered() &&
                                std::fabs(mp.x - ex) <= 4.0f &&
                                !hovered_event) {
                                hovered_event = true;
                                ev_tip = ev.name;
                                if (!ev.param.empty())
                                    ev_tip += " — " + ev.param;
                                char tbuf[16];
                                std::snprintf(tbuf, sizeof(tbuf),
                                              "  @ %.2fs", ev.time);
                                ev_tip += tbuf;
                            }
                        }

                        dl->AddLine(ImVec2(playhead_x, r0.y + 1),
                                    ImVec2(playhead_x, r1.y - 1),
                                    IM_COL32(240, 245, 250, 255),
                                    2.0f);

                        if (active && dur_s > 0.0f) {
                            float t = (mp.x - r0.x) / w;
                            if (t < 0.0f) t = 0.0f;
                            if (t > 1.0f) t = 1.0f;
                            pl.seek(t * dur_s);
                        }

                        if (hovered_event) {
                            ImGui::SetTooltip("%s", ev_tip.c_str());
                        }
                    }

                    ImGui::Separator();
                }
            }

            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##anims_overlay_filter", "Filter",
                                     &S.anim_filter);

            static bool s_show_all_clips = false;
            if (S.dev_mode) {
                ImGui::Checkbox("Show all (ignore skeleton)",
                                &s_show_all_clips);
            }
            const uint32_t want_bones = g_mp.bone_count;
            const bool filter_by_bones = !s_show_all_clips;

            std::vector<int> vis;
            vis.reserve(S.anim_clips.size());
            std::string flow = S.anim_filter;
            std::transform(flow.begin(), flow.end(), flow.begin(), ::tolower);
            for (size_t i = 0; i < S.anim_clips.size(); ++i) {
                if (filter_by_bones) {
                    auto h = Anim::global_data_file().parse_clip_header(
                        S.anim_clips[i]);
                    if (!h.ok || h.bone_count != want_bones) continue;
                }
                if (flow.empty()) {
                    vis.push_back((int)i);
                } else {
                    std::string nlow = S.anim_clips[i].name;
                    std::transform(nlow.begin(), nlow.end(),
                                   nlow.begin(), ::tolower);
                    if (nlow.find(flow) != std::string::npos) {
                        vis.push_back((int)i);
                    }
                }
            }
            if (S.dev_mode) {
                ImGui::TextDisabled("%d / %zu  (skel=%u bones)",
                                    (int)vis.size(),
                                    S.anim_clips.size(),
                                    g_mp.bone_count);
            }

            if (S.dev_mode &&
                S.anim_selected_clip >= 0 &&
                S.anim_selected_clip < (int)S.anim_clips.size())
            {
                const auto& c = S.anim_clips[(size_t)S.anim_selected_clip];
                ImGui::Separator();
                if (Anim::global_data_file().is_open()) {
                    auto h = Anim::global_data_file().parse_clip_header(c);
                    if (h.ok) {
                        ImGui::TextDisabled(
                            "bones=%u idx_bits=%u frames=%u",
                            h.bone_count, h.bone_idx_bits, h.field_C);

                        if (ImGui::TreeNodeEx("##anim_bone_view",
                                              ImGuiTreeNodeFlags_None,
                                              "Per-bone bodies")) {
                            auto sp = Anim::global_data_file().clip_bytes(c);
                            const size_t total = sp.size;
                            ImGui::BeginChild("##anim_bone_list",
                                              ImVec2(0, 120), false,
                                              ImGuiWindowFlags_HorizontalScrollbar);
                            for (uint32_t bi = 0; bi < h.bone_count; ++bi) {
                                uint32_t bo = h.bone_offsets[bi];
                                uint32_t be = (bi + 1 < h.bone_count)
                                    ? h.bone_offsets[bi + 1]
                                    : (uint32_t)total;
                                if (be < bo || be > total) continue;
                                uint32_t blen = be - bo;
                                char hexbuf[3 * 4 + 1] = "??";
                                if (bo + 4 <= total) {
                                    std::snprintf(hexbuf, sizeof(hexbuf),
                                                  "%02X %02X %02X %02X",
                                                  sp.data[bo + 0],
                                                  sp.data[bo + 1],
                                                  sp.data[bo + 2],
                                                  sp.data[bo + 3]);
                                }
                                ImGui::TextDisabled(
                                    "bone %3u  len=%5u  first4: %s",
                                    bi, blen, hexbuf);
                            }
                            ImGui::EndChild();
                            ImGui::TreePop();
                        }
                    } else {
                        ImGui::TextDisabled(
                            "(unrecognised clip header: m=0x%08X v=%u)",
                            h.magic, h.version);
                    }
                } else {
                    ImGui::TextDisabled("(data file not loaded)");
                }
                ImGui::Separator();
            }

            ImGui::BeginChild("##anims_overlay_list", ImVec2(0, 0), false);
            ImGuiListClipper clipper;
            clipper.Begin((int)vis.size());
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart;
                     row < clipper.DisplayEnd; ++row) {
                    const auto& c =
                        S.anim_clips[(size_t)vis[(size_t)row]];
                    ImGui::PushID(row);
                    bool selected =
                        (S.anim_selected_clip == vis[(size_t)row]);
                    char label[80];
                    float dur_s = Anim::clip_duration_seconds(c);
                    std::snprintf(label, sizeof(label), "%s  (%.2fs)",
                                  c.name.c_str(), dur_s);
                    if (ImGui::Selectable(label, selected,
                                          ImGuiSelectableFlags_SpanAllColumns)) {
                        S.anim_selected_clip = vis[(size_t)row];

                        Anim::global_player().play(
                            &S.anim_clips[(size_t)vis[(size_t)row]],
                            Anim::global_player().is_loop());
                    }
                    if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted(c.name.c_str());
                        ImGui::Text("Duration: %.3f s  (%.0f fps)",
                                    dur_s, c.fps);
                        ImGui::Text("Events: %zu", c.events.size());
                        if (S.dev_mode) {
                            ImGui::Text("offset=0x%08X len=%u",
                                        c.data_offset, c.data_length);
                        }
                        ImGui::EndTooltip();
                    }
                    ImGui::PopID();
                }
            }
            clipper.End();
            ImGui::EndChild();
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    if (::g_tex_popout_open && ::g_tex_popout_srv) {
        int tw = 0, th = 0;
        ID3D11Resource* res = nullptr;
        ::g_tex_popout_srv->GetResource(&res);
        if (res) {

            ID3D11Texture2D* t2d = (ID3D11Texture2D*)res;
            D3D11_TEXTURE2D_DESC desc{};
            t2d->GetDesc(&desc);
            tw = (int)desc.Width;
            th = (int)desc.Height;
            res->Release();
        }
        if (tw > 0 && th > 0) {
            std::string title = "Texture: "
                + std::filesystem::path(::g_tex_popout_name).filename().string()
                + "##tex_popout";

            ImGuiWindowFlags fl = ImGuiWindowFlags_NoCollapse
                                | ImGuiWindowFlags_NoResize
                                | ImGuiWindowFlags_AlwaysAutoResize;
            if (ImGui::Begin(title.c_str(), &::g_tex_popout_open, fl)) {

                ImGui::Checkbox("Show UVs", &::g_tex_popout_show_uvs);

                ImGui::Image((ImTextureID)::g_tex_popout_srv,
                             ImVec2((float)tw, (float)th));

                {
                    ImVec2 img_min = ImGui::GetItemRectMin();
                    ImGui::SetCursorScreenPos(img_min);
                    ImGui::InvisibleButton("##popout_hit",
                                           ImVec2((float)tw, (float)th));
                    if (ImGui::BeginPopupContextItem()) {
                        const std::string& preferred_bnk =
                            (S.selected_nested_index != -1 &&
                             !S.selected_nested_temp_path.empty())
                                ? S.selected_nested_temp_path
                                : S.selected_bnk;
                        tex_export_menu_named(::g_tex_popout_name,
                                              ::g_tex_popout_name,
                                              preferred_bnk, 0);
                        ImGui::EndPopup();
                    }
                }

                if (::g_tex_popout_show_uvs &&
                    ::g_tex_popout_mesh_idx >= 0 &&
                    (size_t)::g_tex_popout_mesh_idx < g_mp.meshes.size())
                {
                    uint32_t src = g_mp.meshes[(size_t)::g_tex_popout_mesh_idx].source_mesh_idx;
                    if (src < S.mdl_meshes.size()) {
                        const auto& geom = S.mdl_meshes[src];
                        if (!geom.uvs.empty() && !geom.indices.empty()) {
                            ImVec2 img_min = ImGui::GetItemRectMin();
                            ImVec2 img_max = ImGui::GetItemRectMax();
                            float w_px = img_max.x - img_min.x;
                            float h_px = img_max.y - img_min.y;
                            ImDrawList* dl = ImGui::GetWindowDrawList();

                            const ImU32 col = IM_COL32(255, 255, 255, 200);
                            const float thickness = 1.0f;

                            for (size_t i = 0; i + 2 < geom.indices.size(); i += 3) {
                                uint32_t a = geom.indices[i];
                                uint32_t b = geom.indices[i + 1];
                                uint32_t c = geom.indices[i + 2];
                                if ((size_t)a * 2 + 1 >= geom.uvs.size()) continue;
                                if ((size_t)b * 2 + 1 >= geom.uvs.size()) continue;
                                if ((size_t)c * 2 + 1 >= geom.uvs.size()) continue;
                                ImVec2 pa(img_min.x + geom.uvs[a * 2 + 0] * w_px,
                                          img_min.y + geom.uvs[a * 2 + 1] * h_px);
                                ImVec2 pb(img_min.x + geom.uvs[b * 2 + 0] * w_px,
                                          img_min.y + geom.uvs[b * 2 + 1] * h_px);
                                ImVec2 pc(img_min.x + geom.uvs[c * 2 + 0] * w_px,
                                          img_min.y + geom.uvs[c * 2 + 1] * h_px);
                                dl->AddLine(pa, pb, col, thickness);
                                dl->AddLine(pb, pc, col, thickness);
                                dl->AddLine(pc, pa, col, thickness);
                            }
                        }
                    }
                }
            }
            ImGui::End();
        }

        if (!::g_tex_popout_open) {
            ::g_tex_popout_srv = nullptr;
            ::g_tex_popout_name.clear();
        }
    }
}

void draw_heightmap_popout() {
    if (::g_heightmap_popout_open && ::g_heightmap_popout_srv) {
        const int hw = ::g_heightmap_popout_w;
        const int hh = ::g_heightmap_popout_h;

        if (hw > 0 && hh > 0) {
            ImGuiViewport* vp = ImGui::GetMainViewport();
            const float vw = vp->WorkSize.x;
            const float vh = vp->WorkSize.y;
            const float cap_w = vw * 0.8f;
            const float cap_h = vh * 0.8f;
            float scale = 1.0f;
            if ((float)hw > cap_w || (float)hh > cap_h) {
                scale = std::min(cap_w / (float)hw, cap_h / (float)hh);
            }
            const float dw = std::max(64.0f, (float)hw * scale);
            const float dh = std::max(64.0f, (float)hh * scale);

            std::string title = "Heightmap: " + ::g_heightmap_popout_name
                              + "##heightmap_popout";
            ImGuiWindowFlags fl = ImGuiWindowFlags_NoCollapse
                                | ImGuiWindowFlags_AlwaysAutoResize;
            if (ImGui::Begin(title.c_str(),
                             &::g_heightmap_popout_open, fl)) {
                ImGui::Image((ImTextureID)::g_heightmap_popout_srv,
                             ImVec2(dw, dh));

                ImVec2 img_min = ImGui::GetItemRectMin();
                ImGui::SetCursorScreenPos(img_min);
                ImGui::InvisibleButton("##hmap_popout_hit",
                                       ImVec2(dw, dh));
                if (ImGui::BeginPopupContextItem()) {
                    tex_export_menu_rgba(::g_heightmap_popout_name,
                                         ::g_heightmap_popout_rgba,
                                         hw, hh);
                    ImGui::EndPopup();
                }
            }
            ImGui::End();
        }

        if (!::g_heightmap_popout_open) {
            if (::g_heightmap_popout_srv) {
                ::g_heightmap_popout_srv->Release();
                ::g_heightmap_popout_srv = nullptr;
            }
            ::g_heightmap_popout_name.clear();
            ::g_heightmap_popout_rgba.clear();
            ::g_heightmap_popout_w = 0;
            ::g_heightmap_popout_h = 0;
        }
    }
}
#endif

}

#ifdef _WIN32
void draw_render_panel(ID3D11Device* device) {

    if (g_mp.has_model) {
        draw_model_in_panel(device);
    } else if (S.texture_window_srv) {
        draw_texture_in_panel(device);
    } else if (S.show_lua_render) {
        draw_lua_in_panel();
    } else {
        draw_placeholder();
    }

    draw_heightmap_popout();
}
#else
namespace {
void draw_texture_in_panel_gl() {
    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(origin,
                      ImVec2(origin.x + region.x, origin.y + region.y),
                      IM_COL32(20, 22, 28, 255));

    if (!S.texture_window_gl || S.texture_window_width <= 0 || S.texture_window_height <= 0) {
        const char* msg = S.texture_window_name.empty()
            ? "Texture decode failed"
            : S.texture_window_name.c_str();
        ImVec2 sz = ImGui::CalcTextSize(msg);
        ImVec2 pos(origin.x + (region.x - sz.x) * 0.5f,
                   origin.y + (region.y - sz.y) * 0.5f);
        dl->AddText(pos, IM_COL32(255, 90, 90, 230), msg);
        ImGui::Dummy(region);
        return;
    }

    float tw = (float)S.texture_window_width;
    float th = (float)S.texture_window_height;
    float scale = std::min(region.x / tw, region.y / th);
    if (scale > 4.0f) scale = 4.0f;
    float dw = tw * scale;
    float dh = th * scale;
    float x0 = origin.x + (region.x - dw) * 0.5f;
    float y0 = origin.y + (region.y - dh) * 0.5f;

    dl->AddImage((ImTextureID)(intptr_t)S.texture_window_gl,
                 ImVec2(x0, y0),
                 ImVec2(x0 + dw, y0 + dh));

    ImGui::SetCursorScreenPos(ImVec2(x0, y0));
    ImGui::InvisibleButton("##tex_preview_hit", ImVec2(dw, dh));
    if (S.tex_info_ok && !S.texture_blob.empty() &&
        ImGui::BeginPopupContextItem()) {
        tex_export_menu_blob(S.texture_window_name,
                             S.texture_blob,
                             S.texture_mip_index);
        ImGui::EndPopup();
    }
}

void apply_orbit_to_flycam_gl() {
    float cy = std::cos(S.cam_yaw);
    float sy = std::sin(S.cam_yaw);
    float cp = std::cos(S.cam_pitch);
    float sp = std::sin(S.cam_pitch);
    g_flycam.pos[0] = g_mp.center[0] + sy * cp * S.cam_dist * g_mp.radius;
    g_flycam.pos[1] = g_mp.center[1] + sp * S.cam_dist * g_mp.radius;
    g_flycam.pos[2] = g_mp.center[2] + cy * cp * S.cam_dist * g_mp.radius;
    float dx = g_mp.center[0] - g_flycam.pos[0];
    float dy = g_mp.center[1] - g_flycam.pos[1];
    float dz = g_mp.center[2] - g_flycam.pos[2];
    float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len > 0.0001f) {
        g_flycam.yaw = std::atan2(dx, dz);
        g_flycam.pitch = std::asin(dy / len);
    }
}

void draw_materials_overlay_gl(const ImVec2& origin,
                               const ImVec2& region,
                               float next_overlay_y) {
    if (g_mp.has_model && g_mp.lod_count > 1) {
        static float s_lod_alpha = 0.30f;
        const float kIdleAlpha = 0.30f;
        const float kHoverAlpha = 1.00f;

        ImGui::SetNextWindowPos(ImVec2(origin.x + 6, next_overlay_y));
        ImGui::SetNextWindowSize(ImVec2(190, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(s_lod_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_lod_alpha);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##lod_overlay", nullptr, fl)) {
            bool hovering = ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                ImGuiHoveredFlags_ChildWindows);
            float target = hovering ? kHoverAlpha : kIdleAlpha;
            s_lod_alpha += (target - s_lod_alpha) * 0.18f;
            if (std::fabs(s_lod_alpha - target) < 0.005f) s_lod_alpha = target;

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "LOD");
            const int lod_count = (int)g_mp.lod_count;
            int current = g_mp.selected_lod;
            if (current < -1 || current >= lod_count) current = 0;

            if (ImGui::RadioButton("All", current == -1)) {
                g_mp.selected_lod = -1;
            }
            for (int i = 0; i < lod_count; ++i) {
                ImGui::SameLine();
                char lbl[16];
                std::snprintf(lbl, sizeof(lbl), "%d", i);
                if (ImGui::RadioButton(lbl, current == i)) {
                    g_mp.selected_lod = i;
                }
            }

            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            next_overlay_y = wp.y + ws.y + 6.0f;
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    if (!g_mp.has_model || g_mp.meshes.empty()) {
        ::g_highlight_mesh_idx = -1;
        ::g_isolate_mesh_idx = -1;
        ::g_tex_popout_open = false;
        ::g_tex_popout_gl = 0;
        ::g_tex_popout_name.clear();
        ::g_tex_popout_mesh_idx = -1;
        return;
    }

    static float s_mat_alpha = 0.30f;
    const float kIdleAlpha = 0.30f;
    const float kHoverAlpha = 1.00f;
    const float kMatW = 296.0f;
    float max_h = std::max(160.0f,
                           region.y - (next_overlay_y - origin.y) - 20.0f);

    ImGui::SetNextWindowPos(ImVec2(origin.x + 6, next_overlay_y));
    ImGui::SetNextWindowSizeConstraints(ImVec2(kMatW, 0.0f),
                                        ImVec2(kMatW, max_h));
    ImGui::SetNextWindowBgAlpha(s_mat_alpha * 0.78f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_mat_alpha);

    ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##materials_overlay", nullptr, fl)) {
        bool hovering = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_AllowWhenBlockedByPopup |
            ImGuiHoveredFlags_ChildWindows);
        float target = hovering ? kHoverAlpha : kIdleAlpha;
        s_mat_alpha += (target - s_mat_alpha) * 0.18f;
        if (std::fabs(s_mat_alpha - target) < 0.005f) s_mat_alpha = target;

        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Materials");
        ImGui::Separator();

        const ImVec2 thumb_size(48, 48);
        if (!g_mp.no_tilt) for (size_t mi = 0; mi < g_mp.meshes.size(); ++mi) {
        auto& mesh = g_mp.meshes[mi];

        if (g_mp.selected_lod >= 0 &&
            mesh.lod_index != (uint32_t)g_mp.selected_lod) {
            continue;
        }

        ImGui::PushID((int)mi);
        ImGui::TextUnformatted(mesh.name.c_str());
            bool h = (::g_highlight_mesh_idx == (int)mi);
            bool iso = (::g_isolate_mesh_idx == (int)mi);
            if (ImGui::Checkbox("Highlight", &h)) {
                if (h) {
                    ::g_highlight_mesh_idx = (int)mi;
                    ::g_isolate_mesh_idx = -1;
                } else if (::g_highlight_mesh_idx == (int)mi) {
                    ::g_highlight_mesh_idx = -1;
                }
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Isolate", &iso)) {
                if (iso) {
                    ::g_isolate_mesh_idx = (int)mi;
                    ::g_highlight_mesh_idx = -1;
                } else if (::g_isolate_mesh_idx == (int)mi) {
                    ::g_isolate_mesh_idx = -1;
                }
            }

            struct ThumbSpec {
                const char* slot_id;
                unsigned int tex;
                const std::string* name;
                bool* visible;
            };
            ThumbSpec thumbs[5] = {
                {"diffuse",  mesh.tex_diffuse,  &mesh.diffuse_tex_name,  &mesh.diffuse_visible},
                {"normal",   mesh.tex_normal,   &mesh.normal_tex_name,   &mesh.normal_visible},
                {"specular", mesh.tex_specular, &mesh.specular_tex_name, &mesh.specular_visible},
                {"metallic", mesh.tex_metallic, &mesh.metallic_tex_name, &mesh.metallic_visible},
                {"extra",    mesh.tex_extra,    &mesh.extra_tex_name,    &mesh.extra_visible},
            };

            bool any_thumb = false;
            for (int ti = 0; ti < 5; ++ti) {
                const ThumbSpec& t = thumbs[ti];
                if (!t.tex || t.tex == g_mp.default_tex) continue;
                if (t.name->empty()) continue;
                if (any_thumb) ImGui::SameLine();
                any_thumb = true;
                ImGui::PushID(t.slot_id);
                ImGui::BeginGroup();
                ImVec4 tint = (*t.visible) ? ImVec4(1, 1, 1, 1)
                                           : ImVec4(0.45f, 0.45f, 0.45f, 1);
                if (ImGui::ImageButton("##t",
                                       (ImTextureID)(intptr_t)t.tex,
                                       thumb_size,
                                       ImVec2(0, 0), ImVec2(1, 1),
                                       ImVec4(0, 0, 0, 0), tint)) {
                    ::g_tex_popout_gl = t.tex;
                    ::g_tex_popout_name = *t.name;
                    ::g_tex_popout_open = true;
                    ::g_tex_popout_mesh_idx = (int)mi;
                }
                if (ImGui::BeginPopupContextItem()) {
                    const std::string& preferred_bnk =
                        (S.selected_nested_index != -1 &&
                         !S.selected_nested_temp_path.empty())
                            ? S.selected_nested_temp_path
                            : S.selected_bnk;
                    tex_export_menu_named(*t.name, *t.name,
                                          preferred_bnk, 0);
                    ImGui::EndPopup();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s\n[%s]", t.name->c_str(), t.slot_id);
                }
                ImGui::Checkbox("##vis", t.visible);
                ImGui::EndGroup();
                ImGui::PopID();
            }
            if (!any_thumb) ImGui::TextDisabled("(no textures)");
            ImGui::Separator();
        ImGui::PopID();
    }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void draw_model_in_panel_gl() {
    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    int w = std::max(1, (int)region.x);
    int h = std::max(1, (int)region.y);

    if (!g_mp_initialized) {
        g_mp_initialized = MP_Init(g_mp, w, h);
    }
    if (!g_mp_initialized) {
        ImGui::Dummy(region);
        return;
    }

    MP_Resize(g_mp, w, h);
    ImGui::InvisibleButton("##model_render", region);
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();

    if (S.terrain_mode) {
        float dt = ImGui::GetIO().DeltaTime;
        if (hovered || g_flycam.is_looking) {
            ::render_panel_handle_flycam(dt);
        }
    } else {
        if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            const float kOrbitSensitivity = 0.008f;
            S.cam_yaw += d.x * kOrbitSensitivity;
            S.cam_pitch += d.y * kOrbitSensitivity;
            const float kPitchLimit = 1.5f;
            if (S.cam_pitch > kPitchLimit) S.cam_pitch = kPitchLimit;
            if (S.cam_pitch < -kPitchLimit) S.cam_pitch = -kPitchLimit;
        }
        if (hovered) {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {
                S.cam_dist *= (wheel > 0.0f) ? 0.9f : 1.111f;
                if (S.cam_dist < 0.3f) S.cam_dist = 0.3f;
                if (S.cam_dist > 50.0f) S.cam_dist = 50.0f;
            }
        }
        apply_orbit_to_flycam_gl();
    }

    for (size_t i = 0; i < g_mp.meshes.size(); ++i) {
        g_mp.meshes[i].highlight = ((int)i == ::g_highlight_mesh_idx);
        g_mp.meshes[i].isolated = ((int)i == ::g_isolate_mesh_idx);
    }

    MP_Render(g_mp, g_flycam);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    unsigned int tex = MP_GetTexture(g_mp);
    if (tex) {
        dl->AddImage((ImTextureID)(intptr_t)tex,
                     origin,
                     ImVec2(origin.x + region.x, origin.y + region.y),
                     ImVec2(0.0f, 1.0f),
                     ImVec2(1.0f, 0.0f));
    }

    if (hovered && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        MP_Release(g_mp);
        g_mp.has_model = false;
        g_mp_initialized = false;
        S.show_model_preview = false;
        S.model_preview_open = false;
        S.selected_bone = -1;
    }

    dl->AddRectFilled(ImVec2(origin.x + 6, origin.y + 6),
                      ImVec2(origin.x + 196, origin.y + 70),
                      IM_COL32(20, 22, 28, 200), 4.0f);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 12));
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Controls");
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 30));
    ImGui::TextDisabled(S.terrain_mode ? "R-Drag  look" : "L-Drag  rotate");
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 46));
    ImGui::TextDisabled(S.terrain_mode ? "WASD/QE  move" : "Wheel  zoom  /  ESC  close");

    draw_materials_overlay_gl(origin, region, origin.y + 76.0f);
}

void draw_texture_popout_gl() {
    if (!::g_tex_popout_open || !::g_tex_popout_gl) return;

    int tw = 0;
    int th = 0;
    GLint prev_tex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
    glBindTexture(GL_TEXTURE_2D, ::g_tex_popout_gl);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex);

    if (tw > 0 && th > 0) {
        std::string title = "Texture: "
            + std::filesystem::path(::g_tex_popout_name).filename().string()
            + "##tex_popout";
        ImGuiWindowFlags fl = ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin(title.c_str(), &::g_tex_popout_open, fl)) {
            ImGui::Checkbox("Show UVs", &::g_tex_popout_show_uvs);
            ImGui::Image((ImTextureID)(intptr_t)::g_tex_popout_gl,
                         ImVec2((float)tw, (float)th));

            ImVec2 img_min = ImGui::GetItemRectMin();
            ImGui::SetCursorScreenPos(img_min);
            ImGui::InvisibleButton("##popout_hit",
                                   ImVec2((float)tw, (float)th));
            if (ImGui::BeginPopupContextItem()) {
                const std::string& preferred_bnk =
                    (S.selected_nested_index != -1 &&
                     !S.selected_nested_temp_path.empty())
                        ? S.selected_nested_temp_path
                        : S.selected_bnk;
                tex_export_menu_named(::g_tex_popout_name,
                                      ::g_tex_popout_name,
                                      preferred_bnk, 0);
                ImGui::EndPopup();
            }

            if (::g_tex_popout_show_uvs &&
                ::g_tex_popout_mesh_idx >= 0 &&
                (size_t)::g_tex_popout_mesh_idx < g_mp.meshes.size()) {
                uint32_t src = g_mp.meshes[(size_t)::g_tex_popout_mesh_idx].source_mesh_idx;
                if (src < S.mdl_meshes.size()) {
                    const auto& geom = S.mdl_meshes[src];
                    if (!geom.uvs.empty() && !geom.indices.empty()) {
                        ImVec2 img_max = ImGui::GetItemRectMax();
                        float w_px = img_max.x - img_min.x;
                        float h_px = img_max.y - img_min.y;
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        const ImU32 col = IM_COL32(255, 255, 255, 200);
                        for (size_t i = 0; i + 2 < geom.indices.size(); i += 3) {
                            uint32_t a = geom.indices[i];
                            uint32_t b = geom.indices[i + 1];
                            uint32_t c = geom.indices[i + 2];
                            if ((size_t)a * 2 + 1 >= geom.uvs.size()) continue;
                            if ((size_t)b * 2 + 1 >= geom.uvs.size()) continue;
                            if ((size_t)c * 2 + 1 >= geom.uvs.size()) continue;
                            ImVec2 pa(img_min.x + geom.uvs[a * 2 + 0] * w_px,
                                      img_min.y + geom.uvs[a * 2 + 1] * h_px);
                            ImVec2 pb(img_min.x + geom.uvs[b * 2 + 0] * w_px,
                                      img_min.y + geom.uvs[b * 2 + 1] * h_px);
                            ImVec2 pc(img_min.x + geom.uvs[c * 2 + 0] * w_px,
                                      img_min.y + geom.uvs[c * 2 + 1] * h_px);
                            dl->AddLine(pa, pb, col, 1.0f);
                            dl->AddLine(pb, pc, col, 1.0f);
                            dl->AddLine(pc, pa, col, 1.0f);
                        }
                    }
                }
            }
        }
        ImGui::End();
    }

    if (!::g_tex_popout_open) {
        ::g_tex_popout_gl = 0;
        ::g_tex_popout_name.clear();
    }
}
}

void draw_render_panel() {
    if (g_mp.has_model) {
        draw_model_in_panel_gl();
    } else if (S.texture_window_gl) {
        draw_texture_in_panel_gl();
    } else if (S.show_lua_render) {
        draw_lua_in_panel();
    } else {
        draw_placeholder();
    }
    draw_texture_popout_gl();
}
#endif

}
