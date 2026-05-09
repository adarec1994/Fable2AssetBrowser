#include "RenderPanel.h"
#include "../../Utilities/State.h"
#include "../ModelPreview.h"

#include "imgui.h"

#include <algorithm>

#ifdef _WIN32
#include <d3d11.h>
#include <windows.h>
#else
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#endif

// Globals owned by UI_Main.cpp — the model preview state and the fly-cam
// used to navigate it. Declared extern so the render panel can drive
// rendering without requiring a refactor that moves them.
extern ModelPreview g_mp;
namespace { /* defined in UI_Main.cpp; re-declare here */ }
extern bool g_mp_initialized;

struct FlyCam;            // forward decl matches UI_Main's static
extern FlyCam g_flycam;

// Forward declaration of the input handler. The implementation in
// UI_Main.cpp is `static`, so we duplicate a thin wrapper here that
// applies camera updates only when the render panel is hovered.
void render_panel_handle_flycam(float dt);

namespace UI {

namespace {

// Draw a centered placeholder when nothing's loaded. Matches the panel's
// dark palette so it doesn't fight the rest of the UI for attention.
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

    // Background fill so any unfilled corners (from aspect-ratio fit) match
    // the rest of the dark panel.
    dl->AddRectFilled(origin,
                      ImVec2(origin.x + region.x, origin.y + region.y),
                      IM_COL32(20, 22, 28, 255));

    if (!S.texture_window_srv || S.texture_window_width <= 0 || S.texture_window_height <= 0) {
        // Decode failed — show the error name from the texture window state.
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

    // Aspect-fit the texture into the panel.
    float tw = (float)S.texture_window_width;
    float th = (float)S.texture_window_height;
    float scale = std::min(region.x / tw, region.y / th);
    if (scale > 4.0f) scale = 4.0f;          // don't upscale beyond 4x
    float dw = tw * scale;
    float dh = th * scale;
    float x0 = origin.x + (region.x - dw) * 0.5f;
    float y0 = origin.y + (region.y - dh) * 0.5f;
    dl->AddImage((ImTextureID)S.texture_window_srv,
                 ImVec2(x0, y0),
                 ImVec2(x0 + dw, y0 + dh));

    // Tiny info overlay in the top-left corner.
    char info[128];
    std::snprintf(info, sizeof(info), "%dx%d", S.texture_window_width, S.texture_window_height);
    dl->AddText(ImVec2(origin.x + 8, origin.y + 6),
                IM_COL32(180, 190, 205, 220), info);

    ImGui::Dummy(region);
}

void draw_model_in_panel(ID3D11Device* device, float dt) {
    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    int w = std::max(1, (int)region.x);
    int h = std::max(1, (int)region.y);

    if (!g_mp_initialized) {
        MP_Init(device, g_mp, w, h);
        g_mp_initialized = true;
    }
    MP_Resize(device, g_mp, w, h);
    MP_Render(device, g_mp, g_flycam);

    // Reserve the area as an item so we can hover-test for camera input.
    ImGui::InvisibleButton("##model_render", region);
    bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (g_mp.srv) {
        dl->AddImage((ImTextureID)g_mp.srv,
                     origin,
                     ImVec2(origin.x + region.x, origin.y + region.y));
    }

    // Camera input only when this panel is the focus of attention.
    if (hovered) {
        render_panel_handle_flycam(dt);
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

    // Camera-controls hint, top-left.
    dl->AddRectFilled(ImVec2(origin.x + 6, origin.y + 6),
                      ImVec2(origin.x + 220, origin.y + 132),
                      IM_COL32(20, 22, 28, 200), 4.0f);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 12));
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Camera Controls");
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 30));
    ImGui::TextDisabled("W/S — Forward/Back");
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 46));
    ImGui::TextDisabled("A/D — Strafe");
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 62));
    ImGui::TextDisabled("Q/E — Down/Up");
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 78));
    ImGui::TextDisabled("R-Click — Look");
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 94));
    ImGui::TextDisabled("ESC — Close model");
}
#endif // _WIN32

} // namespace

#ifdef _WIN32
void draw_render_panel(ID3D11Device* device) {
    float dt = ImGui::GetIO().DeltaTime;

    // Priority: 3D model > texture > placeholder.
    if (g_mp.has_model) {
        draw_model_in_panel(device, dt);
    } else if (S.texture_window_srv) {
        draw_texture_in_panel();
    } else {
        draw_placeholder();
    }
}
#else
void draw_render_panel() {
    // Non-Windows path is intentionally minimal — texture/model rendering
    // backends differ enough that the original full-screen path was
    // GL-only too.
    UI::draw_placeholder();
}
#endif

} // namespace UI
