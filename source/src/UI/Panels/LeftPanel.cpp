#include "../UI_Panels.h"
#include "PanelInternal.h"
#include "../UI_Main.h"
#include "../../Utilities/Utils.h"
#include "../../BNKCore.cpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include <filesystem>
#include <algorithm>
#include <unordered_map>
#include <cstring>

// Click-to-load handler shared by the Models / Textures tabs. Mirrors the
// equivalent block inside draw_tree_node so a click in either tab gets the
// same end result: switch the active BNK if needed, restore nested-source
// state, locate the matching entry inside S.files, then arm the pending-load
// flag the central render panel consumes.
//
// kind: 0 = .mdl, 1 = .tex, 2 = .wav (audio player)
void load_flat_asset_entry(const FlatAssetEntry& e, int kind) {
    if (S.selected_bnk != e.bnk_path) {
        S.viewing_adb = false;
        S.global_search.clear();
        S.selected_nested_bnk.clear();
        S.selected_nested_index = -1;
        pick_bnk(e.bnk_path);
    }
    // pick_bnk wipes selected_nested_temp_path — re-establish it for nested
    // sources so subsequent extract paths know which nested BNK to pull from.
    if (e.from_nested) {
        S.selected_nested_temp_path = e.bnk_path;
        S.selected_nested_index = 0;
    }
    for (size_t i = 0; i < S.files.size(); ++i) {
        if (S.files[i].index == e.file_index) {
            S.selected_file_index = (int)i;
            if (kind == 0) {
                g_pending_mdl_full_path = e.full_path;
                g_pending_mdl_load = true;
                g_pending_mdl_index = (int)i;
            } else if (kind == 1) {
                g_pending_tex_load = true;
                g_pending_tex_index = (int)i;
            } else if (kind == 2) {
                // Audio: open the in-app audio player on the selected
                // file. Same code path the file-table double-click uses.
                open_audio_player_for_selected((int)i);
            }
            break;
        }
    }
}

#ifdef _WIN32
void draw_left_panel(ID3D11Device* device) {
#else
void draw_left_panel() {
#endif
    // Fill the parent container — the column width is now decided by the
    // outer MainLayout, not hardcoded here.
    ImGui::BeginChild("left_panel", ImVec2(0, 0), true);

    // ImGui's TabBar can't render across two rows, so we roll our own
    // tab strip out of regular Buttons styled with the Tab/TabActive
    // palette. Selection is exclusive across both rows — clicking a row
    // 2 button visually deselects row 1's active tab and vice versa.
    // s_active_tab values: 0 BNK List, 1 File Tree, 2 Models, 3 Textures,
    // 4 Audio. Default 1 puts the user on File Tree at startup.
    static int s_active_tab = 1;

    auto tab_button = [](const char* label, bool active, ImU32 text_col = 0) -> bool {
        const ImGuiStyle& st = ImGui::GetStyle();
        const ImVec4 bg     = st.Colors[active ? ImGuiCol_TabActive : ImGuiCol_Tab];
        const ImVec4 hov    = st.Colors[ImGuiCol_TabHovered];
        const ImVec4 act    = st.Colors[ImGuiCol_TabActive];
        ImGui::PushStyleColor(ImGuiCol_Button,        bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  act);
        if (text_col) ImGui::PushStyleColor(ImGuiCol_Text, text_col);
        bool clicked = ImGui::Button(label);
        if (text_col) ImGui::PopStyleColor();
        ImGui::PopStyleColor(3);
        return clicked;
    };

    // Row 1 — standard tabs.
    if (tab_button("BNK List", s_active_tab == 0))   s_active_tab = 0;
    ImGui::SameLine(0, 2);
    if (tab_button("File Tree", s_active_tab == 1))  s_active_tab = 1;

    // Row 2 — flat asset views, purple labels to set them apart.
    const ImU32 kPurpleLabel = IM_COL32(200, 130, 255, 255);
    if (tab_button("Models",   s_active_tab == 2, kPurpleLabel)) s_active_tab = 2;
    ImGui::SameLine(0, 2);
    if (tab_button("Textures", s_active_tab == 3, kPurpleLabel)) s_active_tab = 3;
    ImGui::SameLine(0, 2);
    if (tab_button("Audio",    s_active_tab == 4, kPurpleLabel)) s_active_tab = 4;
    ImGui::SameLine(0, 2);
    if (tab_button("Animations", s_active_tab == 5, kPurpleLabel)) s_active_tab = 5;

    ImGui::Separator();

    // Lambda used by the Models / Textures / Audio bodies further down.
    // Defined up here so all three branches can share it without the
    // pre-refactor double-tab-bar plumbing.
    auto draw_flat_asset_tab = [](const char* /*label*/,
                                  std::vector<FlatAssetEntry>& entries,
                                  std::string& filter,
                                  const char* child_id,
                                  int kind) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint(("##" + std::string(child_id) + "_filter").c_str(),
                                 "Filter", &filter);

        std::vector<int> vis;
        vis.reserve(entries.size());
        std::string flow = filter;
        std::transform(flow.begin(), flow.end(), flow.begin(), ::tolower);
        for (size_t i = 0; i < entries.size(); ++i) {
            if (flow.empty()) {
                vis.push_back((int)i);
            } else {
                std::string nlow = entries[i].name;
                std::transform(nlow.begin(), nlow.end(), nlow.begin(), ::tolower);
                if (nlow.find(flow) != std::string::npos) vis.push_back((int)i);
            }
        }

        if (S.dev_mode) {
            ImGui::TextDisabled("%d / %zu", (int)vis.size(), entries.size());
            ImGui::Separator();
        }

        ImGui::BeginChild(child_id, ImVec2(0, 0), false);
        ImGuiListClipper clipper;
        clipper.Begin((int)vis.size());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const FlatAssetEntry& e = entries[(size_t)vis[(size_t)row]];
                ImGui::PushID(row);
                bool selected = (S.selected_bnk == e.bnk_path &&
                                 S.selected_file_index >= 0 &&
                                 S.selected_file_index < (int)S.files.size() &&
                                 S.files[(size_t)S.selected_file_index].index == e.file_index);
                if (ImGui::Selectable(e.name.c_str(), selected,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    load_flat_asset_entry(e, kind);
                }
                // Right-click → "Hex View" (dev mode only) + "Export to" for .tex.
                file_hex_context_menu(e.bnk_path, e.file_index,
                                      e.from_nested, e.name);
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    if (S.dev_mode) {
                        ImGui::TextUnformatted(e.full_path.c_str());
                        ImGui::Text("Size: %u bytes", e.size);
                        ImGui::Text("BNK: %s",
                            std::filesystem::path(e.bnk_path).filename().string().c_str());
                        if (e.from_nested) ImGui::TextDisabled("(nested)");
                    } else {
                        ImGui::TextUnformatted(e.name.c_str());
                    }
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
        }
        clipper.End();
        ImGui::EndChild();
    };

    if (s_active_tab == 0) {
            ImGui::SetNextItemWidth(-1);
            if (!S.bnk_paths.empty()) {
                ImGui::InputTextWithHint("##bnk_filter", "Filter", &S.bnk_filter);
            }
            ImGui::BeginChild("bnk_list", ImVec2(0, 0), false);

            auto paths = filtered_bnk_paths();

            if (!S.adb_paths.empty()) {
                ImGui::PushID("adb_entry");
                bool selected = S.viewing_adb;
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                if (ImGui::Selectable("Audio Database", selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    S.viewing_adb = true;
                    S.viewing_lua = false;
                    S.selected_bnk.clear();
                    S.global_search.clear();
                    S.files.clear();
                    S.selected_file_index = -1;

                    for (size_t i = 0; i < S.adb_paths.size(); ++i) {
                        std::string fname = S.adb_paths[i];
                        std::error_code ec;
                        auto fsize = std::filesystem::file_size(fname, ec);
                        uint32_t size = ec ? 0 : (uint32_t)fsize;
                        S.files.push_back({(int)i, fname, size});
                    }
                }
                ImGui::PopStyleColor();
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Audio Database Files (%d)", (int)S.adb_paths.size());
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }

            if (!S.lua_files.empty()) {
                ImGui::PushID("lua_entry");
                bool selected = S.viewing_lua;
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
                if (ImGui::Selectable("Lua Scripts", selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    S.viewing_lua = true;
                    S.viewing_adb = false;
                    S.selected_bnk.clear();
                    S.global_search.clear();
                    S.files.clear();
                    S.selected_file_index = -1;
                    S.lua_preview_content.clear();
                    S.lua_preview_title.clear();
                    S.lua_preview_selected = -1;

                    for (size_t i = 0; i < S.lua_files.size(); ++i) {
                        S.files.push_back({(int)i, S.lua_files[i].filename, S.lua_files[i].size});
                    }
                }
                ImGui::PopStyleColor();
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Lua Script Files (%d)", (int)S.lua_files.size());
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }

            // Cache of nested-BNK file-table reads, keyed by parent BNK
            // path. Without this we'd open the parent and read its file
            // table every frame an expanded BNK is on screen — fine on
            // disk, but very expensive on ISO (each list_files() goes
            // through the disc reader).
            struct NestedChild { int index; std::string name; };
            static std::unordered_map<std::string, std::vector<NestedChild>> s_nested_cache;
            // Drop the cache whenever the root changed (different folder/
            // ISO selected); g_tree_last_root_dir is the canonical signal.
            static std::string s_nested_cache_root;
            if (s_nested_cache_root != S.root_dir) {
                s_nested_cache.clear();
                s_nested_cache_root = S.root_dir;
            }

            // Build a flat row list so we can virtualize via ImGuiListClipper.
            // Each row is either a top-level BNK or a nested child shown
            // because its parent is expanded. Building this is O(N) over
            // visible BNKs only — no per-frame disc reads thanks to the
            // cache.
            struct Row {
                int kind;            // 0 = top-level, 1 = nested child
                int top_idx;         // into `paths`
                int nested_idx;      // BNKReader index inside parent (kind 1)
                std::string nested_name; // nested file name (kind 1)
            };
            std::vector<Row> rows;
            rows.reserve(paths.size() + 64);
            for (size_t idx = 0; idx < paths.size(); ++idx) {
                rows.push_back({0, (int)idx, -1, {}});

                const auto& p = paths[idx];
                std::string label = std::filesystem::path(p).filename().string();
                std::string label_lower = label;
                std::transform(label_lower.begin(), label_lower.end(), label_lower.begin(), ::tolower);
                bool is_nested_bnk = (label_lower == "levels.bnk" || label_lower == "streaming.bnk");
                bool is_expanded = S.expanded_bnks.count(p) > 0;

                if (is_nested_bnk && is_expanded) {
                    auto it_cache = s_nested_cache.find(p);
                    if (it_cache == s_nested_cache.end()) {
                        std::vector<NestedChild> children;
                        try {
                            BNKReader reader(p);
                            const auto& files = reader.list_files();
                            for (size_t i = 0; i < files.size(); ++i) {
                                std::string fname_lower = files[i].name;
                                std::transform(fname_lower.begin(), fname_lower.end(), fname_lower.begin(), ::tolower);
                                if (fname_lower.size() >= 4 && fname_lower.substr(fname_lower.size() - 4) == ".bnk") {
                                    children.push_back({(int)i, files[i].name});
                                }
                            }
                        } catch (...) {}
                        it_cache = s_nested_cache.emplace(p, std::move(children)).first;
                    }
                    for (const auto& c : it_cache->second) {
                        rows.push_back({1, (int)idx, c.index, c.name});
                    }
                }
            }

            ImGuiListClipper clipper;
            clipper.Begin((int)rows.size());
            while (clipper.Step()) {
                for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                    const Row& row = rows[(size_t)r];
                    if (row.kind == 0) {
                        const auto& p = paths[(size_t)row.top_idx];
                        ImGui::PushID(r);

                        std::string label = std::filesystem::path(p).filename().string();
                        std::string label_lower = label;
                        std::transform(label_lower.begin(), label_lower.end(), label_lower.begin(), ::tolower);
                        bool is_nested_bnk = (label_lower == "levels.bnk" || label_lower == "streaming.bnk");
                        bool is_expanded = S.expanded_bnks.count(p) > 0;
                        if (is_nested_bnk) {
                            label = (is_expanded ? "- " : "+ ") + label;
                        }

                        bool selected = (p == S.selected_bnk && !S.viewing_adb && S.selected_nested_index == -1);
                        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                            if (is_nested_bnk) {
                                if (is_expanded) S.expanded_bnks.erase(p);
                                else             S.expanded_bnks.insert(p);
                            }
                            S.viewing_adb = false;
                            S.viewing_lua = false;
                            S.global_search.clear();
                            S.selected_nested_bnk.clear();
                            S.selected_nested_index = -1;
                            pick_bnk(p);
                        }
                        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(p.c_str());
                            ImGui::EndTooltip();
                        }
                        ImGui::PopID();
                    } else {
                        const auto& p = paths[(size_t)row.top_idx];
                        const std::string& nested_name = row.nested_name;
                        int nested_idx = row.nested_idx;

                        ImGui::PushID(r);
                        std::string nested_label = "    " + std::filesystem::path(nested_name).filename().string();
                        bool nested_selected = (S.selected_nested_bnk == p && S.selected_nested_index == nested_idx);
                        if (ImGui::Selectable(nested_label.c_str(), nested_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                            S.viewing_adb = false;
                            S.viewing_lua = false;
                            S.selected_bnk = p;
                            S.selected_nested_bnk = p;
                            S.selected_nested_index = nested_idx;
                            S.global_search.clear();
                            S.files.clear();
                            S.selected_file_index = -1;

                            auto tmpdir = std::filesystem::temp_directory_path() / "f2_nested_bnk";
                            std::error_code ec;
                            std::filesystem::create_directories(tmpdir, ec);
                            auto tmp_nested = tmpdir / (std::to_string(std::hash<std::string>{}(nested_name)) + ".bnk");

                            extract_one(p, nested_idx, tmp_nested.string());
                            S.selected_nested_temp_path = tmp_nested.string();

                            BNKReader nested_reader(tmp_nested.string());
                            const auto& nested_files = nested_reader.list_files();
                            S.files.reserve(nested_files.size());
                            for (size_t j = 0; j < nested_files.size(); ++j) {
                                S.files.push_back({(int)j, nested_files[j].name, nested_files[j].uncompressed_size});
                            }
                            std::sort(S.files.begin(), S.files.end(), [](const BNKItemUI& a, const BNKItemUI& b) {
                                std::string x = std::filesystem::path(a.name).filename().string();
                                std::string y = std::filesystem::path(b.name).filename().string();
                                std::transform(x.begin(), x.end(), x.begin(), ::tolower);
                                std::transform(y.begin(), y.end(), y.begin(), ::tolower);
                                return x < y;
                            });
                        }
                        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(nested_name.c_str());
                            ImGui::EndTooltip();
                        }
                        ImGui::PopID();
                    }
                }
            }
            clipper.End();
            ImGui::EndChild();
        }

        if (s_active_tab == 1) {
            ImGui::BeginChild("file_tree", ImVec2(0, 0), false);

            // Tree build state was hoisted to file scope so we could
            // start the work right after open_folder_logic. Here we just
            // observe and render from those globals.
            //
            // (The "promote build_complete -> built/!building" step that
            // used to live here moved into the worker thread — see
            // start_tree_build_for_root. Doing it here meant the loading
            // screen could stall at 100% because this tab body wasn't
            // being drawn while the loading screen was up.)

            // If the user re-opened a folder/iso and a build hasn't been
            // kicked yet for some reason, kick one now (lazy fallback).
            if (g_tree_last_root_dir != S.root_dir && !S.bnk_paths.empty()
                && !g_tree_building.load() && !g_tree_built.load())
            {
                start_tree_build_for_root(S.root_dir, S.bnk_paths);
            }

            // Use the global tree root that the build thread populated.
            TreeNode& tree_render_root = g_tree_root;

            if (g_tree_building.load()) {
                {
                    ImVec2 avail = ImGui::GetContentRegionAvail();
                    float elapsed = (float)ImGui::GetTime() - g_tree_build_start_time;

                    float dot_cycle = fmodf(elapsed * 2.0f, 4.0f);
                    int dot_count = (int)dot_cycle;
                    std::string dots(dot_count, '.');
                    std::string loading_text = "Loading file tree" + dots;

                    ImVec2 text_size = ImGui::CalcTextSize(loading_text.c_str());
                    ImVec2 pos((avail.x - text_size.x) * 0.5f, (avail.y - text_size.y) * 0.5f);
                    if (pos.x < 0) pos.x = 0;
                    if (pos.y < 0) pos.y = 0;
                    ImGui::SetCursorPos(pos);
                    ImGui::TextUnformatted(loading_text.c_str());

                    if (elapsed > 10.0f) {
                        ImVec2 warning_size = ImGui::CalcTextSize("(this may take some time)");
                        ImVec2 warning_pos((avail.x - warning_size.x) * 0.5f, pos.y + text_size.y + 10.0f);
                        if (warning_pos.x < 0) warning_pos.x = 0;
                        ImGui::SetCursorPos(warning_pos);
                        ImGui::TextUnformatted("(this may take some time)");
                    }
                }
            } else if (g_tree_built.load()) {
                for (auto& pair : tree_render_root.children) {
#ifdef _WIN32
                    draw_tree_node(pair.second, device);
#else
                    draw_tree_node(pair.second);
#endif
                }
            }

            ImGui::EndChild();
        }

        if (s_active_tab == 2) {
            draw_flat_asset_tab("Models", S.all_mdl_files, S.mdl_filter,
                                "models_list", /*kind=*/0);
        }
        if (s_active_tab == 3) {
            draw_flat_asset_tab("Textures", S.all_tex_files, S.tex_filter,
                                "textures_list", /*kind=*/1);
        }
        if (s_active_tab == 4) {
            draw_flat_asset_tab("Audio", S.all_wav_files, S.wav_filter,
                                "audio_list", /*kind=*/2);
        }
        if (s_active_tab == 5) {
            // Animations tab — flat list of all clips parsed from the
            // shared TOC. No "load on click" yet (Phase E adds the
            // playback wiring); this is a browser + selector only.
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##anim_filter", "Filter",
                                     &S.anim_filter);

            std::vector<int> vis;
            vis.reserve(S.anim_clips.size());
            std::string flow = S.anim_filter;
            std::transform(flow.begin(), flow.end(), flow.begin(), ::tolower);
            for (size_t i = 0; i < S.anim_clips.size(); ++i) {
                if (flow.empty()) {
                    vis.push_back((int)i);
                } else {
                    std::string nlow = S.anim_clips[i].name;
                    std::transform(nlow.begin(), nlow.end(), nlow.begin(), ::tolower);
                    if (nlow.find(flow) != std::string::npos) {
                        vis.push_back((int)i);
                    }
                }
            }
            if (S.dev_mode) {
                ImGui::TextDisabled("%d / %zu", (int)vis.size(),
                                    S.anim_clips.size());
                ImGui::Separator();
            }
            ImGui::BeginChild("anim_list", ImVec2(0, 0), false);
            if (S.anim_clips.empty()) {
                ImGui::TextDisabled("No animation TOC loaded.");
            } else {
                ImGuiListClipper clipper;
                clipper.Begin((int)vis.size());
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart;
                         row < clipper.DisplayEnd; ++row) {
                        const auto& c = S.anim_clips[(size_t)vis[(size_t)row]];
                        ImGui::PushID(row);
                        bool selected =
                            (S.anim_selected_clip == vis[(size_t)row]);
                        char label[64];
                        float dur_s = Anim::clip_duration_seconds(c);
                        std::snprintf(label, sizeof(label), "%s  (%.2fs)",
                                      c.name.c_str(), dur_s);
                        if (ImGui::Selectable(label, selected,
                                              ImGuiSelectableFlags_SpanAllColumns)) {
                            S.anim_selected_clip = vis[(size_t)row];
                        }
                        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(c.name.c_str());
                            ImGui::Text("Duration: %.3f s  (%.0f fps)",
                                        dur_s, c.fps);
                            ImGui::Text("Events: %zu", c.events.size());
                            if (S.dev_mode) {
                                ImGui::Text("offset=0x%08X len=%u",
                                            c.data_offset, c.data_length);
                                ImGui::Text("key0=0x%08X key1=0x%08X",
                                            c.key0, c.key1);
                            }
                            ImGui::EndTooltip();
                        }
                        ImGui::PopID();
                    }
                }
                clipper.End();
            }
            ImGui::EndChild();
        }

    ImGui::EndChild(); // left_panel
}
