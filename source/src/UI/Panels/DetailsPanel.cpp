#include "DetailsPanel.h"

#include "../ContentTabs.h"
#include "../ModelPreview.h"
#include "../OutputLog.h"
#include "../../Level/Creation/LandscapeAuthoring.h"
#include "../../Level/Creation/WaterAuthoring.h"
#include "../../Level/Core/LevelLoader.h"
#include "../../Level/Editing/LevelEdit.h"
#include "../../Utilities/State.h"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <string>
#include <vector>



extern ModelPreview g_mp;
extern int g_selected_level_mesh_idx;
extern uint32_t g_selected_level_pick_id;
extern uint64_t g_selected_level_hash;
bool delete_selected_level_object();

namespace DetailsPanel {

namespace {

constexpr uint64_t kAdditionHashBase = 0xADD0000000000000ull;

enum class SelKind {
    None,
    Landscape,
    Sky,
    Water,
    Addition,
    Generator,
    SpawnPoint
};

SelKind s_kind = SelKind::None;
int     s_index = -1;   
Level::Creation::WaterPlaneSettings s_water_settings;
Level::Creation::WaterPlaneSettings s_water_original_settings;
std::string s_water_level_key;
std::string s_water_error;
bool s_water_loaded = false;
bool s_water_dirty = false;

bool begin_props(const char* id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit)) {
        return false;
    }
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed,
                            80.0f);
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

std::string addition_display_name(const LevelEdit::Addition& a) {
    if (!a.entity_name.empty()) return a.entity_name;
    if (!a.creature_name.empty()) return a.creature_name;
    if (!a.model_path.empty()) {
        return std::filesystem::path(a.model_path).stem().string();
    }
    return "(unnamed)";
}

const char* addition_type_name(const LevelEdit::Addition& a) {
    switch (a.entity_kind) {
        case LevelEdit::AdditionEntityKind::Chest:
            return a.is_dig_spot ? "Dig Spot" : "Chest";
        case LevelEdit::AdditionEntityKind::SilverKey: return "Silver Key";
        case LevelEdit::AdditionEntityKind::Npc:       return "NPC";
        case LevelEdit::AdditionEntityKind::GenericProp: return "Prop";
        default: return "Model";
    }
}

void clear_model_selection() {
    g_selected_level_mesh_idx = -1;
    g_selected_level_pick_id = 0;
    g_selected_level_hash = 0;
}

void focus_camera(const float center[3], float radius) {
    FlyCam_Reset(g_flycam, center[0], center[1], center[2],
                 std::max(radius, 0.5f));
}

void focus_camera_on_engine_position(const float pos[3], float radius) {
    const float center[3] = {pos[0], pos[2], pos[1]};
    focus_camera(center, radius);
}

bool select_addition_model(size_t addition_index, float out_center[3],
                           float& out_radius) {
    const uint64_t selection_hash =
        kAdditionHashBase + static_cast<uint64_t>(addition_index);
    const uint32_t position_offset =
        static_cast<uint32_t>(addition_index) + 1u;

    g_selected_level_mesh_idx = -1;
    g_selected_level_pick_id = 0;
    g_selected_level_hash = selection_hash;

    float bounds_min[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    float bounds_max[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    int hash_mesh = -1;
    uint32_t hash_pick = 0;
    int fallback_mesh = -1;
    uint32_t fallback_pick = 0;
    bool found = false;

    for (size_t mesh_index = 0; mesh_index < g_mp.meshes.size();
         ++mesh_index) {
        const MPPerMesh& mesh = g_mp.meshes[mesh_index];
        for (const MPPerMesh::PickRange& range : mesh.pick_ranges) {
            const bool hash_match = range.inst_hash == selection_hash;
            const bool addition_match =
                range.lev_rec_kind == 5 &&
                range.pos_file_offset == position_offset;
            if (!hash_match && !addition_match) continue;

            found = true;
            if (hash_match && hash_mesh < 0) {
                hash_mesh = static_cast<int>(mesh_index);
                hash_pick = range.selection_id;
            } else if (fallback_mesh < 0) {
                fallback_mesh = static_cast<int>(mesh_index);
                fallback_pick = range.selection_id;
            }
            for (int axis = 0; axis < 3; ++axis) {
                bounds_min[axis] = std::min(
                    bounds_min[axis], range.center[axis] - range.radius);
                bounds_max[axis] = std::max(
                    bounds_max[axis], range.center[axis] + range.radius);
            }
        }
    }

    if (!found) return false;

    g_selected_level_mesh_idx = hash_mesh >= 0 ? hash_mesh : fallback_mesh;
    g_selected_level_pick_id = hash_mesh >= 0 ? hash_pick : fallback_pick;
    out_radius = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        out_center[axis] = (bounds_min[axis] + bounds_max[axis]) * 0.5f;
        out_radius = std::max(
            out_radius, (bounds_max[axis] - bounds_min[axis]) * 0.5f);
    }
    return true;
}

bool water_bounds(float out_center[3], float& out_radius) {
    float bounds_min[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    float bounds_max[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    bool found = false;
    for (const MPPerMesh& mesh : g_mp.meshes) {
        if (!mesh.is_water) continue;
        for (int axis = 0; axis < 3; ++axis) {
            bounds_min[axis] = std::min(bounds_min[axis],
                                        mesh.center[axis] - mesh.radius);
            bounds_max[axis] = std::max(bounds_max[axis],
                                        mesh.center[axis] + mesh.radius);
        }
        found = true;
    }
    if (!found) return false;
    out_radius = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        out_center[axis] = (bounds_min[axis] + bounds_max[axis]) * 0.5f;
        out_radius = std::max(
            out_radius, (bounds_max[axis] - bounds_min[axis]) * 0.5f);
    }
    return true;
}

bool has_water() {
    for (const MPPerMesh& mesh : g_mp.meshes) {
        if (mesh.is_water) return true;
    }
    return false;
}

void load_water_settings(const FlatAssetEntry& entry) {
    s_water_error.clear();
    s_water_loaded = Level::Creation::GetWaterPlaneSettings(
        entry, s_water_settings, s_water_error);
    if (s_water_loaded) s_water_original_settings = s_water_settings;
    s_water_dirty = false;
}

struct OutlinerRowAction {
    bool clicked = false;
    bool double_clicked = false;
};

OutlinerRowAction outliner_row(const char* name, const char* type,
                               bool selected, int push_id) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::PushID(push_id);
    const bool clicked = ImGui::Selectable(
        name, selected,
        ImGuiSelectableFlags_SpanAllColumns |
            ImGuiSelectableFlags_AllowOverlap |
            ImGuiSelectableFlags_AllowDoubleClick);
    const bool double_clicked =
        clicked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    ImGui::PopID();
    ImGui::TableSetColumnIndex(1);
    ImGui::TextDisabled("%s", type);
    return {clicked, double_clicked};
}

void draw_outliner(const std::vector<LevelEdit::Addition>& adds,
                   const std::vector<LevelEdit::GeneratorAddition>& gens,
                   const std::vector<LevelEdit::PendingSpawnPoint>& sps,
                   const FlatAssetEntry& level) {
    if (!ImGui::BeginTable("##outliner", 2,
                           ImGuiTableFlags_RowBg |
                               ImGuiTableFlags_BordersInnerV |
                               ImGuiTableFlags_ScrollY)) {
        return;
    }
    ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed,
                            105.0f);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();

    const OutlinerRowAction landscape_action = outliner_row(
        "Landscape", "Landscape", s_kind == SelKind::Landscape, 1);
    if (landscape_action.clicked) {
        s_kind = SelKind::Landscape;
        s_index = -1;
        clear_model_selection();
        if (landscape_action.double_clicked) {
            focus_camera(g_mp.center, g_mp.radius);
        }
    }

    if (g_mp.has_sky_theme) {
        const OutlinerRowAction sky_action = outliner_row(
            "Sky", "Sky", s_kind == SelKind::Sky, 2);
        if (sky_action.clicked) {
            s_kind = SelKind::Sky;
            s_index = -1;
            clear_model_selection();
        }
    }

    if (has_water()) {
        const OutlinerRowAction water_action = outliner_row(
            "Water", "Water", s_kind == SelKind::Water, 3);
        if (water_action.clicked) {
            s_kind = SelKind::Water;
            s_index = -1;
            clear_model_selection();
            load_water_settings(level);
            if (water_action.double_clicked) {
                float center[3] = {};
                float radius = 0.0f;
                if (water_bounds(center, radius)) {
                    focus_camera(center, radius);
                }
            }
        }
    }

    for (size_t i = 0; i < adds.size(); ++i) {
        const LevelEdit::Addition& a = adds[i];
        if (a.removed) continue;
        const bool selected =
            s_kind == SelKind::Addition && s_index == (int)i;
        const OutlinerRowAction action = outliner_row(
            addition_display_name(a).c_str(), addition_type_name(a),
            selected, 0x1000 + (int)i);
        if (action.clicked) {
            s_kind = SelKind::Addition;
            s_index = (int)i;
            float center[3] = {};
            float radius = 0.0f;
            if (select_addition_model(i, center, radius)) {
                if (action.double_clicked) focus_camera(center, radius);
            } else if (action.double_clicked) {
                focus_camera_on_engine_position(a.pos, 1.0f);
            }
        }
    }

    for (size_t i = 0; i < gens.size(); ++i) {
        const LevelEdit::GeneratorAddition& g = gens[i];
        if (g.removed) continue;
        const bool selected =
            s_kind == SelKind::Generator && s_index == (int)i;
        const std::string name =
            g.creature_name.empty() ? "(generator)" : g.creature_name;
        const OutlinerRowAction action = outliner_row(
            name.c_str(), "Spawn Generator", selected, 0x2000 + (int)i);
        if (action.clicked) {
            s_kind = SelKind::Generator;
            s_index = (int)i;
            clear_model_selection();
            if (action.double_clicked) {
                focus_camera_on_engine_position(g.pos, 1.0f);
            }
        }
    }

    for (const LevelEdit::PendingSpawnPoint& sp : sps) {
        const bool selected =
            s_kind == SelKind::SpawnPoint && s_index == sp.id;
        const std::string name =
            sp.label.empty() ? "Spawn Point" : sp.label;
        const OutlinerRowAction action = outliner_row(
            name.c_str(), "Spawn Point", selected, 0x3000 + sp.id);
        if (action.clicked) {
            s_kind = SelKind::SpawnPoint;
            s_index = sp.id;
            clear_model_selection();
            if (action.double_clicked) {
                focus_camera_on_engine_position(sp.pos, 0.5f);
            }
        }
    }

    ImGui::EndTable();
}

void draw_addition_details(const std::vector<LevelEdit::Addition>& adds) {
    if (s_index < 0 || s_index >= (int)adds.size() ||
        adds[(size_t)s_index].removed) {
        s_kind = SelKind::None;
        return;
    }
    const LevelEdit::Addition& a = adds[(size_t)s_index];

    if (begin_props("##det_add")) {
        prop_label("Name");
        ImGui::TextUnformatted(addition_display_name(a).c_str());
        prop_label("Type");
        ImGui::TextUnformatted(addition_type_name(a));
        if (!a.model_path.empty()) {
            prop_label("Model");
            ImGui::TextUnformatted(
                std::filesystem::path(a.model_path).filename().string()
                    .c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", a.model_path.c_str());
            }
        }

        float pos[3] = {a.pos[0], a.pos[1], a.pos[2]};
        static const char* kAxis[3] = {"X", "Y", "Z"};
        bool moved = false;
        for (int ax = 0; ax < 3; ++ax) {
            prop_label(kAxis[ax]);
            ImGui::PushID(ax);
            if (ImGui::DragFloat("##det_pos", &pos[ax], 0.05f, -100000.0f,
                                 100000.0f, "%.3f")) {
                moved = true;
            }
            ImGui::PopID();
        }
        if (moved) LevelEdit::MoveAddition(s_index, pos);

        float yaw = a.yaw_deg;
        prop_label("Yaw");
        if (ImGui::DragFloat("##det_yaw", &yaw, 0.5f, -360.0f, 360.0f,
                             "%.1f deg")) {
            LevelEdit::SetAdditionYaw(s_index, yaw);
        }

        if (a.entity_kind == LevelEdit::AdditionEntityKind::Chest) {
            prop_label("Items");
            ImGui::Text("%zu placed", a.chest_items.size());
            if (a.silver_keys_needed > 0) {
                prop_label("Silver Keys");
                ImGui::Text("%d", a.silver_keys_needed);
            }
        }
        ImGui::EndTable();
    }

    {
        ImGui::Spacing();
        if (!ImGui::GetIO().WantTextInput &&
            ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            if (::delete_selected_level_object()) {
                s_kind = SelKind::None;
                s_index = -1;
                return;
            }
        }
    }

    if (a.entity_has_text) {
        ImGui::TextDisabled("Readable text");
        std::string text = a.readable_text;
        if (ImGui::InputTextMultiline(
                "##det_readable", text.data(), text.size() + 1,
                ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.0f),
                ImGuiInputTextFlags_ReadOnly)) {
        }
    }

}

void draw_generator_details(
    const std::vector<LevelEdit::GeneratorAddition>& gens) {
    if (s_index < 0 || s_index >= (int)gens.size() ||
        gens[(size_t)s_index].removed) {
        s_kind = SelKind::None;
        return;
    }
    const LevelEdit::GeneratorAddition& g = gens[(size_t)s_index];

    if (begin_props("##det_gen")) {
        prop_label("Creature");
        ImGui::TextUnformatted(g.creature_name.c_str());
        float pos[3] = {g.pos[0], g.pos[1], g.pos[2]};
        static const char* kAxis[3] = {"X", "Y", "Z"};
        bool moved = false;
        for (int ax = 0; ax < 3; ++ax) {
            prop_label(kAxis[ax]);
            ImGui::PushID(ax);
            if (ImGui::DragFloat("##det_gpos", &pos[ax], 0.05f,
                                 -100000.0f, 100000.0f, "%.3f")) {
                moved = true;
            }
            ImGui::PopID();
        }
        if (moved) LevelEdit::MovePendingGenerator(s_index, pos);
        prop_label("Spawn Points");
        ImGui::Text("%zu", g.spawn_points.size());
        ImGui::EndTable();
    }

}

void draw_spawn_point_details(
    const std::vector<LevelEdit::PendingSpawnPoint>& sps) {
    const LevelEdit::PendingSpawnPoint* sp = nullptr;
    for (const auto& candidate : sps) {
        if (candidate.id == s_index) {
            sp = &candidate;
            break;
        }
    }
    if (!sp) {
        s_kind = SelKind::None;
        return;
    }

    if (begin_props("##det_sp")) {
        prop_label("Name");
        ImGui::TextUnformatted(sp->label.empty() ? "Spawn Point"
                                                 : sp->label.c_str());
        float pos[3] = {sp->pos[0], sp->pos[1], sp->pos[2]};
        static const char* kAxis[3] = {"X", "Y", "Z"};
        bool moved = false;
        for (int ax = 0; ax < 3; ++ax) {
            prop_label(kAxis[ax]);
            ImGui::PushID(ax);
            if (ImGui::DragFloat("##det_sppos", &pos[ax], 0.05f,
                                 -100000.0f, 100000.0f, "%.3f")) {
                moved = true;
            }
            ImGui::PopID();
        }
        if (moved) LevelEdit::MovePendingSpawnPoint(sp->id, pos);
        ImGui::EndTable();
    }

}

void draw_sky_details() {
    if (!g_mp.has_sky_theme) {
        s_kind = SelKind::None;
        return;
    }

    if (!begin_props("##det_sky")) return;
    prop_label("Item");
    ImGui::TextUnformatted("Sky");
    prop_label("Type");
    ImGui::TextUnformatted("Environment");

    const bool has_cycle =
        g_mp.has_day_night_cycle && g_mp.day_night_keyframes.size() >= 2;
    bool auto_time = has_cycle && !g_mp.time_of_day_override;
    prop_label("Auto time");
    ImGui::BeginDisabled(!has_cycle);
    if (ImGui::Checkbox("##det_sky_auto_time", &auto_time)) {
        if (auto_time) {
            g_mp.time_of_day_override = false;
        } else {
            g_mp.time_of_day_override = true;
            g_mp.time_of_day_override_value = g_mp.current_time_of_day;
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(
            has_cycle
                ? "Disable this to freeze the sky at the current time."
                : "This sky has no automatic day/night cycle.");
    }

    float hour = (g_mp.time_of_day_override
                      ? g_mp.time_of_day_override_value
                      : g_mp.current_time_of_day) *
                 24.0f;
    hour = std::clamp(hour, 0.0f, 24.0f);
    prop_label("Time");
    if (ImGui::SliderFloat("##det_sky_time", &hour, 0.0f, 24.0f,
                           "%.2f h", ImGuiSliderFlags_AlwaysClamp)) {
        g_mp.time_of_day_override = true;
        g_mp.time_of_day_override_value =
            std::clamp(hour / 24.0f, 0.0f, 1.0f);
    }
    ImGui::EndTable();
}

void draw_water_details(const FlatAssetEntry& level) {
    if (!has_water()) {
        s_kind = SelKind::None;
        return;
    }
    if (!s_water_loaded) load_water_settings(level);
    if (!s_water_loaded) {
        ImGui::TextDisabled("%s", s_water_error.empty()
                                      ? "Water settings are unavailable."
                                      : s_water_error.c_str());
        return;
    }
    if (begin_props("##det_water")) {
        prop_label("Item");
        ImGui::TextUnformatted("Water");
        prop_label("Type");
        ImGui::TextUnformatted("Water Plane");
        prop_label("Width");
        if (ImGui::DragFloat("##det_water_width", &s_water_settings.width,
                             0.25f, 0.1f, 1000000.0f, "%.2f m",
                             ImGuiSliderFlags_AlwaysClamp)) {
            s_water_dirty = true;
        }
        prop_label("Length");
        if (ImGui::DragFloat("##det_water_length", &s_water_settings.length,
                             0.25f, 0.1f, 1000000.0f, "%.2f m",
                             ImGuiSliderFlags_AlwaysClamp)) {
            s_water_dirty = true;
        }
        prop_label("Height");
        if (ImGui::DragFloat("##det_water_height", &s_water_settings.height,
                             0.05f, -100000.0f, 100000.0f, "%.2f m",
                             ImGuiSliderFlags_AlwaysClamp)) {
            s_water_dirty = true;
        }
        ImGui::EndTable();
    }
    const bool blocked = !s_water_dirty || Level::IsAsyncLoadInProgress();
    if (blocked) ImGui::BeginDisabled();
    if (ImGui::Button("Apply Water Changes", ImVec2(-FLT_MIN, 0.0f))) {
        std::string error;
        if (Level::Creation::UpdateWaterPlane(level, s_water_settings,
                                              error)) {
            s_water_original_settings = s_water_settings;
            s_water_dirty = false;
        } else {
            OutputLog::error("water: " + error);
        }
    }
    if (blocked) ImGui::EndDisabled();
}

}

bool Active() {
    const FlatAssetEntry* lv = ContentTabs::ActiveLevelEntry();
    return lv && Level::Creation::IsCustomLooseLevel(*lv);
}

bool WaterSelected() {
    return s_kind == SelKind::Water && s_water_loaded && has_water();
}

bool SelectedWaterPosition(float out_engine_pos[3]) {
    if (!out_engine_pos || !WaterSelected()) return false;
    out_engine_pos[0] = s_water_settings.center_x;
    out_engine_pos[1] = s_water_settings.center_z;
    out_engine_pos[2] = s_water_settings.height;
    return true;
}

void MoveSelectedWater(const float engine_step[3]) {
    if (!engine_step || !WaterSelected()) return;
    s_water_settings.center_x += engine_step[0];
    s_water_settings.center_z += engine_step[1];
    s_water_settings.height += engine_step[2];
    s_water_dirty = true;
}

bool WaterPreviewOffset(float out_render_offset[3]) {
    if (!out_render_offset || !s_water_loaded || !has_water()) return false;
    out_render_offset[0] =
        s_water_settings.center_x - s_water_original_settings.center_x;
    out_render_offset[1] =
        s_water_settings.height - s_water_original_settings.height;
    out_render_offset[2] =
        s_water_settings.center_z - s_water_original_settings.center_z;
    return true;
}

void ClearSelection() {
    s_kind = SelKind::None;
    s_index = -1;
}

void Draw() {
    if (!Active()) {
        ImGui::TextDisabled("Open a custom level to see its details.");
        return;
    }

    const FlatAssetEntry* level = ContentTabs::ActiveLevelEntry();
    if (!level) return;
    const std::string level_key = level->bnk_path + "|" + level->full_path;
    if (s_water_level_key != level_key) {
        s_water_level_key = level_key;
        s_water_loaded = false;
        s_water_dirty = false;
        s_water_error.clear();
        s_kind = SelKind::None;
        s_index = -1;
    }

    if (g_selected_level_hash >= kAdditionHashBase) {
        s_kind = SelKind::Addition;
        s_index = int(g_selected_level_hash - kAdditionHashBase);
    }
    if (s_kind == SelKind::Sky && !g_mp.has_sky_theme) {
        s_kind = SelKind::None;
    }
    if (s_kind == SelKind::Water && !has_water()) {
        s_kind = SelKind::None;
    }

    std::vector<LevelEdit::Addition> adds;
    LevelEdit::GetAdditions(adds);
    std::vector<LevelEdit::GeneratorAddition> gens;
    LevelEdit::GetGenerators(gens);
    std::vector<LevelEdit::PendingSpawnPoint> sps;
    LevelEdit::GetPendingSpawnPoints(sps);

    const float outliner_h =
        ImGui::GetContentRegionAvail().y * 0.45f;
    ImGui::BeginChild("##details_outliner", ImVec2(0, outliner_h), true);
    draw_outliner(adds, gens, sps, *level);
    ImGui::EndChild();

    ImGui::BeginChild("##details_props", ImVec2(0, 0), true);
    switch (s_kind) {
        case SelKind::Landscape:
            if (begin_props("##det_landscape")) {
                prop_label("Item");
                ImGui::TextUnformatted("Landscape");
                prop_label("Show fog");
                ImGui::Checkbox("##det_landscape_fog", &g_mp.show_mist);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Show or hide the environment fog and ground mist "
                        "in the level preview.");
                }
                ImGui::EndTable();
            }
            break;
        case SelKind::Sky:        draw_sky_details(); break;
        case SelKind::Water:      draw_water_details(*level); break;
        case SelKind::Addition:   draw_addition_details(adds); break;
        case SelKind::Generator:  draw_generator_details(gens); break;
        case SelKind::SpawnPoint: draw_spawn_point_details(sps); break;
        default:
            ImGui::TextDisabled("Select an item to edit its properties.");
            break;
    }
    ImGui::EndChild();
}

}
