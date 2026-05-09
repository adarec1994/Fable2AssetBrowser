#include "LoadingScreen.h"
#include "../UI_Panels.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace UI {

bool loading_in_progress() {
    // Show loading whenever a build has started but isn't finished yet.
    // (`tree_build_in_progress` is the "thread is running" signal;
    // `tree_build_finished` flips true once the UI thread has observed
    // the completion flag.)
    return tree_build_in_progress() && !tree_build_finished();
}

void draw_loading_screen() {
    ImVec2 size = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Solid panel background to match the eventual main layout palette.
    dl->AddRectFilled(origin,
                      ImVec2(origin.x + size.x, origin.y + size.y),
                      IM_COL32(20, 22, 28, 255));

    const float center_x = origin.x + size.x * 0.5f;
    const float center_y = origin.y + size.y * 0.5f;

    // ---- Title --------------------------------------------------------
    const char* title = "Loading BNKs";
    ImVec2 title_sz = ImGui::CalcTextSize(title);
    dl->AddText(ImVec2(center_x - title_sz.x * 0.5f, center_y - 70.0f),
                IM_COL32(220, 225, 235, 255), title);

    // ---- Progress bar (real percentage from the build thread) --------
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

    // ---- Percentage text + counter -----------------------------------
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

    // ---- Current item label ------------------------------------------
    std::string label = tree_build_current_label();
    if (!label.empty()) {
        // Clip to a sensible visible width so absurdly long labels
        // don't blow past the bar.
        const size_t kMaxLabelChars = 64;
        if (label.size() > kMaxLabelChars) {
            label = "..." + label.substr(label.size() - (kMaxLabelChars - 3));
        }
        ImVec2 lsz = ImGui::CalcTextSize(label.c_str());
        dl->AddText(ImVec2(center_x - lsz.x * 0.5f, bar_max.y + 32.0f),
                    IM_COL32(140, 150, 165, 255), label.c_str());
    }

    // ---- Slow-build hint (only kicks in for genuinely long jobs) -----
    if (tree_build_elapsed_seconds() > 15.0f && progress < 0.95f) {
        const char* hint = "(nested BNKs are extracted to temp on first scan - subsequent runs reuse the cache)";
        ImVec2 hsz = ImGui::CalcTextSize(hint);
        dl->AddText(ImVec2(center_x - hsz.x * 0.5f, bar_max.y + 60.0f),
                    IM_COL32(110, 120, 135, 255), hint);
    }

    ImGui::Dummy(size);
}

} // namespace UI
