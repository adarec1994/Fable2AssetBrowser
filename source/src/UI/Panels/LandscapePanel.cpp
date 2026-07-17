#include "LandscapePanel.h"

#include "../OutputLog.h"
#include "IconsFontAwesome6.h"
#include "../Layout/RenderPanel.h"
#include "../../Level/Core/LevelLoader.h"
#include "../../Level/Creation/LandscapeAuthoring.h"
#include "../../Level/Creation/NewLevel.h"
#include "../../Level/Creation/SkyAuthoring.h"
#include "../../Level/Creation/WaterAuthoring.h"
#include "../../Level/Terrain/TerrainPaint.h"
#include "../../Utilities/State.h"

#include "../ModelPreview.h"
#include "../../BNKCore.cpp"

#include "imgui.h"
#include "imgui_stdlib.h"
#include "ImGuiFileDialog.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <d3d11.h>
#endif

namespace LandscapePanel {

namespace {

Level::Creation::LandscapeParams s_params;

int         s_mode = 0;
int         s_manage_tool = 0;   
int         s_sculpt_tool = 0;
int         s_paint_tool = 0;
float       s_tool_strength = 0.3f;
float       s_brush_size = 8.0f;
float       s_brush_falloff = 0.5f;
std::string s_heightmap_path;



std::vector<Level::Creation::SkyThemeOption> s_sky_options;
std::string s_sky_options_data_dir = "\x01unloaded";
uint32_t    s_sky_selected = 0;
uint32_t    s_sky_current = 0;
std::string s_sky_current_level;


float       s_water_height = 1.0f;


std::string s_entity_filter;




struct PaintTexOption {
    std::string full_path;
    std::string bnk_path;
    int         file_index = -1;
    uint32_t    size = 0;
    std::string label;
    std::string low;
};
std::vector<PaintTexOption> s_paint_tex_options;
size_t s_paint_tex_options_key = (size_t)-1;
void*  s_thumb_device = nullptr;

void rebuild_paint_tex_options() {
    if (s_paint_tex_options_key == S.all_tex_files.size()) return;
    s_paint_tex_options_key = S.all_tex_files.size();
    s_paint_tex_options.clear();

    std::unordered_map<std::string, size_t> by_leaf;
    for (const FlatAssetEntry& e : S.all_tex_files) {
        std::string low = e.full_path;
        std::transform(low.begin(), low.end(), low.begin(),
                       [](unsigned char c) {
                           return c == '\\' ? '/' : (char)std::tolower(c);
                       });
        if (low.find("environment") == std::string::npos) continue;
        std::string bnk_low = e.bnk_path;
        std::transform(bnk_low.begin(), bnk_low.end(), bnk_low.begin(),
                       [](unsigned char c) {
                           return (char)std::tolower(c);
                       });
        
        if (bnk_low.find("texture_header") != std::string::npos) {
            continue;
        }
        std::string leaf =
            std::filesystem::path(low).stem().string();
        if (leaf.empty()) continue;

        auto [it, inserted] =
            by_leaf.emplace(leaf, s_paint_tex_options.size());
        if (inserted) {
            PaintTexOption opt;
            opt.full_path = e.full_path;
            opt.bnk_path = e.bnk_path;
            opt.file_index = e.file_index;
            opt.size = e.size;
            opt.label =
                std::filesystem::path(e.name).stem().string();
            opt.low = low;
            s_paint_tex_options.push_back(std::move(opt));
        } else if (e.size > s_paint_tex_options[it->second].size) {
            
            PaintTexOption& opt = s_paint_tex_options[it->second];
            opt.full_path = e.full_path;
            opt.bnk_path = e.bnk_path;
            opt.file_index = e.file_index;
            opt.size = e.size;
            opt.low = low;
        }
    }
    std::sort(s_paint_tex_options.begin(), s_paint_tex_options.end(),
              [](const PaintTexOption& a, const PaintTexOption& b) {
                  return a.label < b.label;
              });
}

#ifdef _WIN32
ID3D11ShaderResourceView* paint_tex_thumbnail(
    const PaintTexOption& opt) {
    static std::unordered_map<std::string, ID3D11ShaderResourceView*>
        s_cache;
    auto it = s_cache.find(opt.full_path);
    if (it != s_cache.end()) return it->second;

    ID3D11ShaderResourceView* srv = nullptr;
    auto* device = static_cast<ID3D11Device*>(s_thumb_device);
    if (device) {
        try {
            const std::vector<uint8_t> blob =
                BnkCache::extract_bytes(opt.bnk_path, opt.file_index);
            std::vector<unsigned char> copy(blob.begin(), blob.end());
            std::vector<uint8_t> rgba;
            int w = 0, h = 0;
            bool has_alpha = false;
            if (decode_tex_to_rgba(copy, rgba, w, h, &has_alpha) &&
                w > 0 && h > 0) {
                constexpr int kThumb = 48;
                std::vector<uint8_t> thumb_rgba((size_t)kThumb * kThumb *
                                                4);
                for (int y = 0; y < kThumb; ++y) {
                    const int sy = y * h / kThumb;
                    for (int x = 0; x < kThumb; ++x) {
                        const int sx = x * w / kThumb;
                        std::memcpy(
                            thumb_rgba.data() +
                                ((size_t)y * kThumb + x) * 4,
                            rgba.data() + ((size_t)sy * w + sx) * 4, 4);
                    }
                }
                srv = create_srv_from_rgba(device, kThumb, kThumb,
                                           thumb_rgba);
            }
        } catch (...) {
        }
    }
    s_cache.emplace(opt.full_path, srv);
    return srv;
}
#endif

const ImVec4 kAccent(0.16f, 0.42f, 0.78f, 1.0f);
const ImVec4 kAccentHover(0.22f, 0.50f, 0.88f, 1.0f);


bool mode_button(const char* label, bool active) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccent);
    }
    const bool clicked = ImGui::Button(label);
    if (active) ImGui::PopStyleColor(3);
    return clicked;
}


bool tool_button(const char* icon, const char* caption, bool active) {
    const float w = 54.0f;
    ImGui::BeginGroup();
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccent);
    }
    const std::string id = std::string(icon) + "##ls_tool_" + caption;
    const bool clicked = ImGui::Button(id.c_str(), ImVec2(w, 30.0f));
    if (active) ImGui::PopStyleColor(3);
    const float tw = ImGui::CalcTextSize(caption).x;
    if (tw < w) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (w - tw) * 0.5f);
    }
    ImGui::TextDisabled("%s", caption);
    ImGui::EndGroup();
    return clicked;
}


bool begin_props(const char* id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit)) {
        return false;
    }
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed,
                            100.0f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void prop_label(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void draw_size_rows() {
    prop_label("Width");
    ImGui::DragInt("##ls_w", &s_params.grid_w, 1.0f, 2, 1025,
                   "%d samples");
    prop_label("Length");
    ImGui::DragInt("##ls_h", &s_params.grid_h, 1.0f, 2, 1025,
                   "%d samples");
    prop_label("Tile Size");
    ImGui::DragFloat("##ls_tile", &s_params.tile_size, 0.05f, 0.0f,
                     256.0f, "%.2f m");
}

void draw_manage(const FlatAssetEntry& entry) {
    if (tool_button(ICON_FA_PLUS, "New", s_manage_tool == 0)) {
        s_manage_tool = 0;
    }
    ImGui::SameLine();
    if (tool_button(ICON_FA_FILE_IMPORT, "Import", s_manage_tool == 1)) {
        s_manage_tool = 1;
    }
    ImGui::Separator();

    const bool busy = Level::IsAsyncLoadInProgress();

    if (s_manage_tool == 1 &&
        ImGui::CollapsingHeader("Import from File",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        if (begin_props("##ls_import_props")) {
            prop_label("Heightmap File");
            {
                const std::string leaf =
                    s_heightmap_path.empty()
                        ? std::string()
                        : std::filesystem::path(s_heightmap_path)
                              .filename()
                              .string();
                const float browse_w = ImGui::GetFrameHeight() * 1.6f;
                ImGui::SetNextItemWidth(
                    -(browse_w + ImGui::GetStyle().ItemInnerSpacing.x));
                ImGui::InputText("##ls_hm_file",
                                 const_cast<char*>(leaf.c_str()),
                                 leaf.size() + 1,
                                 ImGuiInputTextFlags_ReadOnly);
                if (ImGui::IsItemHovered() && !s_heightmap_path.empty()) {
                    ImGui::SetTooltip("%s", s_heightmap_path.c_str());
                }
                ImGui::SameLine(0.0f,
                                ImGui::GetStyle().ItemInnerSpacing.x);
                if (ImGui::Button("...", ImVec2(browse_w, 0.0f))) {
                    IGFD::FileDialogConfig cfg;
                    cfg.path = s_heightmap_path.empty()
                                   ? (S.export_dir.empty() ? "."
                                                           : S.export_dir)
                                   : std::filesystem::path(s_heightmap_path)
                                         .parent_path()
                                         .string();
                    ImGuiFileDialog::Instance()->OpenDialog(
                        "PickHeightmapImage", "Select heightmap",
                        "Heightmaps (*.png *.tif *.tiff *.jpg *.jpeg "
                        "*.bmp *.tga *.psd *.r16 *.raw){.png,.tif,.tiff,"
                        ".jpg,.jpeg,.bmp,.tga,.psd,.r16,.raw},.*",
                        cfg);
                }
            }
            prop_label("Min Height");
            ImGui::DragFloat("##ls_min", &s_params.min_height, 0.1f,
                             -1024.0f, 1024.0f, "%.1f m");
            prop_label("Max Height");
            ImGui::DragFloat("##ls_max", &s_params.max_height, 0.1f,
                             -1024.0f, 4096.0f, "%.1f m");
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("New Landscape",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        if (begin_props("##ls_size_props")) {
            if (s_manage_tool == 0) {
                prop_label("Height");
                ImGui::DragFloat("##ls_flat", &s_params.base_height,
                                 0.05f, -256.0f, 1024.0f, "%.2f m");
            }
            draw_size_rows();
            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    const bool blocked =
        busy || (s_manage_tool == 1 && s_heightmap_path.empty());
    if (blocked) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
    if (ImGui::Button(s_manage_tool == 0 ? "Create" : "Import",
                      ImVec2(-FLT_MIN, 0.0f))) {
        std::string error;
        const bool ok =
            s_manage_tool == 0
                ? Level::Creation::CreateFlatLandscape(entry, s_params,
                                                       error)
                : Level::Creation::ImportHeightmapLandscape(
                      entry, s_params, s_heightmap_path, error);
        if (!ok) OutputLog::error("landscape: " + error);
    }
    ImGui::PopStyleColor(2);
    if (blocked) ImGui::EndDisabled();
}

void draw_sculpt() {
    if (tool_button(ICON_FA_MOUNTAIN, "Sculpt", s_sculpt_tool == 0)) {
        s_sculpt_tool = 0;
    }
    ImGui::SameLine();
    if (tool_button(ICON_FA_DROPLET, "Smooth", s_sculpt_tool == 1)) {
        s_sculpt_tool = 1;
    }
    ImGui::SameLine();
    if (tool_button(ICON_FA_RULER_HORIZONTAL, "Flatten",
                    s_sculpt_tool == 2)) {
        s_sculpt_tool = 2;
    }
    ImGui::SameLine();
    if (tool_button(ICON_FA_SHUFFLE, "Noise", s_sculpt_tool == 3)) {
        s_sculpt_tool = 3;
    }
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Tool Settings",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        if (begin_props("##ls_sculpt_tool")) {
            prop_label("Tool Strength");
            ImGui::SliderFloat("##ls_strength", &s_tool_strength, 0.0f,
                               1.0f, "%.2f");
            ImGui::EndTable();
        }
    }
    if (ImGui::CollapsingHeader("Brush Settings",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        if (begin_props("##ls_sculpt_brush")) {
            prop_label("Brush Size");
            ImGui::DragFloat("##ls_bsize", &s_brush_size, 0.1f, 0.5f,
                             256.0f, "%.1f m");
            prop_label("Brush Falloff");
            ImGui::SliderFloat("##ls_bfall", &s_brush_falloff, 0.0f, 1.0f,
                               "%.2f");
            ImGui::EndTable();
        }
    }
}

void draw_paint() {
    tool_button(ICON_FA_PAINTBRUSH, "Paint", true);
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Tool Settings",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        if (begin_props("##ls_paint_tool")) {
            prop_label("Tool Strength");
            ImGui::SliderFloat("##ls_pstrength", &s_tool_strength, 0.0f,
                               1.0f, "%.2f");
            ImGui::EndTable();
        }
    }
    if (ImGui::CollapsingHeader("Brush Settings",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        if (begin_props("##ls_paint_brush")) {
            prop_label("Brush Size");
            ImGui::DragFloat("##ls_pbsize", &s_brush_size, 0.1f, 0.5f,
                             256.0f, "%.1f m");
            prop_label("Brush Falloff");
            ImGui::SliderFloat("##ls_pbfall", &s_brush_falloff, 0.0f,
                               1.0f, "%.2f");
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Target Layers",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& layers = TerrainPaint::Layers();
        const int active = TerrainPaint::ActiveLayer();
        int remove_index = -1;
        for (size_t i = 0; i < layers.size(); ++i) {
            ImGui::PushID((int)i);
            const std::string leaf =
                std::filesystem::path(layers[i].tex_path)
                    .stem()
                    .string();
            if (ImGui::Selectable(leaf.c_str(), (int)i == active,
                                  ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(ImGui::GetContentRegionAvail().x -
                                             112.0f,
                                         0.0f))) {
                TerrainPaint::SetActiveLayer((int)i);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", layers[i].tex_path.c_str());
            }
            ImGui::SameLine();
            float tiling = layers[i].tiling;
            ImGui::SetNextItemWidth(76.0f);
            if (ImGui::DragFloat("##tiling", &tiling, 0.1f, 0.5f, 256.0f,
                                 "%.1f m")) {
                TerrainPaint::SetLayerTiling((int)i, tiling);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) remove_index = (int)i;
            ImGui::PopID();
        }
        if (remove_index >= 0) TerrainPaint::RemoveLayer(remove_index);

        const bool full =
            (int)layers.size() >= TerrainPaint::kMaxLayers;
        if (full) ImGui::BeginDisabled();
        if (ImGui::Button("+ Add Layer", ImVec2(-FLT_MIN, 0.0f))) {
            ImGui::OpenPopup("##paint_add_layer");
        }
        if (full) ImGui::EndDisabled();

        if (ImGui::BeginPopup("##paint_add_layer")) {
            static std::string s_tex_filter;
            ImGui::SetNextItemWidth(300.0f);
            ImGui::InputTextWithHint("##paint_tex_filter", "Search",
                                     &s_tex_filter);
            std::string filter = s_tex_filter;
            std::transform(filter.begin(), filter.end(), filter.begin(),
                           [](unsigned char c) {
                               return (char)std::tolower(c);
                           });

            rebuild_paint_tex_options();
            std::vector<int> vis;
            vis.reserve(s_paint_tex_options.size());
            for (size_t i = 0; i < s_paint_tex_options.size(); ++i) {
                if (!filter.empty() &&
                    s_paint_tex_options[i].low.find(filter) ==
                        std::string::npos) {
                    continue;
                }
                vis.push_back((int)i);
            }

            ImGui::BeginChild("##paint_tex_list", ImVec2(300.0f, 340.0f),
                              true);
            constexpr float kRowH = 44.0f;
            ImGuiListClipper clipper;
            clipper.Begin((int)vis.size(), kRowH);
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart;
                     row < clipper.DisplayEnd; ++row) {
                    const PaintTexOption& opt =
                        s_paint_tex_options[(size_t)vis[(size_t)row]];
                    ImGui::PushID(vis[(size_t)row]);
#ifdef _WIN32
                    ID3D11ShaderResourceView* thumb =
                        paint_tex_thumbnail(opt);
                    if (thumb) {
                        ImGui::Image((ImTextureID)thumb,
                                     ImVec2(40.0f, 40.0f));
                    } else {
                        ImGui::Dummy(ImVec2(40.0f, 40.0f));
                    }
                    ImGui::SameLine();
#endif
                    if (ImGui::Selectable(opt.label.c_str(), false, 0,
                                          ImVec2(0.0f, kRowH - 4.0f))) {
                        if (TerrainPaint::AddLayer(opt.full_path) < 0) {
                            OutputLog::warn(
                                "paint: layer not added (duplicate or "
                                "at the 16-layer limit)");
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", opt.full_path.c_str());
                    }
                    ImGui::PopID();
                }
            }
            clipper.End();
            ImGui::EndChild();
            ImGui::EndPopup();
        }
    }
}

void draw_sky(const FlatAssetEntry& entry) {
    
    
    const std::string data_dir = Level::Creation::ResolveGameDataDir();
    const std::string source_key =
        data_dir + "#" + std::to_string(S.all_gdb_files.size());
    if (source_key != s_sky_options_data_dir) {
        s_sky_options_data_dir = source_key;
        s_sky_options.clear();
        if (!data_dir.empty() && !S.all_gdb_files.empty()) {
            std::string err;
            if (!Level::Creation::ListSkyThemes(s_sky_options, err)) {
                OutputLog::error("sky: " + err);
            }
        }
    }
    if (s_sky_current_level != entry.full_path) {
        s_sky_current_level = entry.full_path;
        s_sky_current = Level::Creation::CurrentSkyTheme(entry);
        s_sky_selected = s_sky_current;
    }

    if (ImGui::CollapsingHeader("Sky Theme",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        const float list_h =
            std::max(120.0f, ImGui::GetContentRegionAvail().y -
                                 ImGui::GetFrameHeight() * 2.2f);
        ImGui::BeginChild("##sky_theme_list", ImVec2(0, list_h), true);
        for (const auto& option : s_sky_options) {
            const bool selected = option.day_set_hash == s_sky_selected;
            std::string label = option.name;
            if (option.day_set_hash == s_sky_current) label += "  (active)";
            if (ImGui::Selectable(label.c_str(), selected)) {
                s_sky_selected = option.day_set_hash;
            }
        }
        ImGui::EndChild();
    }

    const bool busy = Level::IsAsyncLoadInProgress();
    const bool blocked =
        busy || s_sky_selected == 0 || s_sky_selected == s_sky_current;
    if (blocked) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
    if (ImGui::Button("Apply Sky", ImVec2(-FLT_MIN, 0.0f))) {
        std::string err;
        if (Level::Creation::ApplySkyTheme(entry, s_sky_selected, err)) {
            s_sky_current = s_sky_selected;
        } else {
            OutputLog::error("sky: " + err);
        }
    }
    ImGui::PopStyleColor(2);
    if (blocked) ImGui::EndDisabled();
}

void draw_water(const FlatAssetEntry& entry) {
    if (ImGui::CollapsingHeader("Water",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        if (begin_props("##water_props")) {
            prop_label("Height");
            ImGui::DragFloat("##water_h", &s_water_height, 0.05f,
                             -256.0f, 1024.0f, "%.2f m");
            ImGui::EndTable();
        }
    }

    const bool busy = Level::IsAsyncLoadInProgress();
    if (busy) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
    if (ImGui::Button("Create Water", ImVec2(-FLT_MIN, 0.0f))) {
        std::string err;
        if (!Level::Creation::CreateWaterPlane(entry, s_water_height,
                                               err)) {
            OutputLog::error("water: " + err);
        }
    }
    ImGui::PopStyleColor(2);
    if (ImGui::Button("Remove Water", ImVec2(-FLT_MIN, 0.0f))) {
        std::string err;
        if (!Level::Creation::RemoveWater(entry, err)) {
            OutputLog::error("water: " + err);
        }
    }
    if (busy) ImGui::EndDisabled();
}

void draw_entities(const FlatAssetEntry& entry) {
    (void)entry;
    
    if (ImGui::CollapsingHeader("Player Start",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        bool found = false;
        for (size_t mi = 0; mi < g_level_spawn_markers.size(); ++mi) {
            const LevelSpawnMarker& m = g_level_spawn_markers[mi];
            if (!is_player_start_marker(m)) continue;
            found = true;
            char label[192];
            std::snprintf(label, sizeof(label),
                          "%s  (%.1f, %.1f, %.1f)##ps_%zu",
                          m.name.empty() ? "Player Start"
                                         : m.name.c_str(),
                          m.x, m.y, m.z, mi);
            if (ImGui::Selectable(label)) {
                UI::select_level_marker(mi);
            }
        }
        if (!found) {
            ImGui::TextDisabled("(no player start in this level)");
        }
    }

    if (ImGui::CollapsingHeader("Place Entities",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##entity_filter", "Filter",
                                 &s_entity_filter);
        std::string filter = s_entity_filter;
        std::transform(filter.begin(), filter.end(), filter.begin(),
                       [](unsigned char c) {
                           return (char)std::tolower(c);
                       });

        ImGui::BeginChild("##entity_place_list", ImVec2(0, 0), true);
        for (size_t i = 0; i < g_global_entity_catalog.size(); ++i) {
            const Gdb::CreatureCatalogEntry& e =
                g_global_entity_catalog[i];
            const std::string& shown =
                e.display_name.empty() ? e.name : e.display_name;
            if (!filter.empty()) {
                std::string hay = shown + " " + e.name;
                std::transform(hay.begin(), hay.end(), hay.begin(),
                               [](unsigned char c) {
                                   return (char)std::tolower(c);
                               });
                if (hay.find(filter) == std::string::npos) continue;
            }
            const bool is_npc =
                e.kind == Gdb::EntityCatalogKind::Creature;
            const FlatAssetEntry* model =
                e.model_hashes.empty()
                    ? nullptr
                    : FindGlobalModelAssetByPathHash(e.model_hashes[0]);
            if (!is_npc && !model) continue;

            ImGui::PushID((int)i);
            ImGui::Selectable(shown.c_str());
            if (ImGui::BeginDragDropSource()) {
                if (is_npc) {
                    const int idx = (int)i;
                    ImGui::SetDragDropPayload("F2_ENTITY_NPC", &idx,
                                              sizeof(idx));
                } else {
                    ImGui::SetDragDropPayload(
                        "F2_MODEL", model->full_path.c_str(),
                        model->full_path.size());
                }
                ImGui::TextUnformatted(shown.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(is_npc
                                      ? "Drag into the level (NPC)"
                                      : "Drag into the level (prop)");
            }
            ImGui::SameLine(
                std::max(120.0f,
                         ImGui::GetContentRegionAvail().x - 34.0f));
            ImGui::TextDisabled(is_npc ? "NPC" : "Prop");
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
}

}

bool AppliesTo(const FlatAssetEntry& entry) {
    return Level::Creation::IsCustomLooseLevel(entry);
}

bool  InSculptMode()  { return s_mode == 1; }
bool  InPaintMode()   { return s_mode == 2; }
int   SculptTool()    { return s_sculpt_tool; }
float BrushSize()     { return s_brush_size; }
float ToolStrength()  { return s_tool_strength; }
float BrushFalloff()  { return s_brush_falloff; }

void DrawSidePanel(const FlatAssetEntry& entry, void* d3d_device) {
    s_thumb_device = d3d_device;
    ImGui::BeginChild("##landscape_side", ImVec2(360.0f, 0.0f), true);

    
    {
        static const char* kModes[6] = {"Manage", "Sculpt", "Paint",
                                        "Sky",    "Water",  "Entities"};
        const ImGuiStyle& style = ImGui::GetStyle();
        const float avail = ImGui::GetContentRegionAvail().x;
        for (int row = 0; row < 2; ++row) {
            float total = 0.0f;
            for (int i = row * 3; i < row * 3 + 3; ++i) {
                total += ImGui::CalcTextSize(kModes[i]).x +
                         style.FramePadding.x * 2.0f;
                if (i > row * 3) total += style.ItemSpacing.x;
            }
            if (total < avail) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                     (avail - total) * 0.5f);
            }
            for (int i = row * 3; i < row * 3 + 3; ++i) {
                if (i > row * 3) ImGui::SameLine();
                if (mode_button(kModes[i], s_mode == i)) s_mode = i;
            }
        }
    }
    ImGui::Separator();

    switch (s_mode) {
        case 0: draw_manage(entry); break;
        case 1: draw_sculpt(); break;
        case 2: draw_paint(); break;
        case 3: draw_sky(entry); break;
        case 4: draw_water(entry); break;
        case 5: draw_entities(entry); break;
        default: break;
    }

    const ImVec2 min_size(560, 380);
    const ImVec2 max_size(FLT_MAX, FLT_MAX);
    if (ImGuiFileDialog::Instance()->Display(
            "PickHeightmapImage", ImGuiWindowFlags_NoCollapse, min_size,
            max_size)) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            s_heightmap_path =
                ImGuiFileDialog::Instance()->GetFilePathName();
        }
        ImGuiFileDialog::Instance()->Close();
    }

    ImGui::EndChild();
}

}
