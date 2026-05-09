#include "RenderPanel.h"
#include "../../Utilities/State.h"
#include "../ModelPreview.h"

#include "imgui.h"

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
extern FlyCam g_flycam;       // declared in ModelPreview.h with external linkage

// Defined in this TU. Lifted to file scope (out of the overlay block's
// static-local) so the input-handling code earlier in draw_model_in_panel
// can route picking + R-rotate based on whether the skeleton overlay is
// currently visible.
bool g_skel_overlay_show = false;

// Materials-overlay state — driven by the Materials overlay below the
// skeleton overlay. Spec: only ONE submesh can have either flag set at
// a time, AND the two flags are mutually exclusive across all
// submeshes. Stored as indices instead of booleans so the radio
// behaviour is intrinsic. -1 = nothing selected.
int g_highlight_mesh_idx = -1;
int g_isolate_mesh_idx   = -1;

// Texture popout — set when the user clicks a thumbnail in the
// Materials overlay. The window stays up until the user closes it.
#ifdef _WIN32
ID3D11ShaderResourceView* g_tex_popout_srv = nullptr;
#endif
std::string g_tex_popout_name;
bool        g_tex_popout_open    = false;
// Index into g_mp.meshes that the popout was opened from — used by the
// "Show UVs" overlay to find the matching MDLMeshGeom (UVs/indices) in
// S.mdl_meshes via that mesh's source_mesh_idx.
int         g_tex_popout_mesh_idx = -1;
// Persists across popout opens so the user doesn't have to re-tick it
// every time they click a different thumbnail.
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
    const char* msg = "Click a .mdl or .tex file in the tree";
    ImVec2 sz = ImGui::CalcTextSize(msg);
    ImVec2 pos(origin.x + (region.x - sz.x) * 0.5f,
               origin.y + (region.y - sz.y) * 0.5f);
    dl->AddText(pos, IM_COL32(110, 120, 135, 255), msg);
    ImGui::Dummy(region);
}

#ifdef _WIN32
void draw_texture_in_panel() {
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
    dl->AddImage((ImTextureID)S.texture_window_srv,
                 ImVec2(x0, y0),
                 ImVec2(x0 + dw, y0 + dh));

    char info[128];
    std::snprintf(info, sizeof(info), "%dx%d", S.texture_window_width, S.texture_window_height);
    dl->AddText(ImVec2(origin.x + 8, origin.y + 6),
                IM_COL32(180, 190, 205, 220), info);

    ImGui::Dummy(region);
}

// Convert the orbit-camera state (S.cam_yaw, S.cam_pitch, S.cam_dist)
// into a FlyCam positioned around the model's center, looking toward
// it. The model preview's MP_Render still consumes a FlyCam — we just
// drive that FlyCam from orbit math instead of WASD/right-click input.
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
    // Camera looks back at the model center — the FlyCam yaw/pitch must
    // produce a forward vector that is the negative of the offset above.
    g_flycam.yaw   = yaw + 3.14159265f;
    g_flycam.pitch = -pitch;
    g_flycam.is_looking = false;          // suppress legacy mouse-look state
}

// Project every bone's joint origin into screen space, given the same
// W * V * P chain MP_Render uses. Returns visibility flags and screen
// positions per bone. Used both for drawing the skeleton overlay AND
// for click-picking (so picking and rendering see the same positions).
static void project_bones_to_screen(
    const std::vector<float>& world_pose,        // [bone_count*16] from MP_ComputeWorldPose
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

// Draw skeleton overlay using the live pose (rest + per-bone deltas).
// Highlights the currently-selected bone (S.selected_bone) with a
// brighter colour and a larger marker.
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

    // Bone segments (parent -> child). Tint segments adjacent to the
    // selected bone so the user can trace the chain at a glance.
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
    // Joint markers — selected bone gets a thicker ringed dot.
    for (uint32_t i = 0; i < n; ++i) {
        if (!visible[i]) continue;
        if ((int)i == S.selected_bone) {
            dl->AddCircleFilled(screen[i], 5.0f, sel_dot);
            dl->AddCircle      (screen[i], 7.5f, IM_COL32(0, 0, 0, 220), 0, 2.0f);
        } else {
            dl->AddCircleFilled(screen[i], 2.5f, dot_col);
        }
    }

    // Status line in the corner — mode + selected bone name.
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

// Click-pick: find nearest projected bone within `radius_px`. Returns
// -1 on miss. Reuses the same projection helper the renderer uses so
// what the user sees IS what they click.
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

    // Reserve the area as an item so we can hover-test for camera input.
    ImGui::InvisibleButton("##model_render", region);
    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();

    // Skeleton picking + edit are gated on the overlay being visible —
    // the file-scope flag g_skel_overlay_show (defined at the top of
    // this TU) is owned by the overlay block further down. Reading it
    // here lets the input-handling block route to picking / rotate
    // appropriately on the same frame.
    bool skel_visible = ::g_skel_overlay_show && (g_mp.bone_count > 0);

    // ROTATE MODE: takes precedence over picking and orbit. Behaviour:
    //   R           — enter rotate mode (must already have a bone picked).
    //                  Snapshots the bone's current delta quaternion so
    //                  RMB/ESC can revert.
    //   mouse move  — rotates the selected bone (NO button required).
    //                  Horizontal motion spins around bone-local Y,
    //                  vertical motion around bone-local X. Per-frame
    //                  delta is post-multiplied onto the bone's quaternion
    //                  in S.bone_rot_deltas, which the skinning shader
    //                  AND the skeleton overlay both consume.
    //   LMB / R     — confirm and exit rotate mode (changes kept).
    //   RMB / ESC   — cancel; restore the snapshot and exit rotate mode.
    //
    // Snapshot lives in this function so it survives across frames within
    // a single rotate session and resets on the next R-press.
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
        // Apply mouse motion as a rotation delta. Hover-gated so cursor
        // wandering off the panel doesn't perturb the bone — the mouse
        // buttons that confirm/cancel are handled below WITHOUT the
        // hover gate so the user can click anywhere to commit.
        if (hovered) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            if (d.x != 0.0f || d.y != 0.0f) {
                const float kRotSensitivity = 0.01f;   // radians per pixel
                float a_y = d.x * kRotSensitivity;      // around bone-local Y
                float a_x = d.y * kRotSensitivity;      // around bone-local X

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
                // Post-multiply: extends the existing rotation by `delta`
                // in the bone's already-rotated local frame.
                XMVECTOR nxt = XMQuaternionNormalize(XMQuaternionMultiply(cur, delta));
                XMFLOAT4 nf;
                XMStoreFloat4(&nf, nxt);
                S.bone_rot_deltas[(size_t)b * 4 + 0] = nf.x;
                S.bone_rot_deltas[(size_t)b * 4 + 1] = nf.y;
                S.bone_rot_deltas[(size_t)b * 4 + 2] = nf.z;
                S.bone_rot_deltas[(size_t)b * 4 + 3] = nf.w;
            }
        }

        // Confirm / cancel buttons are NOT hover-gated — the user might
        // travel to a panel edge in the heat of editing.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            cancel_rotate();
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            confirm_rotate();
        }
    }

    // PICKING (skeleton visible, NOT in rotate mode): single-click on or
    // near a projected joint selects that bone. Click on empty area
    // clears the selection. Uses IsMouseClicked so drag-to-orbit still
    // works — pick fires at press, then orbit kicks in if the user
    // keeps dragging.
    if (skel_visible && !rotate_active && hovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImVec2 mp = ImGui::GetIO().MousePos;
        int picked = pick_bone_at(mp, origin, region, /*radius_px=*/12.0f);
        S.selected_bone = picked;
    }

    // ORBIT: original camera control, suppressed in rotate-mode.
    if (!rotate_active && active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        const float kOrbitSensitivity = 0.008f;
        S.cam_yaw   += d.x * kOrbitSensitivity;
        S.cam_pitch += d.y * kOrbitSensitivity;
        // Clamp pitch so we don't flip past straight-up / straight-down.
        const float kPitchLimit = 1.5f;        // ~85°
        if (S.cam_pitch >  kPitchLimit) S.cam_pitch =  kPitchLimit;
        if (S.cam_pitch < -kPitchLimit) S.cam_pitch = -kPitchLimit;
    }

    // Wheel to zoom (multiplicative — feels more natural than additive
    // because the perceived effect is constant in log-distance).
    if (hovered) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            S.cam_dist *= (wheel > 0.0f) ? 0.9f : 1.111f;
            if (S.cam_dist < 0.3f)  S.cam_dist = 0.3f;
            if (S.cam_dist > 50.0f) S.cam_dist = 50.0f;
        }
    }

    // R: enter rotate-mode (saving snapshot) or confirm-and-exit. Needs
    // skeleton visible AND a bone selected — otherwise it's a no-op so a
    // stray R press while the panel has focus doesn't engage anything.
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

    // Sync materials-overlay selection into per-mesh flags so MP_Render
    // sees the latest user choice. The radio behaviour is enforced when
    // the user toggles a checkbox below — here we just project the
    // single global index onto every submesh.
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

    // ESC: in rotate-mode it cancels (restores the snapshot). Outside
    // rotate-mode it closes the model as before.
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

    // Compact controls hint, top-left corner.
    dl->AddRectFilled(ImVec2(origin.x + 6, origin.y + 6),
                      ImVec2(origin.x + 196, origin.y + 70),
                      IM_COL32(20, 22, 28, 200), 4.0f);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 12));
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Controls");
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 30));
    ImGui::TextDisabled("L-Drag  rotate");
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 46));
    ImGui::TextDisabled("Wheel  zoom  /  ESC  close");

    // Track where the next overlay should be placed vertically — starts
    // just below the Controls box, advances past the skeleton overlay if
    // that one renders. Used by the Materials overlay below.
    float next_overlay_y = origin.y + 76.0f;

    // Skeleton overlay — only when the loaded MDL carries bone data. Sits
    // directly below the Controls hint, same width. Faded to ~30% alpha
    // when not hovered so it doesn't fight with the model behind it; full
    // alpha while the user is mousing over it.
    bool has_skeleton = S.mdl_info_ok &&
                        S.mdl_info.HasBoneTransforms &&
                        S.mdl_info.BoneCount > 0 &&
                        S.mdl_info.Bones.size() == S.mdl_info.BoneTransforms.size();
    if (has_skeleton) {
        // Visible state lives in the file-scope g_skel_overlay_show so
        // the input handlers above can read it without an extra round
        // trip. Default off so a fresh load doesn't slap rigging on top
        // of the user's first look at the mesh.
        // Smoothed alpha — eased toward 1.0 while hovered, 0.3 otherwise.
        // Keeps the fade tactile without strobing on edge crossings.
        static float s_skel_alpha    = 0.30f;

        const float kIdleAlpha   = 0.30f;
        const float kHoverAlpha  = 1.00f;

        const ImVec2 win_pos (origin.x + 6, origin.y + 76);
        const ImVec2 win_size(190, 0); // height auto-sized to contents
        ImGui::SetNextWindowPos(win_pos);
        ImGui::SetNextWindowSize(win_size, ImGuiCond_Always);
        // Match the Controls box's filled-bg look — same dark colour at
        // ~78% of the window's overall alpha so it sits on the same
        // visual layer.
        ImGui::SetNextWindowBgAlpha(s_skel_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_skel_alpha);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##skeleton_overlay", nullptr, fl)) {
            // Hover-test inside the window so the alpha eases up the
            // moment the cursor enters. ChildWindows flag covers the
            // checkbox's inner widgets.
            bool hovering = ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                ImGuiHoveredFlags_ChildWindows);
            float target = hovering ? kHoverAlpha : kIdleAlpha;
            // Exponential ease — feels natural, no settling jitter near
            // the target thanks to the snap inside ~0.5%.
            s_skel_alpha += (target - s_skel_alpha) * 0.18f;
            if (std::fabs(s_skel_alpha - target) < 0.005f) s_skel_alpha = target;

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Skeleton");
            ImGui::Checkbox("Show", &::g_skel_overlay_show);
            if (S.selected_bone >= 0 && S.selected_bone < (int)S.mdl_info.Bones.size()) {
                // Selected-bone hint inside the overlay — user-discoverable
                // R/RMB hotkeys without cluttering the empty-selection state.
                ImGui::TextDisabled(S.bone_rotate_mode
                                        ? "RMB cancel  /  LMB confirm"
                                        : "R: rotate selected");
            }
            // Capture the overlay's actual bottom so the Materials
            // overlay below can sit flush against it. Done inside Begin
            // so we read the current frame's auto-resized size.
            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            next_overlay_y = wp.y + ws.y + 6.0f;
        }
        ImGui::End();
        ImGui::PopStyleVar();

        // Hide picking and rotate-mode when the overlay is off.
        if (!::g_skel_overlay_show) {
            S.selected_bone     = -1;
            S.bone_rotate_mode  = false;
        }

        if (::g_skel_overlay_show) {
            draw_skeleton_overlay(origin, region);
        }
    } else {
        // No skeleton — make sure stale state from a previous model
        // doesn't leak into picking/rotation paths.
        ::g_skel_overlay_show = false;
        S.selected_bone       = -1;
        S.bone_rotate_mode    = false;
    }

    // ---- Wireframe overlay ----------------------------------------------
    // Single-checkbox overlay sitting between the Skeleton (if present)
    // and the Materials overlay below. Hover-fade matches the others.
    // Drives g_mp.wireframe, which MP_Render uses to swap the rasterizer
    // state between FILL_SOLID and FILL_WIREFRAME.
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

            // Capture bottom for the Materials overlay placement.
            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            next_overlay_y = wp.y + ws.y + 6.0f;
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    // ---- Materials overlay ----------------------------------------------
    // Sits beneath the skeleton overlay (or right under Controls if the
    // model has no skeleton). One section per submesh: name header,
    // Highlight + Isolate checkboxes, and clickable texture thumbnails.
    // Hover-fade alpha mirrors the skeleton overlay's behaviour.
    //
    // Highlight / Isolate are mutually exclusive across ALL submeshes —
    // a single global index per flag (g_highlight_mesh_idx /
    // g_isolate_mesh_idx) is the source of truth. Clicking a checkbox
    // sets/clears that index AND clears the other flag's index, so
    // ticking Highlight on submesh B automatically unticks Highlight on
    // submesh A and any active Isolate elsewhere.
    if (g_mp.has_model && !g_mp.meshes.empty()) {
        static float s_mat_alpha = 0.30f;
        const float kIdleAlpha   = 0.30f;
        const float kHoverAlpha  = 1.00f;

        // Width is fixed (the per-section row of thumbnails + checkboxes
        // is sized for this width). Height is dynamic: ImGui auto-fits
        // the window to its content, but a max-height constraint caps
        // the growth at whatever vertical space is left in the panel —
        // beyond that, the scrollbar takes over.
        const float kMatW = 296.0f;
        float max_h = std::max(160.0f,
                               region.y - (next_overlay_y - origin.y) - 20.0f);

        ImGui::SetNextWindowPos(ImVec2(origin.x + 6, next_overlay_y));
        ImGui::SetNextWindowSizeConstraints(ImVec2(kMatW, 0.0f),
                                            ImVec2(kMatW, max_h));
        ImGui::SetNextWindowBgAlpha(s_mat_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_mat_alpha);

        // AlwaysAutoResize asks ImGui to size the window to fit its
        // content; the size constraint above clamps that to [kMatW,
        // max_h] so a tall list doesn't overflow off-panel.
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
                auto& mesh = g_mp.meshes[mi];   // non-const so the
                                                // per-slot visibility
                                                // checkbox below can
                                                // write through it.
                ImGui::PushID((int)mi);

                // Submesh name header. Truncated rendering relies on the
                // window's clip rect — long names wrap into the next row.
                ImGui::TextUnformatted(mesh.name.c_str());

                bool h   = (::g_highlight_mesh_idx == (int)mi);
                bool iso = (::g_isolate_mesh_idx   == (int)mi);

                // Highlight: mutex with Isolate-on-anything AND with
                // Highlight on every other submesh. We model that by
                // single global indices.
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

                // Texture thumbnails — only render slots that have a
                // real (non-default) SRV with a known name. Click = open
                // popout, hover = tooltip with the texture name. Each
                // thumbnail also gets a visibility checkbox stacked
                // beneath it: when unchecked, MP_Render swaps the slot's
                // SRV for the white default so the user can isolate
                // which channel contributes what to the lit colour.
                struct ThumbSpec {
                    const char*               slot_id;
                    ID3D11ShaderResourceView* srv;
                    const std::string*        name;
                    bool*                     visible;  // -> mesh.<slot>_visible
                };
                ThumbSpec thumbs[4] = {
                    {"diffuse",  mesh.srv_diffuse,  &mesh.diffuse_tex_name,  &mesh.diffuse_visible},
                    {"normal",   mesh.srv_normal,   &mesh.normal_tex_name,   &mesh.normal_visible},
                    {"specular", mesh.srv_specular, &mesh.specular_tex_name, &mesh.specular_visible},
                    {"tint",     mesh.srv_tint,     &mesh.tint_tex_name,     &mesh.tint_visible},
                };
                bool any_thumb = false;
                for (int ti = 0; ti < 4; ++ti) {
                    const ThumbSpec& t = thumbs[ti];
                    if (!t.srv || t.srv == g_mp.default_srv) continue;
                    if (t.name->empty()) continue;
                    if (any_thumb) ImGui::SameLine();
                    any_thumb = true;
                    ImGui::PushID(t.slot_id);
                    // Group keeps the thumbnail + checkbox stacked so
                    // SameLine() between groups places the next slot's
                    // pair next to this one.
                    ImGui::BeginGroup();
                    // Disabled slots get a dim tint so the icon clearly
                    // reflects "this won't show in render".
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
                        // Remember which submesh this popout came from
                        // so the UV overlay can locate the right geom.
                        ::g_tex_popout_mesh_idx = (int)mi;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s\n[%s]",
                                          t.name->c_str(), t.slot_id);
                    }
                    // Visibility toggle directly under the thumbnail —
                    // anonymous label so only the checkbox glyph shows.
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
        // No model — clear materials state so a new load starts fresh.
        ::g_highlight_mesh_idx  = -1;
        ::g_isolate_mesh_idx    = -1;
        ::g_tex_popout_open     = false;
        ::g_tex_popout_srv      = nullptr;
        ::g_tex_popout_name.clear();
        ::g_tex_popout_mesh_idx = -1;
    }

    // ---- Texture popout window ------------------------------------------
    // Floating window showing whichever thumbnail the user last clicked.
    // Window auto-sizes to the texture's native dimensions and is locked
    // (NoResize) — the image displays at its true pixel size. Closes via
    // the X button on the title bar.
    if (::g_tex_popout_open && ::g_tex_popout_srv) {
        int tw = 0, th = 0;
        ID3D11Resource* res = nullptr;
        ::g_tex_popout_srv->GetResource(&res);
        if (res) {
            // SRV resource for our textures is always a Texture2D — same
            // creation path goes through CreateTexture2D + CSRV.
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
            // AlwaysAutoResize + NoResize: window snaps to fit content
            // (the image at native size + the UV checkbox above it),
            // and the user cannot drag-resize.
            ImGuiWindowFlags fl = ImGuiWindowFlags_NoCollapse
                                | ImGuiWindowFlags_NoResize
                                | ImGuiWindowFlags_AlwaysAutoResize;
            if (ImGui::Begin(title.c_str(), &::g_tex_popout_open, fl)) {
                // UV overlay toggle. The geom that owns this texture
                // was recorded in g_tex_popout_mesh_idx when the user
                // clicked the thumbnail. We only draw the overlay if
                // that index is still valid AND the source MDLMeshGeom
                // has UV/index data we can walk.
                ImGui::Checkbox("Show UVs", &::g_tex_popout_show_uvs);

                ImGui::Image((ImTextureID)::g_tex_popout_srv,
                             ImVec2((float)tw, (float)th));

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
                            // Soft white at moderate alpha — readable on
                            // most textures, doesn't drown the image.
                            const ImU32 col = IM_COL32(255, 255, 255, 200);
                            const float thickness = 1.0f;

                            // Each triangle = 3 indices. Convert UV
                            // (assumed [0,1] with V going down to match
                            // ImGui's Y-down convention) to screen px.
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
        // X-button close path — drop the SRV reference so the next load
        // doesn't accidentally reopen with a stale pointer.
        if (!::g_tex_popout_open) {
            ::g_tex_popout_srv = nullptr;
            ::g_tex_popout_name.clear();
        }
    }
}
#endif // _WIN32

} // namespace

#ifdef _WIN32
void draw_render_panel(ID3D11Device* device) {
    if (g_mp.has_model) {
        draw_model_in_panel(device);
    } else if (S.texture_window_srv) {
        draw_texture_in_panel();
    } else {
        draw_placeholder();
    }
}
#else
void draw_render_panel() {
    UI::draw_placeholder();
}
#endif

} // namespace UI
