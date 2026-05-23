#include "LoadingScreen.h"
#include "../UI_Panels.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace UI {

bool loading_in_progress() {

    return tree_build_in_progress() && !tree_build_finished();
}

void draw_loading_screen() {
    ImVec2 size = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(origin,
                      ImVec2(origin.x + size.x, origin.y + size.y),
                      IM_COL32(20, 22, 28, 255));

    const float center_x = origin.x + size.x * 0.5f;
    const float center_y = origin.y + size.y * 0.5f;

    const char* title = "Loading Assets";
    ImVec2 title_sz = ImGui::CalcTextSize(title);
    dl->AddText(ImVec2(center_x - title_sz.x * 0.5f, center_y - 70.0f),
                IM_COL32(220, 225, 235, 255), title);

    const float bar_w = 360.0f;
    const float bar_h = 8.0f;
    ImVec2 bar_min(center_x - bar_w * 0.5f, center_y - 20.0f);
    ImVec2 bar_max(bar_min.x + bar_w,        bar_min.y + bar_h);
    dl->AddRectFilled(bar_min, bar_max, IM_COL32(40, 44, 52, 255), bar_h * 0.5f);

    float progress = tree_build_progress();
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    float fill_x = bar_min.x + (bar_max.x - bar_min.x) * progress;
    if (fill_x > bar_min.x + 1.0f) {
        dl->AddRectFilled(bar_min, ImVec2(fill_x, bar_max.y),
                          IM_COL32(120, 200, 255, 255), bar_h * 0.5f);
    }

    char pct[64];
    int done  = tree_build_done_units();
    int total = tree_build_total_units();
    if (total > 0) {
        std::snprintf(pct, sizeof(pct), "%d%%   (%d / %d)",
                      (int)(progress * 100.0f + 0.5f), done, total);
    } else {
        std::snprintf(pct, sizeof(pct), "scanning...");
    }
    ImVec2 pct_sz = ImGui::CalcTextSize(pct);
    dl->AddText(ImVec2(center_x - pct_sz.x * 0.5f, bar_max.y + 10.0f),
                IM_COL32(200, 210, 225, 255), pct);

    std::string label = tree_build_current_label();
    if (!label.empty()) {

        const size_t kMaxLabelChars = 64;
        if (label.size() > kMaxLabelChars) {
            label = "..." + label.substr(label.size() - (kMaxLabelChars - 3));
        }
        ImVec2 lsz = ImGui::CalcTextSize(label.c_str());
        dl->AddText(ImVec2(center_x - lsz.x * 0.5f, bar_max.y + 32.0f),
                    IM_COL32(140, 150, 165, 255), label.c_str());
    }

    if (tree_build_elapsed_seconds() > 15.0f && progress < 0.95f) {
        const char* hint = "(nested BNKs and animation names can take a moment on first scan)";
        ImVec2 hsz = ImGui::CalcTextSize(hint);
        dl->AddText(ImVec2(center_x - hsz.x * 0.5f, bar_max.y + 60.0f),
                    IM_COL32(110, 120, 135, 255), hint);
    }

    ImGui::Dummy(size);
}

}
