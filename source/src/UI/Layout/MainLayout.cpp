#include "MainLayout.h"
#include "RenderPanel.h"
#include "../UI_Panels.h"

#include "imgui.h"

#ifdef _WIN32
#include <d3d11.h>
#endif

namespace UI {

namespace {

// TT-Lab-style two-column layout:
//   [ content area, flexible width ] [ tree panel, ~20% width fixed ]
// Sidebar (logo column) was dropped — TT-Lab doesn't have one and the
// menu bar carries the app branding now.
constexpr float kRightPanelWidth = 300.0f;

} // namespace

#ifdef _WIN32
void draw_main_layout(ID3D11Device* device) {
#else
void draw_main_layout() {
#endif
    ImVec2 region = ImGui::GetContentRegionAvail();

    // The tree panel is anchored to the right and has a fixed width; the
    // content area takes everything that's left.
    float content_w = region.x - kRightPanelWidth;
    if (content_w < 200.0f) content_w = 200.0f;

    // ---- Left/Center: render panel (model preview / texture / placeholder)
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(20, 22, 28, 255));
    ImGui::BeginChild("##layout_render", ImVec2(content_w, region.y),
                      false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
#ifdef _WIN32
    draw_render_panel(device);
#else
    draw_render_panel();
#endif
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SameLine(0.0f, 0.0f);

    // ---- Right: project-tree groupbox-equivalent (tabs [Tree | Banks]) ----
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(28, 30, 36, 255));
    ImGui::BeginChild("##layout_tree", ImVec2(0, region.y),
                      false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
#ifdef _WIN32
    draw_left_panel(device);
#else
    draw_left_panel();
#endif
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

} // namespace UI
