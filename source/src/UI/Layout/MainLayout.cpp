#include "MainLayout.h"
#include "RenderPanel.h"
#include "../UI_Panels.h"

#include "imgui.h"
#include <algorithm>

#ifdef _WIN32
#include <d3d11.h>
#endif

namespace UI {

namespace {

// User-resizable width of the right tree panel. Persisted only for the
// session — if you want it remembered across runs, push the value into
// the settings file like font_size / show_paths.
float g_right_panel_width = 300.0f;
constexpr float kMinRightPanelWidth = 180.0f;
constexpr float kMaxRightPanelWidth = 800.0f;
constexpr float kSplitterWidth      = 4.0f;

// Draggable vertical splitter handle. Eats horizontal mouse drag and
// updates `g_right_panel_width` (the tree column shrinks as the splitter
// moves right). 4 px wide, the cursor turns into the resize-EW arrow on
// hover so it's discoverable.
void draw_splitter(float region_y) {
    ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(40, 44, 52, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(80, 140, 180, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(120, 200, 255, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    ImGui::Button("##splitter", ImVec2(kSplitterWidth, region_y));

    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (ImGui::IsItemActive()) {
        // Mouse moved right by dx → tree column shrinks by dx.
        g_right_panel_width -= ImGui::GetIO().MouseDelta.x;
        g_right_panel_width = std::clamp(g_right_panel_width,
                                         kMinRightPanelWidth, kMaxRightPanelWidth);
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
}

} // namespace

#ifdef _WIN32
void draw_main_layout(ID3D11Device* device) {
#else
void draw_main_layout() {
#endif
    ImVec2 region = ImGui::GetContentRegionAvail();

    // Tree on the right is fixed-width (user-draggable); content takes
    // the rest minus the splitter handle.
    float content_w = region.x - g_right_panel_width - kSplitterWidth;
    if (content_w < 200.0f) {
        content_w = 200.0f;
        g_right_panel_width = std::max(kMinRightPanelWidth,
                                       region.x - content_w - kSplitterWidth);
    }

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

    // ---- Splitter (4 px) ---------------------------------------------------
    ImGui::SameLine(0.0f, 0.0f);
    draw_splitter(region.y);
    ImGui::SameLine(0.0f, 0.0f);

    // ---- Right: tabs [Tree | Banks | …] ------------------------------------
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
