#include "../UI_Panels.h"
#include "PanelInternal.h"
#include "../../Utilities/Utils.h"
#include "../../Utilities/Files.h"
#include "../../BNKCore.cpp"
#include "imgui.h"
#include <filesystem>
#include <algorithm>
#include <cstring>

#ifdef _WIN32
void draw_tree_node(TreeNode& node, ID3D11Device* device) {
#else
void draw_tree_node(TreeNode& node) {
#endif
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
    auto is_bnk = [](const std::string& name) -> bool {
        std::string l = name;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        return l.size() >= 4 && l.rfind(".bnk") == l.size() - 4;
    };

    if (node.is_file) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        bool selected = false;
        if (!S.viewing_adb && S.selected_bnk == node.bnk_source) {
            for (size_t i = 0; i < S.files.size(); ++i) {
                if (S.files[i].index == node.bnk_index) {
                    selected = (S.selected_file_index == (int)i);
                    break;
                }
            }
        }

        if (selected) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        std::string label = node.name;
        ImGui::TreeNodeEx(label.c_str(), flags);

        file_hex_context_menu(node.bnk_source, node.bnk_index,
                              node.is_nested_source, node.name);

        if (ImGui::IsItemClicked()) {
            S.selected_folder_path.clear();

            if (is_bnk(node.name)) {
                open_tree_bnk_drill_from_entry(node.bnk_source,
                                               node.bnk_index,
                                               node.full_path);
                return;
            }

            if (S.selected_bnk != node.bnk_source) {
                S.viewing_adb = false;
                S.global_search.clear();
                S.selected_nested_bnk.clear();
                S.selected_nested_index = -1;
                pick_bnk(node.bnk_source);
            }

            if (node.is_nested_source) {
                S.selected_nested_temp_path = node.bnk_source;
                S.selected_nested_index = 0;
            }

            for (size_t i = 0; i < S.files.size(); ++i) {
                if (S.files[i].index == node.bnk_index) {
                    S.selected_file_index = (int)i;

                    if (is_mdl(node.name)) {
                        S.show_gdb_render = false;
                        g_pending_mdl_full_path = node.full_path;
                        g_pending_mdl_load = true;
                        g_pending_mdl_index = (int)i;
                    }
                    if (is_tex(node.name)) {
                        S.show_gdb_render = false;
                        g_pending_tex_load = true;
                        g_pending_tex_index = (int)i;
                    }
                    if (is_gdb_file(node.name)) {
                        open_gdb_viewer_for_bnk_entry(
                            node.bnk_source, node.bnk_index, node.name);
                    }

                    if (is_audio_file(node.name) && ImGui::IsMouseDoubleClicked(0)) {
                        S.show_gdb_render = false;
                        open_audio_player_for_selected((int)i);
                    }
                    break;
                }
            }
        }

        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();

            if (S.dev_mode) {
                ImGui::Text("%s", node.full_path.c_str());
                ImGui::Text("Size: %u bytes", node.file_size);
                ImGui::Text("BNK: %s",
                    std::filesystem::path(node.bnk_source).filename().string().c_str());
            } else {
                ImGui::TextUnformatted(node.name.c_str());
            }
            ImGui::EndTooltip();
        }
    } else {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;

        if (node.children.empty()) {
            flags |= ImGuiTreeNodeFlags_Leaf;
        }

        bool node_open = ImGui::TreeNodeEx(node.name.c_str(), flags);

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            S.selected_file_index = -1;
            S.selected_folder_path = node.name;
        }

        if (node_open) {
            std::vector<std::pair<std::string, TreeNode*>> sorted_children;
            for (auto& pair : node.children) {
                sorted_children.push_back({pair.first, &pair.second});
            }

            std::sort(sorted_children.begin(), sorted_children.end(),
                [](const auto& a, const auto& b) {
                    if (a.second->is_file != b.second->is_file) {
                        return !a.second->is_file;
                    }
                    return a.first < b.first;
                });

            for (auto& pair : sorted_children) {
#ifdef _WIN32
                draw_tree_node(*pair.second, device);
#else
                draw_tree_node(*pair.second);
#endif
            }

            ImGui::TreePop();
        }
    }
}
