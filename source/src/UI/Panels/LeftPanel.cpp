#include "../UI_Panels.h"
#include "PanelInternal.h"
#include "../UI_Main.h"
#include "../OutputLog.h"
#include "../ModelPreview.h"

#include "../../ISO/IsoDump.h"
#include "../../Level/LevelLoader.h"

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
#include <atomic>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <cstring>
#include <cmath>

extern ModelPreview g_mp;

static const char* const kLeftPanelTabLabels[] = {
    "BNK List", "File Tree", "Levels", "Models", "Textures", "Audio", "Animations"
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
    ImGui::SameLine(0, 2);
    /* Levels tab — listed right after File Tree, painted gold so it
       stands out as "this loads a whole world, not just one asset". */
    const ImU32 kGoldLabel = IM_COL32(255, 215, 0, 255);
    if (tab_button("Levels", s_active_tab == 6, kGoldLabel)) s_active_tab = 6;

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
                                  float footer_h = 0.0f,
                                  bool dedup_by_name_size = true) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint(("##" + std::string(child_id) + "_filter").c_str(),
                                 "Filter", &filter);

        std::string flow = filter;
        std::transform(flow.begin(), flow.end(), flow.begin(), ::tolower);

        /* Per-tab cache so we don't rebuild + dedup the whole `vis`
           list every frame — for large tabs (Textures, Animations:
           often 10k+ entries) the per-frame hash work was visibly
           lagging the UI.  Cache keyed on (entries-pointer-and-size,
           filter, dedup flag) — invalidated whenever any of those
           change (loading a new ROM, typing in the filter, etc.).  */
        struct CacheEntry {
            const void* entries_ptr = nullptr;
            size_t      entries_size = 0;
            std::string filter_lc;
            bool        dedup = false;
            std::vector<int> vis;
            size_t      dups_skipped = 0;
        };
        static std::unordered_map<std::string, CacheEntry> cache;
        CacheEntry& c = cache[child_id];

        const bool cache_valid =
            c.entries_ptr == (const void*)entries.data() &&
            c.entries_size == entries.size() &&
            c.filter_lc == flow &&
            c.dedup == dedup_by_name_size;

        if (!cache_valid) {
            c.entries_ptr  = (const void*)entries.data();
            c.entries_size = entries.size();
            c.filter_lc    = flow;
            c.dedup        = dedup_by_name_size;
            c.vis.clear();
            c.vis.reserve(entries.size());
            c.dups_skipped = 0;

            std::unordered_set<std::string> seen_keys;
            for (size_t i = 0; i < entries.size(); ++i) {
                const auto& e = entries[i];
                if (!flow.empty()) {
                    std::string nlow = e.name;
                    std::transform(nlow.begin(), nlow.end(), nlow.begin(),
                                   ::tolower);
                    if (nlow.find(flow) == std::string::npos) continue;
                }
                if (dedup_by_name_size) {
                    std::string nlow = e.name;
                    std::transform(nlow.begin(), nlow.end(), nlow.begin(),
                                   ::tolower);
                    std::string k = nlow + "|" + std::to_string(e.size);
                    if (!seen_keys.insert(std::move(k)).second) {
                        ++c.dups_skipped;
                        continue;
                    }
                }
                c.vis.push_back((int)i);
            }
        }
        auto& vis = c.vis;
        const size_t dups_skipped = c.dups_skipped;

        if (S.dev_mode) {
            if (dedup_by_name_size && dups_skipped > 0) {
                ImGui::TextDisabled("%d / %zu  (%zu dup hidden)",
                    (int)vis.size(), entries.size(), dups_skipped);
            } else {
                ImGui::TextDisabled("%d / %zu", (int)vis.size(), entries.size());
            }
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
                                "models_list", /*kind=*/0, footer_h,
                                /*dedup_by_name_size=*/true);

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

        if (s_active_tab == 6) {
            /* Levels tab — discoverable .engine_level files from the
               loaded BNKs, grouped under region headings with friendly
               display names.  Mapping is by case-insensitive full_path
               match; anything not in the table goes under "Other".  */
            struct LvlMap {
                const char* path;     // lowercase forward/back-slash agnostic
                const char* name;
            };
            struct LvlGroup {
                const char* heading;
                std::initializer_list<LvlMap> entries;
            };
            static const LvlGroup kLevelGroups[] = {
                {"Bloodstone", {
                    {"worlds\\albion\\bloodstone\\defaultscenario\\defaultscenario.engine_level", "Bloodstone"},
                    {"worlds\\albion\\caves\\bloodstone\\bloodstone_assault\\defaultscenario\\defaultscenario.engine_level", "Bloodstone Assault"},
                    {"worlds\\albion\\caves\\bloodstone\\sinkhole\\defaultscenario\\defaultscenario.engine_level", "Sinkhole"},
                    {"worlds\\albion\\caves\\bloodstone\\treasureisland\\defaultscenario\\defaultscenario.engine_level", "Treasure Island"},
                    {"worlds\\albion\\reaver beach (bloodtsone)\\defaultscenario\\defaultscenario.engine_level", "Reaver Beach"},
                }},
                {"Bower Lake", {
                    {"worlds\\albion\\bowerlake\\chapter3\\chapter3.engine_level", "Chapter 3"},
                    {"worlds\\albion\\bowerlake\\defaultscenario\\defaultscenario.engine_level", "Bower Lake"},
                    {"worlds\\albion\\caves\\bowerlake\\thagscave\\defaultscenario\\defaultscenario.engine_level", "Thag's Cave"},
                    {"worlds\\albion\\tombs\\bowerlake\\rescuemybabytomb\\defaultscenario\\defaultscenario.engine_level", "\"Rescue My Baby\" Tomb"},
                }},
                {"Brightwood", {
                    {"worlds\\albion\\brightwood\\chapter3abandonedfarm\\chapter3abandonedfarm.engine_level", "Abandoned Farm"},
                    {"worlds\\albion\\brightwood\\chapter3bigfarm\\chapter3bigfarm.engine_level", "Big Farm"},
                    {"worlds\\albion\\brightwood\\defaultscenario\\defaultscenario.engine_level", "Brightwood"},
                    {"worlds\\albion\\caves\\brightwood\\bwfarmcellar\\defaultscenario\\defaultscenario.engine_level", "Brightwood Farm Cellar"},
                    {"worlds\\albion\\caves\\brightwood\\wellcave\\defaultscenario\\defaultscenario.engine_level", "Wellcave"},
                }},
                {"Bowerstone Cemetary", {
                    {"worlds\\albion\\bwscemetary\\ch3_cemetary\\ch3_cemetary.engine_level", "Chapter 3"},
                    {"worlds\\albion\\bwscemetary\\defaultscenario\\defaultscenario.engine_level", "Bowerstone Cemetary"},
                    {"worlds\\albion\\caves\\bwscemetary\\gravekeeperscave\\defaultscenario\\defaultscenario.engine_level", "Gravekeepers Cave"},
                    {"worlds\\albion\\tombs\\bwscemetery\\hallofthedead\\defaultscenario\\defaultscenario.engine_level", "Hall of the Dead"},
                    {"worlds\\albion\\tombs\\bwscemetery\\ladygreystomb\\defaultscenario\\defaultscenario.engine_level", "Lady Grey's Tomb"},
                }},
                {"Bowerstone Market", {
                    {"worlds\\albion\\bwsmarket\\bwsmarket_chapter3\\bwsmarket_chapter3.engine_level", "Chapter 3"},
                    {"worlds\\albion\\bwsmarket\\defaultscenario\\defaultscenario.engine_level", "Bowerstone Market"},
                    {"worlds\\albion\\tombs\\bwsmarket\\nightmare hollow\\defaultscenario\\defaultscenario.engine_level", "Nightmare Hollow"},
                }},
                {"Bowerstone Slums", {
                    {"worlds\\albion\\bwsslums\\chapter2posh\\chapter2posh.engine_level", "Chapter 2 - Posh"},
                    {"worlds\\albion\\bwsslums\\chapter2slums\\chapter2slums.engine_level", "Chapter 2 - Slums"},
                    {"worlds\\albion\\bwsslums\\chapter3posh\\chapter3posh.engine_level", "Chapter 3 - Posh"},
                    {"worlds\\albion\\bwsslums\\chapter3slums\\chapter3slums.engine_level", "Chapter 3 - Slums"},
                    {"worlds\\albion\\bwsslums\\defaultscenario\\defaultscenario.engine_level", "Bowerstone Slums"},
                }},
                {"Dunecrest", {
                    {"worlds\\albion\\dunecrestnew\\defaultscenario\\defaultscenario.engine_level", "Dunecrest New"},
                    {"worlds\\albion\\caves\\dunecrest\\hobbecave\\defaultscenario\\defaultscenario.engine_level", "Hobbe Cave"},
                    {"worlds\\albion\\caves\\dunecrest\\inncave\\defaultscenario\\defaultscenario.engine_level", "Inn Cave"},
                    {"worlds\\albion\\caves\\dunecrest\\waterfallcave\\defaultscenario\\defaultscenario.engine_level", "Waterfall Cave"},
                    {"worlds\\albion\\dunecrestnew\\chapter3\\chapter3.engine_level", "Chapter 3"},
                }},
                {"Deepwood", {
                    {"worlds\\albion\\caves\\deepwood\\rivercave\\defaultscenario\\defaultscenario.engine_level", "River Cave"},
                }},
                {"Wraithmarsh", {
                    {"worlds\\albion\\wraithmarsh\\defaultscenario\\defaultscenario.engine_level", "Wraithmarsh"},
                    {"worlds\\albion\\caves\\wraithmarsh\\wellcave\\defaultscenario\\defaultscenario.engine_level", "Well Cave"},
                    {"worlds\\albion\\tombs\\wraithmarsh\\autumnshrine\\defaultscenario\\defaultscenario.engine_level", "Autumn Shrine"},
                    {"worlds\\albion\\tombs\\wraithmarsh\\hotcrypt\\defaultscenario\\defaultscenario.engine_level", "Hot Crypt"},
                    {"worlds\\albion\\tombs\\wraithmarsh\\wraithmarshtobloodstonetomb\\defaultscenario\\defaultscenario.engine_level", "Wraithmarsh to Bloodstone Tomb"},
                }},
                {"Westcliffe", {
                    {"worlds\\albion\\westcliff\\chapter3\\chapter3.engine_level", "Chapter 3"},
                    {"worlds\\albion\\westcliff\\defaultscenario\\defaultscenario.engine_level", "Westcliffe"},
                    {"worlds\\albion\\caves\\westcliff\\palacecave\\defaultscenario\\defaultscenario.engine_level", "Palace Cave"},
                    {"worlds\\albion\\caves\\westcliff\\smugglerscave\\defaultscenario\\defaultscenario.engine_level", "Smuggler's Cave"},
                    {"worlds\\albion\\caves\\westcliff\\westcliffexterior\\defaultscenario\\defaultscenario.engine_level", "Westcliffe Exterior"},
                }},
                {"Ravenscar", {
                    {"worlds\\albion\\caves\\ravenscar\\hobbescavern\\defaultscenario\\defaultscenario.engine_level", "Hobbes Cavern"},
                    {"worlds\\albion\\caves\\ravenscar\\rvsritualcave\\defaultscenario\\defaultscenario.engine_level", "Ravenscar Ritual Cave"},
                    {"worlds\\albion\\ravenscar\\chapter3_evil\\chapter3_evil.engine_level", "Chapter 3 - Evil"},
                    {"worlds\\albion\\ravenscar\\chapter3_good\\chapter3_good.engine_level", "Chapter 3 - Good"},
                    {"worlds\\albion\\ravenscar\\defaultscenario\\defaultscenario.engine_level", "Ravenscar"},
                }},
                {"Castle Fairfax", {
                    {"worlds\\albion\\fairfaxcastlegardens\\defaultscenario\\defaultscenario.engine_level", "Fairfax Castle Gardens"},
                    {"worlds\\albion\\fairfaxcastlegardens\\ff_chapter1\\ff_chapter1.engine_level", "Chapter 1"},
                    {"worlds\\albion\\fairfaxcastlegardens\\ff_chapter3\\ff_chapter3.engine_level", "Chapter 3"},
                    {"worlds\\albion\\tombs\\fairfaxcastlegardens\\fairfaxtomb\\defaultscenario\\defaultscenario.engine_level", "Fairfax Tomb"},
                }},
                {"Tattered Spire", {
                    {"worlds\\albion\\tatteredspire\\chapter3\\chapter3.engine_level", "Chapter 3"},
                    {"worlds\\albion\\tatteredspire\\chapter4\\chapter4.engine_level", "Chapter 4"},
                    {"worlds\\albion\\tatteredspire\\defaultscenario\\defaultscenario.engine_level", "Tattered Spire"},
                }},
                {"Mystery Island", {
                    {"worlds\\albion\\mysteryisland\\defaultscenario\\defaultscenario.engine_level", "Mystery Island"},
                    {"worlds\\albion\\mysteryisland\\summer\\summer.engine_level", "Summer"},
                    {"worlds\\albion\\mysteryisland\\winter\\winter.engine_level", "Winter"},
                }},
                {"Shrines", {
                    {"worlds\\albion\\summershrine\\defaultscenario\\defaultscenario.engine_level", "Summer Shrine"},
                    {"worlds\\albion\\wintershrine\\defaultscenario\\defaultscenario.engine_level", "Winter Shrine"},
                }},
                {"Other", {
                    {"worlds\\albion\\templeofevil\\defaultscenario\\defaultscenario.engine_level", "Temple of Evil"},
                    {"worlds\\albion\\dreamworld\\defaultscenario\\defaultscenario.engine_level", "Dreamworld"},
                    {"worlds\\albion\\crucible\\defaultscenario\\defaultscenario.engine_level", "Crucible"},
                    {"worlds\\albion\\chamberofseasons\\defaultscenario\\defaultscenario.engine_level", "Chamber of Seasons"},
                    {"worlds\\albion\\caves\\gargoylescave\\defaultscenario\\defaultscenario.engine_level", "Gargoyle's Cave"},
                }},
                {"Demon Doors", {
                    {"worlds\\albion\\demondoors\\bloodstonedd\\defaultscenario\\defaultscenario.engine_level", "Bloodstone Demon Door"},
                    {"worlds\\albion\\demondoors\\bowerlakedd\\defaultscenario\\defaultscenario.engine_level", "Bower Lake Demon Door"},
                    {"worlds\\albion\\demondoors\\brightwooddd\\defaultscenario\\defaultscenario.engine_level", "Brightwood Demon Door"},
                    {"worlds\\albion\\demondoors\\deepwooddd\\defaultscenario\\defaultscenario.engine_level", "Deepwood Demon Door"},
                    {"worlds\\albion\\demondoors\\dunecrestdd\\defaultscenario\\defaultscenario.engine_level", "Dunecrest Demon Door"},
                    {"worlds\\albion\\demondoors\\homestead\\defaultscenario\\defaultscenario.engine_level", "Homestead Demon Door"},
                    {"worlds\\albion\\demondoors\\marcusmemorial\\defaultscenario\\defaultscenario.engine_level", "Marcus Memorial Demon Door"},
                    {"worlds\\albion\\demondoors\\ravenscardd\\defaultscenario\\defaultscenario.engine_level", "Ravenscar Demon Door"},
                    {"worlds\\albion\\demondoors\\westcliffdd\\defaultscenario\\defaultscenario.engine_level", "Westcliffe Demon Door"},
                }},
                {"DLC", {
                    {"worlds\\albion\\dlc2\\dlc2_colosseum\\defaultscenario\\defaultscenario.engine_level", "Colosseum"},
                    {"worlds\\albion\\dlc2\\dlc2_future\\defaultscenario\\defaultscenario.engine_level", "Future"},
                    {"worlds\\albion\\dlc2\\dlc2_past\\defaultscenario\\defaultscenario.engine_level", "Past"},
                    {"worlds\\albion\\dlc2\\dlc2_present\\defaultscenario\\defaultscenario.engine_level", "Present"},
                }},
            };

            /* Normalize a path for matching: lowercase + backslashes. */
            auto norm = [](std::string s) -> std::string {
                std::transform(s.begin(), s.end(), s.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                std::replace(s.begin(), s.end(), '/', '\\');
                return s;
            };

            /* Build a path → friendly_name map for this frame, and
               figure out which level entries are "uncategorized".    */
            std::unordered_map<std::string, std::string> path_to_name;
            for (const auto& g : kLevelGroups) {
                for (const auto& m : g.entries) {
                    path_to_name[norm(m.path)] = m.name;
                }
            }

            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##level_filter", "Filter",
                                     &S.level_filter);
            std::string flow = S.level_filter;
            std::transform(flow.begin(), flow.end(), flow.begin(), ::tolower);

            if (S.dev_mode) {
                ImGui::TextDisabled("%zu entries indexed",
                                    S.all_level_files.size());
                ImGui::Separator();
            }

            ImGui::BeginChild("levels_list", ImVec2(0, 0), false);
            if (S.all_level_files.empty()) {
                ImGui::TextDisabled("No .engine_level files indexed yet.");
                ImGui::TextDisabled("Open a Fable 2 root to populate the list.");
            } else {
                /* Per-row drawer reused across groups. */
                auto draw_entry = [&](const FlatAssetEntry& e,
                                      const std::string& friendly)
                {
                    ImGui::PushID(&e);
                    if (ImGui::Selectable(friendly.c_str(), false,
                                          ImGuiSelectableFlags_SpanAllColumns))
                    {
                        Level::Open(e);
                    }
                    if (ImGui::BeginPopupContextItem("##lvl_ctx")) {
                        if (ImGui::MenuItem("View Heightmap")) {
                            std::vector<uint8_t> rgba;
                            int hw = 0, hh = 0;
                            if (Level::RenderHeightmapToRGBA(e, rgba, hw, hh)) {
                                extern std::atomic<bool>    g_pending_heightmap_view_load;
                                extern std::vector<uint8_t> g_pending_heightmap_view_rgba;
                                extern int                  g_pending_heightmap_view_w;
                                extern int                  g_pending_heightmap_view_h;
                                extern std::string          g_pending_heightmap_view_name;
                                g_pending_heightmap_view_rgba = std::move(rgba);
                                g_pending_heightmap_view_w    = hw;
                                g_pending_heightmap_view_h    = hh;
                                g_pending_heightmap_view_name = friendly;
                                g_pending_heightmap_view_load = true;
                            }
                        }
                        ImGui::EndPopup();
                    }
                    if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted(e.full_path.c_str());
                        ImGui::Text("BNK: %s",
                            std::filesystem::path(e.bnk_path)
                                .filename().string().c_str());
                        ImGui::Text("Size: %u bytes", e.size);
                        ImGui::EndTooltip();
                    }
                    ImGui::PopID();
                };

                /* Index entries by normalized path so we can resolve
                   each mapping entry to its underlying FlatAssetEntry. */
                std::unordered_map<std::string, const FlatAssetEntry*> by_path;
                for (const auto& e : S.all_level_files) {
                    by_path[norm(e.full_path)] = &e;
                }

                /* Track which entries we've placed under a heading so
                   leftovers can fall under a fallback "Uncategorized".  */
                std::unordered_set<const FlatAssetEntry*> placed;

                /* Filter helper: returns true if entry matches the
                   user's filter (against either friendly name or
                   full path, case-insensitive).                       */
                auto matches_filter = [&](const std::string& friendly,
                                          const std::string& full_path) {
                    if (flow.empty()) return true;
                    auto contains = [&](const std::string& s) {
                        std::string l = s;
                        std::transform(l.begin(), l.end(), l.begin(),
                            [](unsigned char c){ return std::tolower(c); });
                        return l.find(flow) != std::string::npos;
                    };
                    return contains(friendly) || contains(full_path);
                };

                for (const auto& g : kLevelGroups) {
                    /* Collect entries that match filter + exist on disk. */
                    std::vector<std::pair<const FlatAssetEntry*, std::string>> rows;
                    for (const auto& m : g.entries) {
                        auto it = by_path.find(norm(m.path));
                        if (it == by_path.end()) continue;
                        if (!matches_filter(m.name, it->second->full_path)) continue;
                        rows.push_back({it->second, std::string(m.name)});
                        placed.insert(it->second);
                    }
                    if (rows.empty()) continue;

                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImVec4(1.0f, 0.84f, 0.0f, 1.0f));   // gold heading
                    ImGui::TextUnformatted(g.heading);
                    ImGui::PopStyleColor();
                    ImGui::Indent(8.0f);
                    for (const auto& [e, friendly] : rows) {
                        draw_entry(*e, friendly);
                    }
                    ImGui::Unindent(8.0f);
                    ImGui::Spacing();
                }

                /* Anything not in the mapping table — show under
                   "Uncategorized" so it's still reachable.  Useful
                   while the table is incomplete or for modded ROMs. */
                std::vector<std::pair<const FlatAssetEntry*, std::string>> leftover;
                for (const auto& e : S.all_level_files) {
                    if (placed.count(&e)) continue;
                    std::filesystem::path p = e.full_path;
                    auto parent = p.parent_path().filename().string();
                    std::string label = parent.empty()
                        ? e.name : parent + " — " + e.name;
                    if (!matches_filter(label, e.full_path)) continue;
                    leftover.push_back({&e, label});
                }
                if (!leftover.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                    ImGui::TextUnformatted("Uncategorized");
                    ImGui::PopStyleColor();
                    ImGui::Indent(8.0f);
                    for (const auto& [e, label] : leftover) {
                        draw_entry(*e, label);
                    }
                    ImGui::Unindent(8.0f);
                }
            }
            ImGui::EndChild();
        }

    ImGui::EndChild();
}
