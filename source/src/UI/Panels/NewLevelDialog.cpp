#include "NewLevelDialog.h"

#include "PanelInternal.h"
#include "../UI_Panels.h"
#include "../ContentTabs.h"
#include "../OutputLog.h"
#include "../../Level/Core/LevelLoader.h"
#include "../../Level/Creation/NewLevel.h"
#include "../../Utilities/Files.h"
#include "../../Utilities/State.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cctype>

namespace NewLevelDialog {

namespace {

bool        s_open_requested = false;
std::string s_name;
std::string s_pending_open_path;
std::string s_pending_open_name;

std::string norm(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    std::replace(s.begin(), s.end(), '/', '\\');
    return s;
}

void kick_reindex_and_open(const std::string& virtual_path,
                           const std::string& friendly) {
    S.bnk_paths = scan_bnks_recursive(S.root_dir);
    g_tree_last_root_dir.clear();
    g_tree_built.store(false);
    start_tree_build_for_root(S.root_dir, S.bnk_paths);
    s_pending_open_path = norm(virtual_path);
    s_pending_open_name = friendly;
}

void drive_pending_open() {
    if (s_pending_open_path.empty()) return;
    if (tree_build_in_progress() || !tree_build_finished()) return;
    if (Level::IsAsyncLoadInProgress()) return;

    const FlatAssetEntry* found = nullptr;
    for (const auto& e : S.all_level_files) {
        if (norm(e.full_path) == s_pending_open_path) found = &e;
    }
    const std::string friendly = s_pending_open_name;
    s_pending_open_path.clear();
    s_pending_open_name.clear();
    if (!found) {
        OutputLog::warn("new level: created but not found after re-index");
        return;
    }
    ContentTabs::OpenLevel(*found, friendly);
}

}

void Open() {
    s_open_requested = true;
}

void Draw() {
    if (s_open_requested) {
        ImGui::OpenPopup("New Level##f2ab_new_level");
        s_open_requested = false;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("New Level##f2ab_new_level", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(280.0f);
        ImGui::InputTextWithHint("Name", "e.g. my_island", &s_name);
        ImGui::Spacing();

        const bool busy = Level::IsAsyncLoadInProgress() ||
                          tree_build_in_progress();
        if (busy) ImGui::BeginDisabled();
        if (ImGui::Button("Create", ImVec2(120, 0))) {
            Level::Creation::NewLevelParams params;
            params.name = s_name;
            const Level::Creation::NewLevelResult res =
                Level::Creation::CreateNewLevel(params);
            if (res.ok) {
                
                
                for (const std::string& written : res.written_files) {
                    std::string low = norm(written);
                    if (low.size() >= 4 &&
                        low.compare(low.size() - 4, 4, ".bnk") == 0 &&
                        std::find(S.bnk_paths.begin(), S.bnk_paths.end(),
                                  written) == S.bnk_paths.end()) {
                        S.bnk_paths.push_back(written);
                    }
                }
                const FlatAssetEntry* found = nullptr;
                if (refresh_loose_file_index()) {
                    const std::string want =
                        norm(res.engine_level_virtual_path);
                    for (const auto& e : S.all_level_files) {
                        if (norm(e.full_path) == want) found = &e;
                    }
                }
                if (found) {
                    ContentTabs::OpenLevel(*found, s_name);
                } else {
                    kick_reindex_and_open(res.engine_level_virtual_path,
                                          s_name);
                }
                ImGui::CloseCurrentPopup();
            } else {
                OutputLog::error("new level: " + res.error);
            }
        }
        if (busy) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    drive_pending_open();
}

}
