#include "../UI_Panels.h"
#include "PanelInternal.h"
#include "../../Utilities/Utils.h"
#include "../../Utilities/Files.h"
#include "../../Utilities/Progress.h"
#include "../../textures/export/TextureExport.h"
#include "../OutputLog.h"
#include "../../BNKCore.cpp"
#include "../../Lua.h"
#include "../ModelPreview.h"
#include "imgui.h"
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <thread>
#include <vector>

extern std::atomic<bool> g_pending_mdl_load;
extern std::atomic<bool> g_pending_tex_load;
extern int               g_pending_mdl_index;
extern int               g_pending_tex_index;
extern std::string       g_pending_mdl_full_path;
extern ModelPreview      g_mp;

namespace {

struct TreeTexExportTarget {
    std::string full_path;
    std::string bnk_source;
};

void collect_tree_textures(const TreeNode& node,
                           std::vector<TreeTexExportTarget>& out)
{
    if (node.is_file) {
        if (is_tex_file(node.name) && !node.full_path.empty() &&
            !node.bnk_source.empty()) {
            out.push_back({node.full_path, node.bnk_source});
        }
        return;
    }

    for (const auto& child : node.children) {
        collect_tree_textures(child.second, out);
    }
}

const char* tree_tex_format_name(TexExportFormat fmt)
{
    switch (fmt) {
        case TexExportFormat::PNG:  return "PNG";
        case TexExportFormat::JPG:  return "JPG";
        case TexExportFormat::TIFF: return "TIFF";
        case TexExportFormat::DDS:  return "DDS";
        case TexExportFormat::TEX:  return "TEX";
    }
    return "?";
}

void export_tree_textures_as(std::vector<TreeTexExportTarget> targets,
                             TexExportFormat fmt,
                             const std::string& folder_name)
{
    if (targets.empty()) {
        OutputLog::warn("File tree texture export: no .tex files under " +
                        folder_name);
        return;
    }

    const int total = static_cast<int>(targets.size());
    const std::string export_root =
        S.export_dir.empty() ? std::filesystem::absolute(".").string()
                             : S.export_dir;
    const std::string fmt_name = tree_tex_format_name(fmt);

    OutputLog::info("File tree: exporting " + std::to_string(total) +
                    " texture(s) under '" + folder_name + "' as " +
                    fmt_name + " -> " + export_root);
    progress_open(total,
                  "Exporting tree textures as " + fmt_name +
                  " -> " + export_root);
    progress_update(0, total, "Starting...");

    std::thread([targets = std::move(targets), total, fmt, fmt_name]() {
        struct ProgressGuard {
            ~ProgressGuard() { progress_done(); }
        } guard;

        int done = 0;
        for (const auto& target : targets) {
            if (S.cancel_requested.load() || S.exiting.load()) break;

            try {
                tex_export_begin_named(fmt, target.full_path,
                                       target.bnk_source, 0);
            } catch (const std::exception& ex) {
                OutputLog::error("Tree texture export exception (" +
                                 target.full_path + "): " + ex.what());
            } catch (...) {
                OutputLog::error("Tree texture export exception (" +
                                 target.full_path + ")");
            }

            ++done;
            progress_update(done, total,
                            std::filesystem::path(target.full_path)
                                .filename().string());
        }

        if (S.cancel_requested.load()) {
            OutputLog::warn("Tree texture export cancelled (" +
                            std::to_string(done) + "/" +
                            std::to_string(total) + ").");
            S.cancel_requested = false;
            return;
        }

        OutputLog::success("Tree texture export complete as " +
                           fmt_name + ": " + std::to_string(done) +
                           "/" + std::to_string(total) + " texture(s).");
    }).detach();
}

void tree_texture_export_context_menu(TreeNode& node)
{
    if (!ImGui::BeginPopupContextItem()) return;

    std::vector<TreeTexExportTarget> targets;
    collect_tree_textures(node, targets);

    if (targets.empty()) {
        ImGui::TextDisabled("No textures in this folder");
    } else {
        const std::string menu_label =
            "Export textures (" + std::to_string(targets.size()) + ")";
        if (ImGui::BeginMenu(menu_label.c_str())) {
            if (ImGui::MenuItem("PNG")) {
                export_tree_textures_as(targets, TexExportFormat::PNG,
                                        node.name);
            }
            if (ImGui::MenuItem("JPG")) {
                export_tree_textures_as(targets, TexExportFormat::JPG,
                                        node.name);
            }
            if (ImGui::MenuItem("TIFF")) {
                export_tree_textures_as(targets, TexExportFormat::TIFF,
                                        node.name);
            }
            if (ImGui::MenuItem("DDS")) {
                export_tree_textures_as(targets, TexExportFormat::DDS,
                                        node.name);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(".tex (raw)")) {
                export_tree_textures_as(targets, TexExportFormat::TEX,
                                        node.name);
            }
            ImGui::EndMenu();
        }
    }

    ImGui::EndPopup();
}

}

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
    auto is_lua = [](const std::string& name) -> bool {
        std::string l = name;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        return l.size() >= 4 && l.rfind(".lua") == l.size() - 4;
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
                    if (is_lua(node.name)) {
                        g_pending_mdl_load = false;
                        g_pending_tex_load = false;
                        g_pending_mdl_index = -1;
                        g_pending_tex_index = -1;
                        g_pending_mdl_full_path.clear();
#ifdef _WIN32
                        if (g_mp.has_model) MP_Release(g_mp);
                        g_mp.has_model = false;
                        if (S.texture_window_srv) {
                            S.texture_window_srv->Release();
                            S.texture_window_srv = nullptr;
                        }
                        S.texture_window_width  = 0;
                        S.texture_window_height = 0;
#else
                        g_mp.has_model = false;
#endif
                        std::string bnk_path  = node.bnk_source;
                        std::string lua_title = node.name;
                        int         bnk_index = node.bnk_index;
                        S.lua_preview_selected = (int)i;
                        S.lua_preview_title    = lua_title;
                        S.lua_preview_content.clear();
                        S.lua_preview_loading  = true;
                        S.show_lua_render      = true;
                        S.show_gdb_render      = false;
                        OutputLog::info("Decompiling Lua: " + lua_title);
                        progress_open(0, "Decompiling " + lua_title + "...");
                        std::thread([bnk_path, bnk_index, lua_title]() {
                            std::string content;
                            try {
                                auto bytes = BnkCache::extract_bytes(bnk_path, bnk_index);
                                if (bytes.empty()) {
                                    content = "-- Error: empty entry";
                                } else if (bytes.size() > 10 * 1024 * 1024) {
                                    content = "-- Error: File too large to preview (>10MB)";
                                } else {
                                    bool is_bytecode =
                                        bytes.size() >= 4 &&
                                        bytes[0] == 0x1B && bytes[1] == 'L' &&
                                        bytes[2] == 'u' && bytes[3] == 'a';
                                    if (is_bytecode) {
                                        content = decompile_lua51_bytecode(
                                            bytes.data(), bytes.size());
                                    } else {
                                        content.assign(bytes.begin(), bytes.end());
                                    }
                                }
                            } catch (const std::exception& ex) {
                                content = std::string("-- Error: ") + ex.what();
                            } catch (...) {
                                content = "-- Error: extracting lua bytes failed";
                            }
                            S.lua_preview_content = content;
                            S.lua_preview_loading = false;
                            progress_done();
                        }).detach();
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
        tree_texture_export_context_menu(node);

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
