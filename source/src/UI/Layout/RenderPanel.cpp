#include "RenderPanel.h"
#include "../../Utilities/State.h"
#include "../ModelPreview.h"
#include "../../textures/export/TextureExport.h"
#include "../../animations/AnimBank.h"
#include "../../animations/AnimDataFile.h"
#include "../../animations/AnimPlayer.h"
#include "../IconButton.h"
#include "IconsFontAwesome6.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdint>
#include <string>
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

bool g_skel_overlay_show = false;

int g_highlight_mesh_idx = -1;
int g_isolate_mesh_idx   = -1;

#ifdef _WIN32
ID3D11ShaderResourceView* g_tex_popout_srv = nullptr;
#endif
std::string g_tex_popout_name;
bool        g_tex_popout_open    = false;

int         g_tex_popout_mesh_idx = -1;

bool        g_tex_popout_show_uvs = false;

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

void bind_point_sampler_cb(const ImDrawList* /*dl*/,
                           const ImDrawCmd*  /*cmd*/) {
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

void draw_model_in_panel(ID3D11Device* device) {
    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    int w = std::max(1, (int)region.x);
    int h = std::max(1, (int)region.y);

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
        int picked = pick_bone_at(mp, origin, region, /*radius_px=*/12.0f);
        S.selected_bone = picked;
    }

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

    if (skel_visible && hovered && ImGui::IsKeyPressed(ImGuiKey_R)) {
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
        g_mp.meshes[i].highlight = ((int)i == ::g_highlight_mesh_idx);
        g_mp.meshes[i].isolated  = ((int)i == ::g_isolate_mesh_idx);
    }

    apply_orbit_to_flycam();
    MP_Render(device, g_mp, g_flycam);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (g_mp.srv) {
        dl->AddImage((ImTextureID)g_mp.srv,
                     origin,
                     ImVec2(origin.x + region.x, origin.y + region.y));
    }

    if (hovered && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (S.bone_rotate_mode) {
            cancel_rotate();
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

    /* LOD switcher — only shown when the model has more than one
       LOD group (V2's walker tags each mesh's name with a "|lod<N>"
       suffix that MP_Build parses into MPPerMesh.lod_index). */
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
            int current = g_mp.selected_lod;        /* -1 = All */
            if (current < -1 || current >= lod_count) current = 0;

            /* "All" radio shows every LOD overlaid (legacy behavior). */
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

            for (size_t mi = 0; mi < g_mp.meshes.size(); ++mi) {
                auto& mesh = g_mp.meshes[mi];

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
                        const std::string& preferred_bnk =
                            (S.selected_nested_index != -1 &&
                             !S.selected_nested_temp_path.empty())
                                ? S.selected_nested_temp_path
                                : S.selected_bnk;
                        tex_export_menu_named(*t.name, *t.name,
                                              preferred_bnk, /*mip=*/0);
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
        }
        ImGui::End();
        ImGui::PopStyleVar();
    } else {

        ::g_highlight_mesh_idx  = -1;
        ::g_isolate_mesh_idx    = -1;
        ::g_tex_popout_open     = false;
        ::g_tex_popout_srv      = nullptr;
        ::g_tex_popout_name.clear();
        ::g_tex_popout_mesh_idx = -1;
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
                            /*loop=*/Anim::global_player().is_loop());
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
                                              preferred_bnk, /*mip=*/0);
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
}
#else
void draw_render_panel() {
    UI::draw_placeholder();
}
#endif

}
