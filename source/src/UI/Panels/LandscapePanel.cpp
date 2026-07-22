#include "LandscapePanel.h"

#include "../OutputLog.h"
#include "IconsFontAwesome6.h"
#include "../Layout/RenderPanel.h"
#include "../../Level/Core/LevelLoader.h"
#include "../../Level/Creation/LandscapeAuthoring.h"
#include "../../Level/Creation/GameRegistry.h"
#include "../../Level/Creation/NewLevel.h"
#include "../../Level/Creation/FoliageAuthoring.h"
#include "../../Level/Creation/SkyAuthoring.h"
#include "../../Level/Creation/WaterAuthoring.h"
#include "../../Level/Editing/LevelEdit.h"
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
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
float       s_paint_noise_scale = 12.0f;
float       s_paint_noise_coverage = 0.45f;
float       s_tool_strength = 0.3f;
float       s_brush_size = 8.0f;
float       s_brush_falloff = 0.5f;
std::string s_heightmap_path;
std::string s_level_name_region;
std::string s_level_display_name;
std::string s_level_saved_display_name;



std::vector<Level::Creation::SkyThemeOption> s_sky_options;
std::string s_sky_options_data_dir = "\x01unloaded";
uint32_t    s_sky_selected = 0;
uint32_t    s_sky_current = 0;
std::string s_sky_current_level;


float       s_water_height = 1.0f;


std::string s_entity_filter;

struct FoliagePaletteSlot {
    FoliagePaintEntry entry;
    bool enabled = true;
};
std::vector<FoliagePaletteSlot> s_foliage_palette;
int         s_foliage_active = -1;
std::string s_foliage_filter;
int         s_foliage_tool = 0;
float       s_foliage_radius = 6.0f;

}
}
void foliage_preload_model(const std::string& model_path);
namespace LandscapePanel {
namespace {

int foliage_palette_find(const std::string& model_path) {
    for (int i = 0; i < (int)s_foliage_palette.size(); ++i) {
        if (s_foliage_palette[(size_t)i].entry.model_path == model_path) {
            return i;
        }
    }
    return -1;
}




struct PaintTexOption {
    std::string full_path;
    std::string normal_path;
    std::string bnk_path;
    int         file_index = -1;
    uint32_t    size = 0;
    std::string label;
    std::string low;
};
std::vector<PaintTexOption> s_paint_tex_options;
size_t s_paint_tex_options_key = (size_t)-1;
void*  s_thumb_device = nullptr;

enum class PaintTexRole { Unknown, Diffuse, Normal, Ignore };

std::string lower_slash(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return c == '\\' ? '/' : char(std::tolower(c));
                   });
    return value;
}

bool is_ground_texture_path(const std::string& path) {
    return path.rfind("art/environment/_groundtextures/", 0) == 0 ||
           path.find("/art/environment/_groundtextures/") !=
               std::string::npos;
}

bool has_word(const std::string& value,
              std::initializer_list<const char*> words) {
    size_t start = 0;
    while (start < value.size()) {
        while (start < value.size() &&
               !std::isalnum(static_cast<unsigned char>(value[start]))) {
            ++start;
        }
        size_t end = start;
        while (end < value.size() &&
               std::isalnum(static_cast<unsigned char>(value[end]))) {
            ++end;
        }
        const std::string token = value.substr(start, end - start);
        for (const char* word : words) {
            if (token == word) return true;
        }
        start = end;
    }
    return false;
}

PaintTexRole paint_texture_role(const std::string& path) {
    const std::string stem =
        std::filesystem::path(path).stem().string();
    if (has_word(stem, {"normal", "normalmap", "norm", "nrm", "nm",
                        "nor", "n"}) ||
        path.find("/normal/") != std::string::npos ||
        path.find("/normals/") != std::string::npos) {
        return PaintTexRole::Normal;
    }
    if (has_word(stem, {"diffuse", "diff", "dif", "albedo", "colour",
                        "color", "col", "d"}) ||
        path.find("/diffuse/") != std::string::npos ||
        path.find("/diffuses/") != std::string::npos ||
        path.find("/albedo/") != std::string::npos) {
        return PaintTexRole::Diffuse;
    }
    if (has_word(stem, {"specular", "spec", "rough", "roughness",
                        "metal", "metallic", "height", "mask",
                        "occlusion", "gloss", "bump", "ao", "rph"})) {
        return PaintTexRole::Ignore;
    }
    return PaintTexRole::Unknown;
}

std::string paint_texture_pair_key(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    std::string folder =
        slash == std::string::npos ? std::string() : path.substr(0, slash);
    std::string stem =
        std::filesystem::path(path).stem().string();
    for (const char* part : {"/diffuse", "/diffuses", "/albedo",
                             "/normal", "/normals"}) {
        size_t pos = std::string::npos;
        while ((pos = folder.find(part)) != std::string::npos) {
            folder.erase(pos, std::strlen(part));
        }
    }
    std::string base;
    size_t start = 0;
    while (start < stem.size()) {
        while (start < stem.size() &&
               !std::isalnum(static_cast<unsigned char>(stem[start]))) {
            ++start;
        }
        size_t end = start;
        while (end < stem.size() &&
               std::isalnum(static_cast<unsigned char>(stem[end]))) {
            ++end;
        }
        const std::string token = stem.substr(start, end - start);
        if (!has_word(token,
                      {"normal", "normalmap", "norm", "nrm", "nm",
                       "nor", "n", "diffuse", "diff", "dif",
                       "albedo", "colour", "color", "col", "d",
                       "specular", "spec", "rough", "roughness",
                       "metal", "metallic", "height", "mask",
                       "occlusion", "gloss", "bump", "ao", "rph"})) {
            if (!base.empty()) base.push_back('_');
            base += token;
        }
        start = end;
    }
    return folder + "/" + base;
}

void rebuild_paint_tex_options() {
    if (s_paint_tex_options_key == S.all_tex_files.size()) return;
    s_paint_tex_options_key = S.all_tex_files.size();
    s_paint_tex_options.clear();

    std::vector<PaintTexOption> candidates;
    std::vector<PaintTexRole> roles;
    std::unordered_map<std::string, size_t> by_path;
    for (const FlatAssetEntry& e : S.all_tex_files) {
        const std::string low = lower_slash(e.full_path);
        if (!is_ground_texture_path(low)) continue;
        const std::string bnk_low = lower_slash(e.bnk_path);
        if (bnk_low.find("texture_header") != std::string::npos) {
            continue;
        }
        const std::string leaf = std::filesystem::path(low).stem().string();
        if (leaf.empty()) continue;

        auto [it, inserted] = by_path.emplace(low, candidates.size());
        if (inserted) {
            PaintTexOption opt;
            opt.full_path = e.full_path;
            opt.bnk_path = e.bnk_path;
            opt.file_index = e.file_index;
            opt.size = e.size;
            opt.label =
                std::filesystem::path(e.name).stem().string();
            opt.low = low;
            candidates.push_back(std::move(opt));
            roles.push_back(paint_texture_role(low));
        } else if (e.size > candidates[it->second].size) {
            PaintTexOption& opt = candidates[it->second];
            opt.full_path = e.full_path;
            opt.bnk_path = e.bnk_path;
            opt.file_index = e.file_index;
            opt.size = e.size;
            opt.low = low;
        }
    }

    std::unordered_map<std::string, size_t> normal_by_key;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (roles[i] == PaintTexRole::Normal) {
            const std::string key = paint_texture_pair_key(candidates[i].low);
            auto it = normal_by_key.find(key);
            if (it == normal_by_key.end() ||
                candidates[i].size > candidates[it->second].size) {
                normal_by_key[key] = i;
            }
        }
    }
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (roles[i] == PaintTexRole::Normal ||
            roles[i] == PaintTexRole::Ignore) {
            continue;
        }
        const auto normal = normal_by_key.find(
            paint_texture_pair_key(candidates[i].low));
        if (normal != normal_by_key.end()) {
            candidates[i].normal_path =
                candidates[normal->second].full_path;
        }
        s_paint_tex_options.push_back(std::move(candidates[i]));
    }
    std::sort(s_paint_tex_options.begin(), s_paint_tex_options.end(),
              [](const PaintTexOption& a, const PaintTexOption& b) {
                  return a.label < b.label;
              });
}

const PaintTexOption* find_paint_tex_option(const std::string& full_path) {
    const std::string low = lower_slash(full_path);
    for (const PaintTexOption& option : s_paint_tex_options) {
        if (option.low == low) return &option;
    }
    return nullptr;
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
            if (decode_tex_to_rgba(copy, rgba, w, h, &has_alpha, -1) &&
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
    ImGui::Text("%d samples", s_params.grid_w);
    prop_label("Length");
    ImGui::Text("%d samples", s_params.grid_h);
    prop_label("Sample Spacing");
    ImGui::Text("%.2f m", s_params.tile_size);
}

void draw_manage(const FlatAssetEntry& entry) {
    std::string region;
    Level::Creation::IsCustomLooseLevel(entry, &region);
    const std::string data_dir = Level::Creation::ResolveGameDataDir();
    static std::string s_layout_region;
    static bool s_layout_ok = false;
    static int s_layout_w = 0;
    static int s_layout_h = 0;
    static float s_layout_spacing = 0.0f;
    static std::string s_layout_error;
    if (s_layout_region != region) {
        s_layout_region = region;
        s_layout_error.clear();
        s_layout_ok = Level::Creation::GetNativeLandscapeLayout(
            entry, s_layout_w, s_layout_h, s_layout_spacing,
            s_layout_error);
    }
    if (s_layout_ok) {


        s_params.grid_w = s_layout_w;
        s_params.grid_h = s_layout_h;
        s_params.tile_size = s_layout_spacing;
    }
    if (s_level_name_region != region) {
        s_level_name_region = region;
        s_level_display_name =
            Level::Creation::GetCustomLevelDisplayName(data_dir, region);
        s_level_saved_display_name = s_level_display_name;
    }

    if (ImGui::CollapsingHeader("Level Name",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        if (begin_props("##ls_level_name")) {
            prop_label("In-game Name");
            ImGui::InputText("##ls_display_name", &s_level_display_name);
            ImGui::EndTable();
        }
        const bool name_blocked =
            Level::IsAsyncLoadInProgress() || s_level_display_name.empty() ||
            s_level_display_name == s_level_saved_display_name;
        if (name_blocked) ImGui::BeginDisabled();
        if (ImGui::Button("Apply Name", ImVec2(-FLT_MIN, 0.0f))) {
            std::string error;
            if (Level::Creation::SetCustomLevelDisplayName(
                    data_dir, region, s_level_display_name, error)) {
                s_level_display_name =
                    Level::Creation::GetCustomLevelDisplayName(data_dir,
                                                               region);
                s_level_saved_display_name = s_level_display_name;
                OutputLog::success("level name: in-game name set to '" +
                                   s_level_display_name + "'");
            } else {
                OutputLog::error("level name: " + error);
            }
        }
        if (name_blocked) ImGui::EndDisabled();
    }

    ImGui::Separator();
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
            if (!s_heightmap_path.empty()) {
                static std::string s_probe_path;
                static int s_probe_w = 0;
                static int s_probe_h = 0;
                if (s_probe_path != s_heightmap_path) {
                    s_probe_path = s_heightmap_path;
                    if (!Level::Creation::ProbeHeightmapSize(
                            s_heightmap_path, s_probe_w, s_probe_h)) {
                        s_probe_w = 0;
                        s_probe_h = 0;
                    }
                }
                if (s_probe_w > 0) {
                    prop_label("Source Size");
                    ImGui::Text("%d x %d", s_probe_w, s_probe_h);
                    prop_label("Imported As");
                    ImGui::Text("%d x %d (donor grid)", s_params.grid_w,
                                s_params.grid_h);
                }
            }
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader(s_manage_tool == 0 ? "New Landscape"
                                                   : "Landscape Size",
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
        ImGui::TextDisabled(
            "Grid and spacing are fixed by the donor EHF used in game.");
    }

    if (busy || !s_layout_ok) ImGui::BeginDisabled();
    if (ImGui::Button("Repair Existing Terrain for Game",
                      ImVec2(-FLT_MIN, 0.0f))) {
        std::string error;
        if (!Level::Creation::RepairLandscapeForGame(entry, error)) {
            OutputLog::error("terrain repair: " + error);
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(
            "Resample an older mismatched GHF to the donor EHF grid, "
            "repair HDB/GENV, and synchronize the streaming banks.");
    }
    if (busy || !s_layout_ok) ImGui::EndDisabled();

    const bool large_layout =
        s_layout_ok && s_layout_w == 769 && s_layout_h == 769;
    if (busy || !s_layout_ok || large_layout) ImGui::BeginDisabled();
    if (ImGui::Button("Upgrade Terrain to 769 x 769",
                      ImVec2(-FLT_MIN, 0.0f))) {
        std::string error;
        if (Level::Creation::UpgradeLandscapeToLarge(entry, error)) {

            s_layout_region.clear();
        } else {
            OutputLog::error("terrain upgrade: " + error);
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(
            large_layout
                ? "This level already uses the 769 x 769 terrain layout."
                : "Resample the current heights into the retail Crucible "
                  "terrain family. Scenario entities and level edits are "
                  "preserved.");
    }
    if (busy || !s_layout_ok || large_layout) ImGui::EndDisabled();

    if (!s_layout_ok) {
        ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.30f, 1.0f),
                           "Terrain layout error: %s",
                           s_layout_error.c_str());
    }
    ImGui::Spacing();
    const bool blocked =
        busy || !s_layout_ok ||
        (s_manage_tool == 1 && s_heightmap_path.empty());
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
    if (tool_button(ICON_FA_PAINTBRUSH, "Paint", s_paint_tool == 0)) {
        s_paint_tool = 0;
    }
    ImGui::SameLine();
    if (tool_button(ICON_FA_BRAILLE, "Noise", s_paint_tool == 1)) {
        s_paint_tool = 1;
    }
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Tool Settings",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        if (begin_props("##ls_paint_tool")) {
            prop_label("Tool Strength");
            ImGui::SliderFloat("##ls_pstrength", &s_tool_strength, 0.0f,
                               1.0f, "%.2f");
            if (s_paint_tool == 1) {
                prop_label("Noise Scale");
                ImGui::SliderFloat("##ls_pnscale", &s_paint_noise_scale,
                                   1.0f, 64.0f, "%.1f m",
                                   ImGuiSliderFlags_Logarithmic);
                prop_label("Coverage");
                ImGui::SliderFloat("##ls_pncov",
                                   &s_paint_noise_coverage, 0.02f,
                                   1.0f, "%.2f");
            }
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
        rebuild_paint_tex_options();
        const auto& layers = TerrainPaint::Layers();
        const int active = TerrainPaint::ActiveLayer();
        int remove_index = -1;
        static int s_normal_pick_layer = -1;
        auto accept_texture_drop = [&]() {
            if (!ImGui::BeginDragDropTarget()) return;
            if (const ImGuiPayload* pay =
                    ImGui::AcceptDragDropPayload("F2_TEXTURE")) {
                const std::string path((const char*)pay->Data,
                                       (size_t)pay->DataSize);
                const PaintTexOption* opt = find_paint_tex_option(path);
                if (TerrainPaint::AddLayer(
                        path,
                        opt ? opt->normal_path : std::string()) < 0) {
                    OutputLog::warn(
                        "paint: layer not added (duplicate or at the "
                        "16-layer limit)");
                }
            }
            ImGui::EndDragDropTarget();
        };
        for (size_t i = 0; i < layers.size(); ++i) {
            ImGui::PushID((int)i);
            const bool selected = (int)i == active;
            const std::string leaf =
                std::filesystem::path(layers[i].tex_path)
                    .stem()
                    .string();
            const PaintTexOption* option =
                find_paint_tex_option(layers[i].tex_path);
            const ImVec4 background = selected
                                          ? ImVec4(kAccent.x, kAccent.y,
                                                   kAccent.z, 0.58f)
                                          : ImGui::GetStyleColorVec4(
                                                ImGuiCol_FrameBg);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
            ImGui::BeginChild("##layer_card", ImVec2(0.0f, 78.0f), true,
                              ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse);
            if (ImGui::BeginTable("##layer_layout", 3,
                                  ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("##preview",
                                        ImGuiTableColumnFlags_WidthFixed,
                                        58.0f);
                ImGui::TableSetupColumn("##details",
                                        ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("##actions",
                                        ImGuiTableColumnFlags_WidthFixed,
                                        24.0f);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
#ifdef _WIN32
                ID3D11ShaderResourceView* thumbnail =
                    option ? paint_tex_thumbnail(*option) : nullptr;
                if (thumbnail) {
                    ImGui::Image((ImTextureID)thumbnail,
                                 ImVec2(56.0f, 56.0f));
                } else {
                    ImGui::Dummy(ImVec2(56.0f, 56.0f));
                }
#else
                ImGui::Dummy(ImVec2(56.0f, 56.0f));
#endif
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", layers[i].tex_path.c_str());
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(leaf.c_str());
                ImGui::TextDisabled("Layer %02d  |  Diffuse", (int)i + 1);
                ImGui::TextDisabled(
                    layers[i].normal_path.empty() ? "Normal not found"
                                                  : "Normal paired");
                ImGui::TableSetColumnIndex(2);
                if (ImGui::SmallButton("X")) remove_index = (int)i;
                ImGui::EndTable();
            }
            const bool card_clicked =
                ImGui::IsWindowHovered(
                    ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            if (card_clicked) TerrainPaint::SetActiveLayer((int)i);
            ImGui::EndChild();
            accept_texture_drop();
            if (ImGui::BeginPopupContextItem("##layer_ctx")) {
                if (ImGui::MenuItem("Set normal map...")) {
                    s_normal_pick_layer = (int)i;
                }
                if (!layers[i].normal_path.empty() &&
                    ImGui::MenuItem("Clear normal map")) {
                    TerrainPaint::SetLayerNormal((int)i, std::string());
                }
                ImGui::EndPopup();
            }
            ImGui::PopStyleColor();
            ImGui::Spacing();
            ImGui::PopID();
        }
        if (remove_index >= 0) TerrainPaint::RemoveLayer(remove_index);

        if (s_normal_pick_layer >= 0 &&
            s_normal_pick_layer >= (int)layers.size()) {
            s_normal_pick_layer = -1;
        }
        if (s_normal_pick_layer >= 0) {
            if (!ImGui::IsPopupOpen("##paint_pick_normal")) {
                ImGui::OpenPopup("##paint_pick_normal");
            }
            if (ImGui::BeginPopup("##paint_pick_normal")) {
                static std::string s_normal_filter;
                ImGui::SetNextItemWidth(300.0f);
                ImGui::InputTextWithHint("##paint_normal_filter", "Search",
                                         &s_normal_filter);
                const std::string nfilter = lower_slash(s_normal_filter);

                static std::vector<int> s_normal_catalog;
                static size_t s_normal_catalog_count = (size_t)-1;
                if (s_normal_catalog_count != S.all_tex_files.size()) {
                    s_normal_catalog.clear();
                    std::unordered_set<std::string> seen;
                    for (int ti = 0; ti < (int)S.all_tex_files.size();
                         ++ti) {
                        const std::string low =
                            lower_slash(S.all_tex_files[(size_t)ti]
                                            .full_path);
                        std::string leaf = low;
                        const size_t sl = leaf.find_last_of('/');
                        if (sl != std::string::npos) {
                            leaf = leaf.substr(sl + 1);
                        }
                        if (leaf.find("norm") == std::string::npos &&
                            leaf.find("nrm") == std::string::npos &&
                            leaf.find("bump") == std::string::npos) {
                            continue;
                        }
                        if (!seen.insert(low).second) continue;
                        s_normal_catalog.push_back(ti);
                    }
                    s_normal_catalog_count = S.all_tex_files.size();
                }

                std::vector<int> nvis;
                nvis.reserve(s_normal_catalog.size());
                for (int ti : s_normal_catalog) {
                    if (!nfilter.empty()) {
                        const std::string low = lower_slash(
                            S.all_tex_files[(size_t)ti].full_path);
                        if (low.find(nfilter) == std::string::npos) {
                            continue;
                        }
                    }
                    nvis.push_back(ti);
                }

                ImGui::BeginChild("##paint_normal_list",
                                  ImVec2(300.0f, 340.0f), true);
                ImGuiListClipper nclip;
                nclip.Begin((int)nvis.size());
                while (nclip.Step()) {
                    for (int row = nclip.DisplayStart;
                         row < nclip.DisplayEnd; ++row) {
                        const FlatAssetEntry& te =
                            S.all_tex_files[(size_t)nvis[(size_t)row]];
                        std::string leaf = lower_slash(te.full_path);
                        const size_t sl = leaf.find_last_of('/');
                        if (sl != std::string::npos) {
                            leaf = leaf.substr(sl + 1);
                        }
                        ImGui::PushID(nvis[(size_t)row]);
                        if (ImGui::Selectable(leaf.c_str())) {
                            TerrainPaint::SetLayerNormal(
                                s_normal_pick_layer, te.full_path);
                            s_normal_pick_layer = -1;
                            ImGui::CloseCurrentPopup();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s", te.full_path.c_str());
                        }
                        ImGui::PopID();
                    }
                }
                nclip.End();
                ImGui::EndChild();
                ImGui::EndPopup();
            } else {
                s_normal_pick_layer = -1;
            }
        }

        const bool full =
            (int)layers.size() >= TerrainPaint::kMaxLayers;
        if (full) ImGui::BeginDisabled();
        if (ImGui::Button("+ Add Layer", ImVec2(-FLT_MIN, 0.0f))) {
            ImGui::OpenPopup("##paint_add_layer");
        }
        accept_texture_drop();
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
                        if (TerrainPaint::AddLayer(
                                opt.full_path, opt.normal_path) < 0) {
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

    if (ImGui::CollapsingHeader("Auto Material")) {
        const auto& layers = TerrainPaint::Layers();
        for (size_t i = 0; i < layers.size(); ++i) {
            ImGui::PushID((int)(4000 + i));
            TerrainPaint::AutoRule rule = layers[i].rule;
            bool changed = false;
            const std::string leaf =
                std::filesystem::path(layers[i].tex_path).stem().string();
            changed |= ImGui::Checkbox("##rule_on", &rule.enabled);
            ImGui::SameLine();
            ImGui::TextUnformatted(leaf.c_str());
            if (rule.enabled) {
                ImGui::Indent(24.0f);
                ImGui::SetNextItemWidth(-90.0f);
                changed |= ImGui::DragFloatRange2(
                    "##rule_h", &rule.h_min, &rule.h_max, 0.25f,
                    -1000.0f, 1000.0f, "h %.1f", "%.1f m");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= ImGui::DragFloat("##rule_hf", &rule.h_fade,
                                            0.1f, 0.0f, 100.0f,
                                            "+-%.1f");
                ImGui::SetNextItemWidth(-90.0f);
                changed |= ImGui::DragFloatRange2(
                    "##rule_s", &rule.slope_min, &rule.slope_max, 0.25f,
                    0.0f, 90.0f, "slope %.0f", "%.0f deg");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= ImGui::DragFloat("##rule_sf",
                                            &rule.slope_fade, 0.1f,
                                            0.0f, 45.0f, "+-%.1f");
                ImGui::SetNextItemWidth(-90.0f);
                changed |= ImGui::SliderFloat("##rule_na",
                                              &rule.noise_amount, 0.0f,
                                              1.0f, "noise %.2f");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= ImGui::DragFloat("##rule_ns",
                                            &rule.noise_scale, 0.25f,
                                            1.0f, 128.0f, "%.0f m");
                ImGui::Unindent(24.0f);
            }
            if (changed) TerrainPaint::SetLayerRule((int)i, rule);
            ImGui::PopID();
        }
        if (!layers.empty()) {
            if (ImGui::Button("Generate From Rules",
                              ImVec2(-FLT_MIN, 0.0f))) {
                std::string error;
                if (TerrainPaint::ApplyAutoMaterial(error)) {
                    OutputLog::success(
                        "paint: auto material generated");
                } else {
                    OutputLog::warn("paint: " + error);
                }
            }
        } else {
            ImGui::TextDisabled("Add layers first.");
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
        for (size_t i = 0; i < s_sky_options.size(); ++i) {
            const auto& option = s_sky_options[i];
            ImGui::PushID((int)i);
            const bool selected = option.day_set_hash == s_sky_selected;
            std::string label = option.name;
            if (option.day_set_hash == s_sky_current) label += "  (active)";
            if (ImGui::Selectable(label.c_str(), selected)) {
                s_sky_selected = option.day_set_hash;
            }
            ImGui::PopID();
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
        const bool can_edit = LevelEdit::Enabled() && !LevelEdit::Saving();
        for (size_t mi = 0; mi < g_level_spawn_markers.size(); ++mi) {
            const LevelSpawnMarker& m = g_level_spawn_markers[mi];
            if (!is_player_start_marker(m)) continue;
            found = true;
            const bool removed =
                LevelEdit::IsDeleted(0x70000000u | (uint32_t)mi);
            char label[192];
            if (removed) {
                std::snprintf(label, sizeof(label),
                              "%s  [deleted]##ps_%zu",
                              m.name.empty() ? "Player Start"
                                             : m.name.c_str(),
                              mi);
            } else {
                std::snprintf(label, sizeof(label),
                              "%s  (%.1f, %.1f, %.1f)##ps_%zu",
                              m.name.empty() ? "Player Start"
                                             : m.name.c_str(),
                              m.x, m.y, m.z, mi);
            }
            if (ImGui::Selectable(label) && !removed) {
                UI::select_level_marker(mi);
            }
            if (can_edit && ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("F2_START_PIN", &mi,
                                          sizeof(mi));
                ImGui::TextUnformatted(m.name.empty()
                                           ? "Player Start"
                                           : m.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(removed
                                      ? "Drag into the level to restore"
                                      : "Drag into the level");
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

static void draw_foliage() {
    const size_t total = FoliageEdit::TotalInstances();
    ImGui::Text("Painted: %zu instance(s)", total);
    if (!FoliageEdit::Dirty() && total > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("(saved)");
    } else if (FoliageEdit::Dirty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.25f, 1.0f),
                           "(unsaved - use Save)");
    }

    ImGui::Spacing();
    {
        static const char* kTools[3] = {"Paint", "Place", "Erase"};
        for (int i = 0; i < 3; ++i) {
            if (i > 0) ImGui::SameLine();
            if (mode_button(kTools[i], s_foliage_tool == i)) {
                s_foliage_tool = i;
            }
        }
    }
    if (s_foliage_tool != 1) {
        ImGui::SliderFloat("Brush radius", &s_foliage_radius, 1.0f, 30.0f,
                           "%.1f m");
    }

    ImGui::Spacing();
    ImGui::Separator();
    int remove_slot = -1;
    for (int i = 0; i < (int)s_foliage_palette.size(); ++i) {
        FoliagePaletteSlot& slot = s_foliage_palette[(size_t)i];
        FoliagePaintEntry& fe = slot.entry;
        std::string leaf = fe.model_path;
        const size_t sl = leaf.find_last_of("/\\");
        if (sl != std::string::npos) leaf = leaf.substr(sl + 1);
        ImGui::PushID(i + 90000);
        ImGui::Checkbox("##on", &slot.enabled);
        ImGui::SameLine();
        const bool active = s_foliage_active == i;
        if (ImGui::Selectable(leaf.c_str(), active,
                              ImGuiSelectableFlags_AllowItemOverlap)) {
            s_foliage_active = i;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", fe.model_path.c_str());
        }
        ImGui::SameLine(
            std::max(140.0f, ImGui::GetContentRegionAvail().x +
                                 ImGui::GetCursorPosX() - 24.0f));
        if (ImGui::SmallButton("x")) remove_slot = i;
        ImGui::Indent(24.0f);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat("##density", &fe.density, 0.005f, 8.0f,
                           "density %.3f / m2",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::DragFloatRange2("##scale", &fe.scale_min, &fe.scale_max,
                               0.01f, 0.2f, 3.0f, "scale %.2f", "%.2f");
        ImGui::Unindent(24.0f);
        ImGui::PopID();
    }
    if (remove_slot >= 0) {
        s_foliage_palette.erase(s_foliage_palette.begin() + remove_slot);
        if (s_foliage_active == remove_slot) s_foliage_active = -1;
        else if (s_foliage_active > remove_slot) --s_foliage_active;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##foliage_model_filter", "Filter",
                             &s_foliage_filter);
    std::string filter = lower_slash(s_foliage_filter);

    static std::vector<int> s_catalog;
    static size_t s_catalog_source_count = (size_t)-1;
    if (s_catalog_source_count != S.all_mdl_files.size()) {
        s_catalog.clear();
        std::unordered_set<std::string> seen;
        auto is_lod_variant = [](const std::string& leaf) {
            const size_t pos = leaf.rfind("lod");
            if (pos == std::string::npos || pos == 0) return false;
            const char prev = leaf[pos - 1];
            if (prev != '_' && prev != '-') return false;
            size_t q = pos + 3;
            while (q < leaf.size() && leaf[q] >= '0' && leaf[q] <= '9') {
                ++q;
            }
            return q >= leaf.size() || leaf[q] == '.';
        };
        for (int i = 0; i < (int)S.all_mdl_files.size(); ++i) {
            const std::string low =
                lower_slash(S.all_mdl_files[i].full_path);
            const bool foliage_dir =
                low.find("foliage") != std::string::npos;
            std::string leaf = low;
            const size_t sl = leaf.find_last_of('/');
            if (sl != std::string::npos) leaf = leaf.substr(sl + 1);
            if (!foliage_dir && leaf.find("grass") == std::string::npos) {
                continue;
            }
            if (is_lod_variant(leaf)) continue;
            if (!seen.insert(low).second) continue;
            s_catalog.push_back(i);
        }
        std::sort(s_catalog.begin(), s_catalog.end(), [](int a, int b) {
            auto leaf_of = [](const FlatAssetEntry& e) {
                std::string low = lower_slash(e.full_path);
                const size_t sl = low.find_last_of('/');
                return sl == std::string::npos ? low : low.substr(sl + 1);
            };
            return leaf_of(S.all_mdl_files[a]) <
                   leaf_of(S.all_mdl_files[b]);
        });
        s_catalog_source_count = S.all_mdl_files.size();
    }

    std::vector<int> vis;
    vis.reserve(s_catalog.size());
    for (int idx : s_catalog) {
        if (!filter.empty()) {
            const std::string low =
                lower_slash(S.all_mdl_files[idx].full_path);
            if (low.find(filter) == std::string::npos) continue;
        }
        vis.push_back(idx);
    }

    ImGui::BeginChild("##foliage_model_list", ImVec2(0, 0), true);
    ImGuiListClipper clipper;
    clipper.Begin((int)vis.size());
    while (clipper.Step()) {
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
            const int idx = vis[r];
            const FlatAssetEntry& mf = S.all_mdl_files[(size_t)idx];
            std::string leaf = lower_slash(mf.full_path);
            const size_t sl = leaf.find_last_of('/');
            if (sl != std::string::npos) leaf = leaf.substr(sl + 1);
            ImGui::PushID(idx);
            const int slot = foliage_palette_find(mf.full_path);
            const bool sel = slot >= 0;
            std::string label = leaf;
            const size_t used = FoliageEdit::InstanceCount(mf.full_path);
            if (used > 0) label += "  (" + std::to_string(used) + ")";
            if (ImGui::Selectable(label.c_str(), sel)) {
                if (slot >= 0) {
                    s_foliage_palette.erase(
                        s_foliage_palette.begin() + slot);
                    if (s_foliage_active == slot) {
                        s_foliage_active = -1;
                    } else if (s_foliage_active > slot) {
                        --s_foliage_active;
                    }
                } else {
                    FoliagePaletteSlot ns;
                    ns.entry.model_path = mf.full_path;
                    s_foliage_palette.push_back(ns);
                    s_foliage_active =
                        (int)s_foliage_palette.size() - 1;
#ifdef _WIN32
                    foliage_preload_model(mf.full_path);
#endif
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", mf.full_path.c_str());
            }
            ImGui::PopID();
        }
    }
    clipper.End();
    ImGui::EndChild();
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pay =
                ImGui::AcceptDragDropPayload("F2_MODEL")) {
            const std::string path((const char*)pay->Data,
                                   (size_t)pay->DataSize);
            if (!path.empty() && foliage_palette_find(path) < 0) {
                FoliagePaletteSlot ns;
                ns.entry.model_path = path;
                s_foliage_palette.push_back(ns);
                s_foliage_active = (int)s_foliage_palette.size() - 1;
#ifdef _WIN32
                foliage_preload_model(path);
#endif
            }
        }
        ImGui::EndDragDropTarget();
    }
}

bool AppliesTo(const FlatAssetEntry& entry) {
    return Level::Creation::IsCustomLooseLevel(entry);
}

bool  InSculptMode()  { return s_mode == 1; }
bool  InPaintMode()   { return s_mode == 2; }
bool  InFoliageMode() { return s_mode == 6; }
int   SculptTool()    { return s_sculpt_tool; }
int   PaintTool()     { return s_paint_tool; }
float PaintNoiseScale() { return s_paint_noise_scale; }
float PaintNoiseCoverage() { return s_paint_noise_coverage; }
float BrushSize()     { return s_brush_size; }
float ToolStrength()  { return s_tool_strength; }
float BrushFalloff()  { return s_brush_falloff; }

int   FoliageTool()        { return s_foliage_tool; }
float FoliageBrushRadius() { return s_foliage_radius; }
bool  FoliageEraseMode()   { return s_foliage_tool == 2; }

void FoliageEnabledPaintSet(std::vector<FoliagePaintEntry>& out) {
    out.clear();
    for (const FoliagePaletteSlot& slot : s_foliage_palette) {
        if (slot.enabled) out.push_back(slot.entry);
    }
}

const FoliagePaintEntry* FoliageActiveEntry() {
    if (s_foliage_active >= 0 &&
        s_foliage_active < (int)s_foliage_palette.size()) {
        return &s_foliage_palette[(size_t)s_foliage_active].entry;
    }
    for (const FoliagePaletteSlot& slot : s_foliage_palette) {
        if (slot.enabled) return &slot.entry;
    }
    return nullptr;
}

void DrawSidePanel(const FlatAssetEntry& entry, void* d3d_device) {
    s_thumb_device = d3d_device;
    ImGui::BeginChild("##landscape_side", ImVec2(360.0f, 0.0f), true);

    
    {
        static const char* kModes[7] = {"Manage", "Sculpt", "Paint",
                                        "Sky",    "Water",  "Entities",
                                        "Foliage"};
        static const int kRowStart[3] = {0, 3, 6};
        static const int kRowCount[3] = {3, 3, 1};
        const ImGuiStyle& style = ImGui::GetStyle();
        const float avail = ImGui::GetContentRegionAvail().x;
        for (int row = 0; row < 3; ++row) {
            float total = 0.0f;
            for (int i = kRowStart[row];
                 i < kRowStart[row] + kRowCount[row]; ++i) {
                total += ImGui::CalcTextSize(kModes[i]).x +
                         style.FramePadding.x * 2.0f;
                if (i > kRowStart[row]) total += style.ItemSpacing.x;
            }
            if (total < avail) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                     (avail - total) * 0.5f);
            }
            for (int i = kRowStart[row];
                 i < kRowStart[row] + kRowCount[row]; ++i) {
                if (i > kRowStart[row]) ImGui::SameLine();
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
        case 6: draw_foliage(); break;
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
