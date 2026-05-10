#include "../UI_Panels.h"
#include "PanelInternal.h"
#include "../../Utilities/Utils.h"
#include "../../Utilities/operations.h"
#include "../UI_Main.h"
#include "../AudioPlayerWindow.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include <filesystem>
#include <algorithm>
#include <atomic>
#include <thread>

std::vector<GlobalHit> g_global_hits;
std::atomic<bool> g_global_busy(false);
std::atomic<bool> g_cancel_search(false);
std::string g_last_global_search;
int g_selected_global = -1;

void draw_file_table() {
    std::vector<int> vis;
    vis.reserve(S.files.size());
    for (size_t i = 0; i < S.files.size(); ++i)
        if (name_matches_filter(S.files[i].name, S.file_filter) &&
            name_matches_ext(S.files[i].name, S.ext_filter)) vis.push_back((int) i);

    auto get_filename = [](const std::string& path) -> std::string {
        size_t pos = path.find_last_of("/\\");
        if (pos != std::string::npos) return path.substr(pos + 1);
        return path;
    };

    auto is_mdl = [](const std::string& name) -> bool {
        std::string l = name;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        return l.size() >= 4 && l.rfind(".mdl") == l.size() - 4;
    };

    auto is_tex = [](const std::string& name) -> bool {
        std::string l = name;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        return l.size() >= 4 && l.rfind(".tex") == l.size() - 4;
    };

    ImGuiTable *tbl_ptr = nullptr;
    if (ImGui::BeginTable("files_table", 2,
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter |
                          ImGuiTableFlags_SizingStretchProp)) {
        tbl_ptr = ImGui::GetCurrentTable();
        ImGui::TableSetupColumn("File");
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin((int) vis.size());
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                int i = vis[r];
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                bool selected = (i == S.selected_file_index);
                std::string base = get_filename(S.files[i].name);
                if (ImGui::Selectable(base.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    S.selected_file_index = i;
                    if (is_mdl(S.files[i].name) && !S.viewing_adb && !S.viewing_lua) {
                        g_pending_mdl_load = true;
                        g_pending_mdl_index = i;
                    }
                    if (ImGui::IsMouseDoubleClicked(0) && is_tex(S.files[i].name)) {
                        g_pending_tex_load = true;
                        g_pending_tex_index = i;
                    }
                    // Double-click a .wav -> open the in-app audio player.
                    if (ImGui::IsMouseDoubleClicked(0) && is_audio_file(S.files[i].name)
                        && !S.viewing_adb && !S.viewing_lua) {
                        open_audio_player_for_selected(i);
                    }
                }
                // Right-click → "Hex View" (dev mode only) + "Export to"
                // for .tex. Skip for the ADB / Lua views since those use
                // special-cased decoders routed through draw_right_panel.
                if (!S.viewing_adb && !S.viewing_lua) {
                    bool is_nested = (S.selected_nested_index != -1 &&
                                      !S.selected_nested_temp_path.empty());
                    const std::string& bp = is_nested ? S.selected_nested_temp_path
                                                      : S.selected_bnk;
                    file_hex_context_menu(bp, S.files[i].index, is_nested,
                                          S.files[i].name);
                }
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(S.files[i].name.c_str());
                    ImGui::EndTooltip();
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%u", S.files[i].size);
                ImGui::PopID();
            }
        }
        clipper.End();
        ImGui::EndTable();
    }
    if (tbl_ptr) {
        ImRect r = tbl_ptr->OuterRect;
        ImU32 col = ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_Border));
        ImGui::GetWindowDrawList()->AddRect(r.Min, r.Max, col, 8.0f, 0, 1.0f);
    }
}

void draw_global_results_table() {
    if (g_global_busy) {
        ImGui::TextUnformatted("Searching all BNKs...");
        return;
    }

    auto get_filename = [](const std::string& path) -> std::string {
        size_t pos = path.find_last_of("/\\");
        if (pos != std::string::npos) return path.substr(pos + 1);
        return path;
    };

    auto is_mdl = [](const std::string& name) -> bool {
        std::string l = name;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        return l.size() >= 4 && l.rfind(".mdl") == l.size() - 4;
    };

    std::vector<int> vis;
    vis.reserve(g_global_hits.size());
    for (size_t i = 0; i < g_global_hits.size(); ++i) {
        if (name_matches_filter(g_global_hits[i].file_name, S.file_filter) &&
            name_matches_ext(g_global_hits[i].file_name, S.ext_filter)) {
            vis.push_back((int)i);
        }
    }

    ImGuiTable *tbl_ptr = nullptr;
    if (ImGui::BeginTable("global_results_table", 3,
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter |
                          ImGuiTableFlags_SizingStretchProp)) {
        tbl_ptr = ImGui::GetCurrentTable();
        ImGui::TableSetupColumn("File");
        ImGui::TableSetupColumn("BNK", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin((int)vis.size());
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                int i = vis[r];
                const auto& hit = g_global_hits[i];

                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);

                bool selected = (i == g_selected_global);
                std::string base = get_filename(hit.file_name);

                if (ImGui::Selectable(base.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    g_selected_global = i;
                    S.viewing_adb = false;
                    S.viewing_lua = false;
                    pick_bnk(hit.bnk_path);
                    for (size_t j = 0; j < S.files.size(); ++j) {
                        if (S.files[j].index == hit.index) {
                            S.selected_file_index = (int)j;
                            if (is_mdl(hit.file_name)) {
                                g_pending_mdl_load = true;
                                g_pending_mdl_index = (int)j;
                            }
                            break;
                        }
                    }
                }
                // Right-click → "Hex View" (dev mode only) + "Export to"
                // for .tex. A hit's BNK is "nested" if it's known in
                // nested_bnk_parents.
                {
                    bool is_nested = (S.nested_bnk_parents.count(hit.bnk_path) > 0);
                    file_hex_context_menu(hit.bnk_path, hit.index, is_nested,
                                          hit.file_name);
                }

                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(hit.file_name.c_str());
                    ImGui::EndTooltip();
                }

                ImGui::TableSetColumnIndex(1);
                std::string bnk_name = get_filename(hit.bnk_path);
                ImGui::TextUnformatted(bnk_name.c_str());

                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(hit.bnk_path.c_str());
                    ImGui::EndTooltip();
                }

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%u", hit.size);
                ImGui::PopID();
            }
        }
        clipper.End();
        ImGui::EndTable();
    }
    if (tbl_ptr) {
        ImRect r = tbl_ptr->OuterRect;
        ImU32 col = ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_Border));
        ImGui::GetWindowDrawList()->AddRect(r.Min, r.Max, col, 8.0f, 0, 1.0f);
    }
}
