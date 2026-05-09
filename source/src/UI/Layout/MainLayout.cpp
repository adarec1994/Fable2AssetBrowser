#include "MainLayout.h"
#include "Sidebar.h"
#include "../UI_Panels.h"

#include "imgui.h"

#ifdef _WIN32
#include <d3d11.h>
#endif

namespace UI {

namespace {

constexpr float kMiddleColumnWidth = 380.0f;

} // namespace

#ifdef _WIN32
void draw_main_layout(ID3D11Device* device) {
#else
void draw_main_layout() {
#endif
    ImVec2 region = ImGui::GetContentRegionAvail();

    // ---- Left: branding sidebar (fixed 180 px) -----------------------------
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::BeginChild("##layout_sidebar", ImVec2(kSidebarWidth, region.y),
                      false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
#ifdef _WIN32
    draw_sidebar(device);
#else
    draw_sidebar();
#endif
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::SameLine(0.0f, 0.0f);

    // ---- Middle: tabs [File Tree | BNK List] ------------------------------
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(28, 30, 36, 255));
    ImGui::BeginChild("##layout_middle", ImVec2(kMiddleColumnWidth, region.y),
                      false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
#ifdef _WIN32
    draw_left_panel(device);
#else
    draw_left_panel();
#endif
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SameLine(0.0f, 0.0f);

    // ---- Right: content (file table + previews + action buttons) -----------
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(20, 22, 28, 255));
    ImGui::BeginChild("##layout_right", ImVec2(0, region.y),
                      false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
#ifdef _WIN32
    draw_right_panel(device);
#else
    draw_right_panel();
#endif
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

} // namespace UI
