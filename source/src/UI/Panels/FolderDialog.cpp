#include "../UI_Panels.h"
#include "../../Utilities/State.h"
#include "imgui.h"
#include "ImGuiFileDialog.h"

void draw_folder_dialog() {
    ImVec2 vp = ImGui::GetMainViewport()->WorkSize;
    ImVec2 minSize(680, 440);
    ImVec2 maxSize(vp.x * 0.9f, vp.y * 0.9f);
    if (ImGuiFileDialog::Instance()->Display("PickDir", ImGuiWindowFlags_NoCollapse, minSize, maxSize)) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string sel = ImGuiFileDialog::Instance()->GetCurrentPath();
            open_folder_logic(sel);
        }
        ImGuiFileDialog::Instance()->Close();
    }
}
