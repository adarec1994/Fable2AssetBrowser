#include "../UI_Panels.h"
#include "PanelInternal.h"
#include "../UI_Main.h"
#include "../OutputLog.h"
#include "../ModelPreview.h"

#include "../../ISO/IsoDump.h"

#include "../../Lua.h"
#include "../../Utilities/Progress.h"
#include "../../Utilities/Utils.h"
#include "../../BNKCore.cpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "IconsFontAwesome6.h"
#include <filesystem>
#include <algorithm>
#include <thread>
#include <unordered_map>
#include <cstring>
#include <cmath>

extern ModelPreview g_mp;

static const char* const kLeftPanelTabLabels[] = {
    "BNK List", "File Tree", "Models", "Textures", "Audio", "Animations"
};

static float compute_tab_button_width() {
    float w = 0.0f;
    for (const char* L : kLeftPanelTabLabels) {
        w = (std::max)(w, ImGui::CalcTextSize(L).x);
    }
    return w + ImGui::GetStyle().FramePadding.x * 2.0f;
}

float left_panel_min_width() {

    const ImGuiStyle& st = ImGui::GetStyle();
    float tab_w = compute_tab_button_width();
    constexpr float kTabGap = 2.0f;
    constexpr int kRow2Count = 4;
    float row_w = (float)kRow2Count * tab_w +
                  (float)(kRow2Count - 1) * kTabGap;
    row_w += st.WindowPadding.x * 2.0f;
    return row_w;
}

void load_flat_asset_entry(const FlatAssetEntry& e, int kind) {
    if (S.selected_bnk != e.bnk_path) {
        S.viewing_adb = false;
        S.global_search.clear();
        S.selected_nested_bnk.clear();
        S.selected_nested_index = -1;
        pick_bnk(e.bnk_path);
    }

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

                open_audio_player_for_selected((int)i);
            }
            break;
        }
    }
}

namespace {

enum class DrillKind { None, Bnk, Adb, Lua };

struct DrillState {
    DrillKind kind = DrillKind::None;
    std::string title;
    std::string bnk_path;
    bool        from_nested = false;
    std::vector<BNKItemUI> items;
    std::string filter;

    float anim_t   = 0.0f;
    float target_t = 0.0f;
};

DrillState g_bnk_drill;

void drill_step_anim(DrillState& d, float dt) {
    constexpr float kSpeed = 7.0f;
    if (d.target_t == d.anim_t) return;
    float dir = (d.target_t > d.anim_t) ? +1.0f : -1.0f;
    d.anim_t += dir * dt * kSpeed;
    if ((dir > 0 && d.anim_t > d.target_t) ||
        (dir < 0 && d.anim_t < d.target_t)) {
        d.anim_t = d.target_t;
    }
}

void drill_open_bnk(DrillState& d, const std::string& bnk_path,
                    bool from_nested) {
    d.kind        = DrillKind::Bnk;
    d.title       = std::filesystem::path(bnk_path).filename().string();
    d.bnk_path    = bnk_path;
    d.from_nested = from_nested;
    d.items.clear();
    d.filter.clear();
    try {
        BNKReader reader(bnk_path);
        const auto& files = reader.list_files();
        d.items.reserve(files.size());
        for (size_t i = 0; i < files.size(); ++i) {
            d.items.push_back({(int)i, files[i].name,
                               files[i].uncompressed_size});
        }
        std::sort(d.items.begin(), d.items.end(),
                  [](const BNKItemUI& a, const BNKItemUI& b) {
                      auto la = std::filesystem::path(a.name)
                                    .filename().string();
                      auto lb = std::filesystem::path(b.name)
                                    .filename().string();
                      std::transform(la.begin(), la.end(), la.begin(),
                                     ::tolower);
                      std::transform(lb.begin(), lb.end(), lb.begin(),
                                     ::tolower);
                      return la < lb;
                  });
    } catch (...) {

    }
    d.target_t = 1.0f;
}

void drill_open_adb(DrillState& d) {
    d.kind        = DrillKind::Adb;
    d.title       = "Audio Database";
    d.bnk_path.clear();
    d.from_nested = false;
    d.items.clear();
    d.filter.clear();
    for (size_t i = 0; i < S.adb_paths.size(); ++i) {
        std::error_code ec;
        auto sz = std::filesystem::file_size(S.adb_paths[i], ec);
        d.items.push_back({(int)i, S.adb_paths[i],
                           ec ? 0u : (uint32_t)sz});
    }
    d.target_t = 1.0f;
}

void drill_open_lua(DrillState& d) {
    d.kind        = DrillKind::Lua;
    d.title       = "Lua Scripts";
    d.bnk_path.clear();
    d.from_nested = false;
    d.items.clear();
    d.filter.clear();
    for (size_t i = 0; i < S.lua_files.size(); ++i) {
        d.items.push_back({(int)i, S.lua_files[i].filename,
                           S.lua_files[i].size});
    }
    d.target_t = 1.0f;
}

void drill_back(DrillState& d) {
    d.target_t = 0.0f;

}

bool drill_settled(const DrillState& d) {
    return std::abs(d.anim_t - d.target_t) < 0.001f;
}

}

#ifdef _WIN32
void draw_left_panel(ID3D11Device* device) {
#else
void draw_left_panel() {
#endif

    ImGui::BeginChild("left_panel", ImVec2(0, 0), true);

    static int s_active_tab = 1;

    const ImVec2 tab_size(compute_tab_button_width(), 0.0f);

    auto tab_button = [&tab_size](const char* label, bool active,
                                  ImU32 text_col = 0) -> bool {
        const ImGuiStyle& st = ImGui::GetStyle();
        const ImVec4 bg     = st.Colors[active ? ImGuiCol_TabActive : ImGuiCol_Tab];
        const ImVec4 hov    = st.Colors[ImGuiCol_TabHovered];
        const ImVec4 act    = st.Colors[ImGuiCol_TabActive];
        ImGui::PushStyleColor(ImGuiCol_Button,        bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  act);
        if (text_col) ImGui::PushStyleColor(ImGuiCol_Text, text_col);
        bool clicked = ImGui::Button(label, tab_size);
        if (text_col) ImGui::PopStyleColor();
        ImGui::PopStyleColor(3);
        return clicked;
    };

    if (tab_button("BNK List", s_active_tab == 0))   s_active_tab = 0;
    ImGui::SameLine(0, 2);
    if (tab_button("File Tree", s_active_tab == 1))  s_active_tab = 1;

    const ImU32 kPurpleLabel = IM_COL32(200, 130, 255, 255);
    if (tab_button("Models",   s_active_tab == 2, kPurpleLabel)) s_active_tab = 2;
    ImGui::SameLine(0, 2);
    if (tab_button("Textures", s_active_tab == 3, kPurpleLabel)) s_active_tab = 3;
    ImGui::SameLine(0, 2);
    if (tab_button("Audio",    s_active_tab == 4, kPurpleLabel)) s_active_tab = 4;
    ImGui::SameLine(0, 2);
    if (tab_button("Animations", s_active_tab == 5, kPurpleLabel)) s_active_tab = 5;

    ImGui::Separator();

    auto draw_flat_asset_tab = [](const char* /*label*/,
                                  std::vector<FlatAssetEntry>& entries,
                                  std::string& filter,
                                  const char* child_id,
                                  int kind,
                                  float footer_h = 0.0f) {
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

        const float child_h = (footer_h > 0.0f) ? -footer_h : 0.0f;
        ImGui::BeginChild(child_id, ImVec2(0, child_h), false);
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

            drill_step_anim(g_bnk_drill, ImGui::GetIO().DeltaTime);

            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const float page_w = avail.x;
            const float page_h = avail.y;

            const float kVisEps = 0.0001f;
            const bool a_visible = g_bnk_drill.anim_t <  1.0f - kVisEps;
            const bool b_visible = g_bnk_drill.anim_t >  0.0f + kVisEps;

            ImGui::BeginChild("##bnk_drill_container",
                              ImVec2(page_w, page_h),
                              false,
                              ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);

            ImGui::SetScrollX(g_bnk_drill.anim_t * page_w);

            ImGui::BeginChild("##bnk_page_a", ImVec2(page_w, page_h), false);
            if (a_visible) {

            ImGui::SetNextItemWidth(-1);
            if (!S.bnk_paths.empty()) {
                ImGui::InputTextWithHint("##bnk_filter", "Filter", &S.bnk_filter);
            }

            auto paths = filtered_bnk_paths();

            const bool a_can_click = (g_bnk_drill.target_t == 0.0f) &&
                                      drill_settled(g_bnk_drill);

            if (!S.adb_paths.empty()) {
                ImGui::PushID("adb_entry");
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                if (ImGui::Selectable("Audio Database",
                                      g_bnk_drill.kind == DrillKind::Adb,
                                      ImGuiSelectableFlags_SpanAllColumns) &&
                    a_can_click) {
                    drill_open_adb(g_bnk_drill);
                }
                ImGui::PopStyleColor();
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Audio Database Files (%d)",
                                (int)S.adb_paths.size());
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }

            if (!S.lua_files.empty()) {
                ImGui::PushID("lua_entry");
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
                if (ImGui::Selectable("Lua Scripts",
                                      g_bnk_drill.kind == DrillKind::Lua,
                                      ImGuiSelectableFlags_SpanAllColumns) &&
                    a_can_click) {
                    drill_open_lua(g_bnk_drill);
                }
                ImGui::PopStyleColor();
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Lua Script Files (%d)",
                                (int)S.lua_files.size());
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }

            struct NestedChild { int index; std::string name; };
            static std::unordered_map<std::string, std::vector<NestedChild>> s_nested_cache;
            static std::string s_nested_cache_root;
            if (s_nested_cache_root != S.root_dir) {
                s_nested_cache.clear();
                s_nested_cache_root = S.root_dir;
            }

            struct Row {
                int kind;
                int top_idx;
                int nested_idx;
                std::string nested_name;
            };
            std::vector<Row> rows;
            rows.reserve(paths.size() + 64);
            for (size_t idx = 0; idx < paths.size(); ++idx) {
                rows.push_back({0, (int)idx, -1, {}});
                const auto& p = paths[idx];
                std::string label = std::filesystem::path(p).filename().string();
                std::string label_lower = label;
                std::transform(label_lower.begin(), label_lower.end(),
                               label_lower.begin(), ::tolower);
                bool is_container = (label_lower == "levels.bnk" ||
                                     label_lower == "streaming.bnk");
                bool is_expanded  = S.expanded_bnks.count(p) > 0;
                if (is_container && is_expanded) {
                    auto it_cache = s_nested_cache.find(p);
                    if (it_cache == s_nested_cache.end()) {
                        std::vector<NestedChild> children;
                        try {
                            BNKReader reader(p);
                            const auto& files = reader.list_files();
                            for (size_t i = 0; i < files.size(); ++i) {
                                std::string fl = files[i].name;
                                std::transform(fl.begin(), fl.end(),
                                               fl.begin(), ::tolower);
                                if (fl.size() >= 4 &&
                                    fl.substr(fl.size() - 4) == ".bnk") {
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

                        std::string label = std::filesystem::path(p)
                                                .filename().string();
                        std::string label_lower = label;
                        std::transform(label_lower.begin(), label_lower.end(),
                                       label_lower.begin(), ::tolower);
                        bool is_container = (label_lower == "levels.bnk" ||
                                             label_lower == "streaming.bnk");
                        bool is_expanded  = S.expanded_bnks.count(p) > 0;
                        if (is_container) {
                            label = (is_expanded ? "- " : "+ ") + label;
                        }

                        bool drilled_here =
                            (g_bnk_drill.kind == DrillKind::Bnk &&
                             g_bnk_drill.bnk_path == p);
                        if (ImGui::Selectable(label.c_str(), drilled_here,
                                              ImGuiSelectableFlags_SpanAllColumns) &&
                            a_can_click) {
                            if (is_container) {

                                if (is_expanded) S.expanded_bnks.erase(p);
                                else             S.expanded_bnks.insert(p);
                            } else {

                                drill_open_bnk(g_bnk_drill, p,
                                               /*from_nested=*/false);
                            }
                        }

                        if (ImGui::BeginPopupContextItem()) {
                            if (ImGui::MenuItem("Extract")) {
                                extract_single_bnk_contents(p);
                            }
                            ImGui::EndPopup();
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
                        std::string nested_label =
                            "    " + std::filesystem::path(nested_name)
                                         .filename().string();
                        bool drilled_here =
                            (g_bnk_drill.kind == DrillKind::Bnk &&
                             g_bnk_drill.from_nested &&
                             std::filesystem::path(g_bnk_drill.bnk_path)
                                 .filename() ==
                             std::filesystem::path(nested_name)
                                 .filename());
                        if (ImGui::Selectable(nested_label.c_str(), drilled_here,
                                              ImGuiSelectableFlags_SpanAllColumns) &&
                            a_can_click) {
                            try {
                                auto tmpdir = std::filesystem::temp_directory_path()
                                            / "f2_nested_bnk";
                                std::error_code ec;
                                std::filesystem::create_directories(tmpdir, ec);
                                auto tmp_nested = tmpdir /
                                    (std::to_string(std::hash<std::string>{}(nested_name)) + ".bnk");
                                extract_one(p, nested_idx, tmp_nested.string());
                                drill_open_bnk(g_bnk_drill,
                                               tmp_nested.string(),
                                               /*from_nested=*/true);
                            } catch (const std::exception& e) {
                                OutputLog::error(std::string(
                                    "Failed to extract nested BNK ") +
                                    nested_name + ": " + e.what());
                            } catch (...) {
                                OutputLog::error(std::string(
                                    "Failed to extract nested BNK ") +
                                    nested_name);
                            }
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
            }
            ImGui::EndChild();

            ImGui::SameLine(0.0f, 0.0f);

            ImGui::BeginChild("##bnk_page_b", ImVec2(page_w, page_h), false);
            if (b_visible) {

            const bool b_can_click = (g_bnk_drill.target_t == 1.0f) &&
                                      drill_settled(g_bnk_drill);

            {

                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                      ImVec4(0.30f, 0.45f, 0.65f, 0.40f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                      ImVec4(0.40f, 0.60f, 0.90f, 0.55f));
                if (ImGui::Button(ICON_FA_ARROW_LEFT "##drill_back")) {
                    drill_back(g_bnk_drill);
                }
                ImGui::PopStyleColor(3);
                ImGui::SameLine();
                ImGui::TextUnformatted(g_bnk_drill.title.c_str());
            }

            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##drill_filter", "Filter",
                                     &g_bnk_drill.filter);

            std::string flt = g_bnk_drill.filter;
            std::transform(flt.begin(), flt.end(), flt.begin(), ::tolower);
            std::vector<int> vis;
            vis.reserve(g_bnk_drill.items.size());
            for (size_t i = 0; i < g_bnk_drill.items.size(); ++i) {
                if (flt.empty()) {
                    vis.push_back((int)i);
                } else {
                    std::string n =
                        std::filesystem::path(g_bnk_drill.items[i].name)
                            .filename().string();
                    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                    if (n.find(flt) != std::string::npos)
                        vis.push_back((int)i);
                }
            }

            ImGui::BeginChild("##drill_list", ImVec2(0, 0), false);

            ImGuiListClipper drill_clipper;
            drill_clipper.Begin((int)vis.size());
            while (drill_clipper.Step()) {
                for (int r = drill_clipper.DisplayStart;
                     r < drill_clipper.DisplayEnd; ++r) {
                    int idx = vis[(size_t)r];
                    const BNKItemUI& it = g_bnk_drill.items[(size_t)idx];
                    ImGui::PushID(r);

                    std::string label =
                        std::filesystem::path(it.name).filename().string();
                    bool selected =
                        (g_bnk_drill.kind == DrillKind::Bnk &&
                         S.selected_bnk == g_bnk_drill.bnk_path &&
                         S.selected_file_index >= 0 &&
                         S.selected_file_index < (int)S.files.size() &&
                         S.files[(size_t)S.selected_file_index].index == it.index);
                    if (ImGui::Selectable(label.c_str(), selected,
                                          ImGuiSelectableFlags_SpanAllColumns) &&
                        b_can_click) {

                        if (g_bnk_drill.kind == DrillKind::Bnk) {

                            if (S.selected_bnk != g_bnk_drill.bnk_path) {
                                S.viewing_adb = false;
                                S.viewing_lua = false;
                                S.global_search.clear();
                                S.selected_nested_bnk.clear();
                                S.selected_nested_index = -1;
                                pick_bnk(g_bnk_drill.bnk_path);
                            }
                            if (g_bnk_drill.from_nested) {
                                S.selected_nested_temp_path = g_bnk_drill.bnk_path;
                                S.selected_nested_index = 0;
                            }

                            for (size_t j = 0; j < S.files.size(); ++j) {
                                if (S.files[j].index == it.index) {
                                    S.selected_file_index = (int)j;
                                    std::string ln = it.name;
                                    std::transform(ln.begin(), ln.end(),
                                                   ln.begin(), ::tolower);
                                    if (ln.size() >= 4 &&
                                        ln.rfind(".mdl") == ln.size() - 4) {
                                        g_pending_mdl_full_path = it.name;
                                        g_pending_mdl_load = true;
                                        g_pending_mdl_index = (int)j;
                                    } else if (ln.size() >= 4 &&
                                               ln.rfind(".tex") == ln.size() - 4) {
                                        g_pending_tex_load = true;
                                        g_pending_tex_index = (int)j;
                                    } else if (ln.size() >= 4 &&
                                               ln.rfind(".wav") == ln.size() - 4) {

                                        open_audio_player_for_selected((int)j);
                                    }
                                    break;
                                }
                            }
                        } else if (g_bnk_drill.kind == DrillKind::Adb) {
                            S.viewing_adb = true;
                            S.viewing_lua = false;
                            S.selected_bnk.clear();
                            S.global_search.clear();
                            S.files.clear();
                            S.selected_file_index = -1;
                            for (size_t i = 0; i < S.adb_paths.size(); ++i) {
                                std::error_code ec;
                                auto fs = std::filesystem::file_size(S.adb_paths[i], ec);
                                S.files.push_back({(int)i, S.adb_paths[i],
                                                   ec ? 0u : (uint32_t)fs});
                            }
                            S.selected_file_index = idx;
                        } else if (g_bnk_drill.kind == DrillKind::Lua) {

                            S.viewing_lua = true;
                            S.viewing_adb = false;
                            S.selected_bnk.clear();
                            S.global_search.clear();
                            S.files.clear();
                            S.selected_file_index = -1;
                            for (size_t i = 0; i < S.lua_files.size(); ++i) {
                                S.files.push_back({(int)i,
                                                   S.lua_files[i].filename,
                                                   S.lua_files[i].size});
                            }
                            S.selected_file_index = idx;

                            if (idx >= 0 &&
                                (size_t)idx < S.lua_files.size())
                            {
                                const std::string lua_path =
                                    S.lua_files[(size_t)idx].path;
                                const std::string lua_title =
                                    S.lua_files[(size_t)idx].filename;

#ifdef _WIN32
                                if (g_mp.has_model) MP_Release(g_mp);
                                g_mp.has_model = false;
                                if (S.texture_window_srv) {
                                    S.texture_window_srv->Release();
                                    S.texture_window_srv = nullptr;
                                }
                                S.texture_window_width  = 0;
                                S.texture_window_height = 0;
#endif

                                S.lua_preview_selected = idx;
                                S.lua_preview_title    = lua_title;
                                S.lua_preview_content.clear();
                                S.lua_preview_loading = true;
                                S.show_lua_render = true;

                                progress_open(
                                    0,
                                    "Decompiling " + lua_title + "...");
                                std::thread([lua_path]() {
                                    std::string content =
                                        read_lua_file_content(lua_path);
                                    S.lua_preview_content = content;
                                    S.lua_preview_loading = false;
                                    progress_done();
                                }).detach();
                            }
                        }
                    }

                    if (g_bnk_drill.kind == DrillKind::Bnk) {
                        file_hex_context_menu(g_bnk_drill.bnk_path,
                                              it.index,
                                              g_bnk_drill.from_nested,
                                              it.name);
                    }
                    if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted(it.name.c_str());
                        ImGui::EndTooltip();
                    }
                    ImGui::PopID();
                }
            }
            drill_clipper.End();

            ImGui::EndChild();
            }
            ImGui::EndChild();
            ImGui::EndChild();
        }

        if (s_active_tab == 1) {
            ImGui::BeginChild("file_tree", ImVec2(0, 0), false);

            if (g_tree_last_root_dir != S.root_dir && !S.bnk_paths.empty()
                && !g_tree_building.load() && !g_tree_built.load())
            {
                start_tree_build_for_root(S.root_dir, S.bnk_paths);
            }

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

            const float footer_h = ImGui::GetFrameHeightWithSpacing();
            draw_flat_asset_tab("Models", S.all_mdl_files, S.mdl_filter,
                                "models_list", /*kind=*/0, footer_h);

            const bool has_any = !S.all_mdl_files.empty();
            if (!has_any) ImGui::BeginDisabled();
            if (ImGui::Button("Extract All as...##mdl_extract_all_as",
                              ImVec2(-1, 0))) {
                ImGui::OpenPopup("##mdl_extract_all_as_popup");
            }
            if (!has_any) ImGui::EndDisabled();
            if (!has_any && !S.hide_tooltips &&
                ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(
                    "No MDLs indexed yet — open a Fable 2 root "
                    "(folder or ISO) to populate this list.");
                ImGui::EndTooltip();
            }
            if (ImGui::BeginPopup("##mdl_extract_all_as_popup")) {
                if (ImGui::MenuItem("GLB")) {
                    ISO::dump_mdl_files_as(ISO::MdlExportFormat::GLB);
                }
                if (ImGui::MenuItem("FBX")) {
                    ISO::dump_mdl_files_as(ISO::MdlExportFormat::FBX);
                }
                ImGui::Separator();
                if (ImGui::MenuItem(".mdl (raw)")) {
                    ISO::dump_mdl_files_as(ISO::MdlExportFormat::RAW);
                }
                ImGui::EndPopup();
            }
        }
        if (s_active_tab == 3) {

            const float footer_h = ImGui::GetFrameHeightWithSpacing();
            draw_flat_asset_tab("Textures", S.all_tex_files, S.tex_filter,
                                "textures_list", /*kind=*/1, footer_h);

            const bool has_any = !S.all_tex_files.empty();
            if (!has_any) ImGui::BeginDisabled();
            if (ImGui::Button("Extract All as...##tex_extract_all_as",
                              ImVec2(-1, 0))) {
                ImGui::OpenPopup("##tex_extract_all_as_popup");
            }
            if (!has_any) ImGui::EndDisabled();
            if (!has_any && !S.hide_tooltips &&
                ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(
                    "No textures indexed yet — open a Fable 2 root "
                    "(folder or ISO) to populate this list.");
                ImGui::EndTooltip();
            }
            if (ImGui::BeginPopup("##tex_extract_all_as_popup")) {
                if (ImGui::MenuItem("PNG")) {
                    ISO::dump_tex_files_as(TexExportFormat::PNG);
                }
                if (ImGui::MenuItem("JPG")) {
                    ISO::dump_tex_files_as(TexExportFormat::JPG);
                }
                if (ImGui::MenuItem("TIFF")) {
                    ISO::dump_tex_files_as(TexExportFormat::TIFF);
                }
                if (ImGui::MenuItem("DDS")) {
                    ISO::dump_tex_files_as(TexExportFormat::DDS);
                }
                ImGui::Separator();
                if (ImGui::MenuItem(".tex (raw)")) {

                    ISO::dump_tex_files();
                }
                ImGui::EndPopup();
            }
        }
        if (s_active_tab == 4) {

            const float footer_h = ImGui::GetFrameHeightWithSpacing();
            draw_flat_asset_tab("Audio", S.all_wav_files, S.wav_filter,
                                "audio_list", /*kind=*/2, footer_h);

            const bool has_any = !S.all_wav_files.empty();
            if (!has_any) ImGui::BeginDisabled();
            if (ImGui::Button("Extract All as...##wav_extract_all_as",
                              ImVec2(-1, 0))) {
                ImGui::OpenPopup("##wav_extract_all_as_popup");
            }
            if (!has_any) ImGui::EndDisabled();
            if (!has_any && !S.hide_tooltips &&
                ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(
                    "No audio indexed yet — open a Fable 2 root "
                    "(folder or ISO) to populate this list.");
                ImGui::EndTooltip();
            }
            if (ImGui::BeginPopup("##wav_extract_all_as_popup")) {
                if (ImGui::MenuItem("WAV (PCM)")) {
                    ISO::dump_wav_files_as(
                        ISO::AudioExportFormat::WAV_PCM);
                }
                if (ImGui::MenuItem(".wav (raw XMA)")) {
                    ISO::dump_wav_files_as(
                        ISO::AudioExportFormat::WAV_RAW);
                }
                ImGui::Separator();

                if (ImGui::MenuItem("MP3")) {
                    ISO::dump_wav_files_as(
                        ISO::AudioExportFormat::MP3);
                }
                if (ImGui::MenuItem("AAC (.m4a)")) {
                    ISO::dump_wav_files_as(
                        ISO::AudioExportFormat::AAC);
                }
                ImGui::EndPopup();
            }
        }
        if (s_active_tab == 5) {

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

    ImGui::EndChild();
}
