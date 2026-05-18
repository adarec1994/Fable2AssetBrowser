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

float g_right_panel_width = 300.0f;
constexpr float kMaxRightPanelWidth = 800.0f;
constexpr float kSplitterWidth      = 4.0f;

float min_right_panel_width() {
    return std::max(60.0f, left_panel_min_width());
}

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

        g_right_panel_width -= ImGui::GetIO().MouseDelta.x;
        g_right_panel_width = std::clamp(g_right_panel_width,
                                         min_right_panel_width(),
                                         kMaxRightPanelWidth);
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
}

}

#ifdef _WIN32
void draw_main_layout(ID3D11Device* device, float bottom_overlay) {
#else
void draw_main_layout(float bottom_overlay) {
#endif
    ImVec2 region = ImGui::GetContentRegionAvail();

    const float min_w = min_right_panel_width();
    g_right_panel_width = std::clamp(g_right_panel_width,
                                     min_w, kMaxRightPanelWidth);

    float content_w = region.x - g_right_panel_width - kSplitterWidth;
    if (content_w < 200.0f) {
        content_w = 200.0f;
        g_right_panel_width = std::max(min_w,
                                       region.x - content_w - kSplitterWidth);
    }

    if (bottom_overlay < 0.0f) bottom_overlay = 0.0f;
    const float right_h = std::max(50.0f, region.y - bottom_overlay);

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
    draw_splitter(region.y);
    ImGui::SameLine(0.0f, 0.0f);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(28, 30, 36, 255));
    ImGui::BeginChild("##layout_tree", ImVec2(0, right_h),
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

}
