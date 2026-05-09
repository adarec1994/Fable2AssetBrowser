#include "RenderPanel.h"
#include "../../Utilities/State.h"
#include "../ModelPreview.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#include <d3d11.h>
#include <windows.h>
#else
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#endif

extern ModelPreview g_mp;
extern bool g_mp_initialized;
extern FlyCam g_flycam;       // declared in ModelPreview.h with external linkage

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

    // Left-click drag to orbit. Active==true while the user is dragging
    // even if they wander outside the panel — that's intentional, so a
    // continuous rotation isn't broken by hand wobble crossing the edge.
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
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

    apply_orbit_to_flycam();
    MP_Render(device, g_mp, g_flycam);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (g_mp.srv) {
        dl->AddImage((ImTextureID)g_mp.srv,
                     origin,
                     ImVec2(origin.x + region.x, origin.y + region.y));
    }

    // ESC closes the model so we drop back to the placeholder. Only honor
    // it when we're hovered, so it doesn't stomp dialogs / popups.
    if (hovered && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        MP_Release(g_mp);
        g_mp.has_model = false;
        g_mp_initialized = false;
        S.show_model_preview = false;
        S.model_preview_open = false;
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
