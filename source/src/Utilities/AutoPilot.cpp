#ifdef _WIN32
#include "AutoPilot.h"

#include "State.h"
#include "../Level/Editing/LevelEdit.h"
#include "../Level/Core/LevelLoader.h"
#include "../UI/ModelPreview.h"
#include "../UI/ContentTabs.h"
#include "../UI/UI_Panels.h"
#include "../UI/Quest/QuestNodeView.h"
#include "../UI/Layout/RenderPanel.h"
#include "../UI/Layout/LoadingScreen.h"
#include "../UI/OutputLog.h"
#include "../textures/export/TextureExport.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

extern bool g_mp_vista_only;
extern ModelPreview g_mp;

namespace {

enum class Stage {
    Idle,
    OpenRoot,
    WaitIndex,
    OpenLevel,
    WaitLevel,
    OpenQuest,
    WaitQuest,
    OpenCustomQuest,
    OpenEntity,
    WaitEntity,
    Settle,
    Capture,
    Done,
};

Stage       g_stage = Stage::Idle;
std::string g_root_path;
std::string g_level_query;
std::string g_quest_query;
std::string g_custom_quest_id;
std::string g_entity_query;
std::string g_shot_path;
bool        g_auto_exit = false;
bool        g_vista_only = false;
bool        g_sky_shot = false;
bool        g_terrain_shot = false;
bool        g_quest_fit_all = false;
bool        g_show_dig_spots = false;
bool        g_select_dig_spot = false;
bool        g_select_entity = false;
bool        g_show_entity_model = false;
bool        g_show_create_npc = false;
bool        g_show_add_menu = false;
bool        g_show_quest_prereq_menu = false;
bool        g_add_quest_prereq_examples = false;
bool        g_scroll_quest_prereqs_bottom = false;
bool        g_scroll_quest_prereqs_middle = false;
bool        g_select_quest_completion = false;
int         g_quest_completion_index = 1;
int         g_quest_focus_nodes = 0;
int         g_quest_focus_start = 0;
float       g_time_of_day = -1.0f;
float       g_pitch_offset = 0.0f;
int         g_settle_frames = 150;
int         g_countdown = 0;
int         g_timeout_frames = 0;
bool        g_capture_now = false;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

void finish(const std::string& msg, bool ok) {
    if (ok) OutputLog::success("autopilot: " + msg);
    else    OutputLog::error("autopilot: " + msg);
    g_stage = Stage::Done;
    if (g_auto_exit) PostQuitMessage(ok ? 0 : 1);
}

}

void AutoPilot_Init() {
    for (int i = 1; i < __argc; ++i) {
        const std::string arg = __argv[i];
        auto value = [&](const char* key) -> const char* {
            const size_t n = std::strlen(key);
            if (arg.compare(0, n, key) == 0 && arg.size() > n &&
                arg[n] == '=') {
                return arg.c_str() + n + 1;
            }
            return nullptr;
        };
        if (const char* v = value("--autoroot")) g_root_path = v;
        else if (const char* v = value("--autoload")) g_level_query = lower(v);
        else if (const char* v = value("--autoquest")) g_quest_query = lower(v);
        else if (const char* v = value("--autocustomquest"))
            g_custom_quest_id = v;
        else if (const char* v = value("--autoentity")) g_entity_query = lower(v);
        else if (const char* v = value("--autoshot")) g_shot_path = v;
        else if (const char* v = value("--autowait"))
            g_settle_frames = std::max(1, std::atoi(v));
        else if (const char* v = value("--autotime"))
            g_time_of_day = float(std::atof(v));
        else if (const char* v = value("--autopitch"))
            g_pitch_offset = float(std::atof(v));
        else if (const char* v = value("--questfocusnodes"))
            g_quest_focus_nodes = std::max(1, std::atoi(v));
        else if (const char* v = value("--questfocusstart"))
            g_quest_focus_start = std::max(1, std::atoi(v));
        else if (arg == "--autoexit") g_auto_exit = true;
        else if (arg == "--vistaonly") g_vista_only = true;
        else if (arg == "--skyshot") g_sky_shot = true;
        else if (arg == "--terrainshot") g_terrain_shot = true;
        else if (arg == "--questfitall") g_quest_fit_all = true;
        else if (arg == "--showdig") g_show_dig_spots = true;
        else if (arg == "--selectdig") {
            g_show_dig_spots = true;
            g_select_dig_spot = true;
        }
        else if (arg == "--selectentity") g_select_entity = true;
        else if (arg == "--showentitymodel") g_show_entity_model = true;
        else if (arg == "--showcreatenpc") g_show_create_npc = true;
        else if (arg == "--showaddmenu") g_show_add_menu = true;
        else if (arg == "--showquestprereqmenu")
            g_show_quest_prereq_menu = true;
        else if (arg == "--questprereqexamples")
            g_add_quest_prereq_examples = true;
        else if (arg == "--questprereqscrollbottom")
            g_scroll_quest_prereqs_bottom = true;
        else if (arg == "--questprereqscrollmiddle")
            g_scroll_quest_prereqs_middle = true;
        else if (arg == "--selectquestcompletion")
            g_select_quest_completion = true;
        else if (const char* v = value("--questcompletionindex")) {
            g_quest_completion_index = std::max(1, std::atoi(v));
            g_select_quest_completion = true;
        }
        else if (const char* v = value("--levprobe")) {
            std::string msg;
            const bool ok = LevelEdit::RunLevProbe(v, msg);
            if (ok) OutputLog::success("autopilot: " + msg);
            else    OutputLog::error("autopilot: " + msg);
            std::fprintf(ok ? stdout : stderr, "%s\n", msg.c_str());
            std::fflush(nullptr);
            ExitProcess(ok ? 0 : 1);
        }
        else if (const char* v = value("--levprobefloat")) {
            std::string msg;
            const bool ok = LevelEdit::RunLevProbeMode(v, true, msg);
            std::fprintf(ok ? stdout : stderr, "%s\n", msg.c_str());
            std::fflush(nullptr);
            ExitProcess(ok ? 0 : 1);
        }
        else if (const char* v = value("--fixstream")) {
            std::string msg;
            const bool ok = LevelEdit::RunStreamFix(v, msg);
            std::fprintf(ok ? stdout : stderr, "%s\n", msg.c_str());
            std::fflush(nullptr);
            ExitProcess(ok ? 0 : 1);
        }
    }
    if (g_quest_fit_all) QuestUI::SetInitialViewAll(true);
    if (g_quest_focus_start > 0) {
        QuestUI::SetInitialFocusNodeRange(
            static_cast<std::size_t>(g_quest_focus_start - 1),
            static_cast<std::size_t>(std::max(1, g_quest_focus_nodes)));
    } else if (g_quest_focus_nodes > 0) {
        QuestUI::SetInitialFocusNodeCount(
            static_cast<std::size_t>(g_quest_focus_nodes));
    }
    if (!g_level_query.empty() || !g_quest_query.empty() ||
        !g_custom_quest_id.empty() ||
        !g_entity_query.empty()) {
        g_stage = Stage::OpenRoot;
        g_timeout_frames = 60 * 60 * 5;
        const std::string target = !g_custom_quest_id.empty()
            ? "custom quest '" + g_custom_quest_id + "'"
            : !g_quest_query.empty()
            ? "quest matching '" + g_quest_query + "'"
            : (!g_entity_query.empty()
                ? "entity matching '" + g_entity_query + "'"
                : "level matching '" + g_level_query + "'");
        OutputLog::info("autopilot: will load " + target +
                        (g_shot_path.empty()
                             ? std::string()
                             : " and capture to " + g_shot_path));
    }
}

void AutoPilot_Tick() {
    if (g_stage == Stage::Idle || g_stage == Stage::Done) return;
    if (--g_timeout_frames <= 0) {
        finish("timed out", false);
        return;
    }

    switch (g_stage) {
    case Stage::OpenRoot: {
        if (!S.root_dir.empty()) {
            g_stage = Stage::WaitIndex;
            break;
        }
        const std::string& root =
            g_root_path.empty() ? S.last_dir : g_root_path;
        if (root.empty() || !std::filesystem::is_directory(root)) {
            finish("no saved root directory to open", false);
            break;
        }
        open_folder_logic(root);
        if (S.root_dir.empty()) {
            finish("failed to open root " + root, false);
            break;
        }
        g_stage = Stage::WaitIndex;
        break;
    }
    case Stage::WaitIndex: {
        if (UI::loading_in_progress()) break;
        if (!g_custom_quest_id.empty()) {
            g_countdown = 10;
            g_stage = Stage::OpenCustomQuest;
            break;
        }
        if (!g_quest_query.empty()) {
            if (S.all_quest_files.empty()) break;
            g_countdown = 10;
            g_stage = Stage::OpenQuest;
            break;
        }
        if (!g_entity_query.empty()) {
            if (g_global_entity_catalog.empty()) break;
            g_countdown = 10;
            g_stage = Stage::OpenEntity;
            break;
        }
        if (S.all_level_files.empty()) break;
        g_countdown = 10;
        g_stage = Stage::OpenLevel;
        break;
    }
    case Stage::OpenQuest: {
        if (--g_countdown > 0) break;
        if (!select_quest_script_by_query(g_quest_query)) {
            finish("no quest matches '" + g_quest_query + "'", false);
            break;
        }
        OutputLog::info("autopilot: opening quest " + S.lua_preview_title);
        g_stage = Stage::WaitQuest;
        break;
    }
    case Stage::WaitQuest: {
        if (S.lua_preview_loading) break;
        if (S.show_progress.load()) break;
        if (g_select_quest_completion) {
            QuestUI::RequestSelectCompletionNode(
                static_cast<std::size_t>(g_quest_completion_index - 1));
        }
        g_countdown = g_settle_frames;
        g_stage = Stage::Settle;
        break;
    }
    case Stage::OpenCustomQuest: {
        if (--g_countdown > 0) break;
        std::string error;
        if (!QuestUI::CreateNewQuest(g_custom_quest_id, error)) {
            finish("could not create custom quest: " + error, false);
            break;
        }
        ContentTabs::OpenCustomQuest(
            g_custom_quest_id, "Custom quest: " + g_custom_quest_id);
        S.selected_quest = -1;
        S.selected_item = -1;
        S.show_item_details = false;
        S.item_model_active = false;
        S.selected_entity = -1;
        S.show_entity_details = false;
        S.entity_model_active = false;
        S.viewing_lua = false;
        S.viewing_adb = false;
        S.show_gdb_render = false;
        S.lua_preview_selected = -1;
        S.lua_preview_title = "Custom quest: " + g_custom_quest_id;
        S.lua_preview_content = QuestUI::ActiveAuthoredLua();
        S.lua_preview_loading = false;
        S.lua_preview_is_quest = true;
        S.quest_preview_select_nodes = true;
        S.show_lua_render = true;
        if (g_show_quest_prereq_menu) {
            QuestUI::RequestOpenPrerequisiteMenu();
        }
        if (g_add_quest_prereq_examples) {
            QuestUI::AddPrerequisiteExamplesForCapture();
        }
        QuestUI::SetPrerequisiteInspectorCaptureScrollBottom(
            g_scroll_quest_prereqs_bottom);
        if (g_scroll_quest_prereqs_middle) {
            QuestUI::SetPrerequisiteInspectorCaptureScrollFraction(0.5f);
        }
        g_countdown = g_settle_frames;
        g_stage = Stage::Settle;
        break;
    }
    case Stage::OpenEntity: {
        if (--g_countdown > 0) break;
        if (!select_entity_by_query(g_entity_query)) {
            finish("no entity matches '" + g_entity_query + "'", false);
            break;
        }
        OutputLog::info("autopilot: opening entity " + g_entity_query);
        g_stage = Stage::WaitEntity;
        break;
    }
    case Stage::WaitEntity: {
        if (S.show_progress.load() || S.pending_preview_build ||
            !S.mdl_info_ok || !g_mp.has_model) {
            break;
        }
        S.cam_yaw = 3.14159265f;
        S.cam_pitch = 0.0f;
        S.cam_dist = 0.45f;
        S.cam_target_offset_y = g_mp.radius * 0.67f;
        if (g_show_create_npc) request_open_create_npc();
        g_countdown = g_settle_frames;
        g_stage = Stage::Settle;
        break;
    }
    case Stage::OpenLevel: {
        if (--g_countdown > 0) break;
        const FlatAssetEntry* best = nullptr;
        for (const auto& e : S.all_level_files) {
            const std::string full =
                lower(e.full_path.empty() ? e.name : e.full_path);
            if (full.find(g_level_query) == std::string::npos) continue;
            if (!best) best = &e;
            if (full.find("mainlevel") != std::string::npos) {
                best = &e;
                break;
            }
        }
        if (!best) {
            finish("no level matches '" + g_level_query + "'", false);
            break;
        }
        OutputLog::info("autopilot: opening level " +
                        (best->full_path.empty() ? best->name
                                                 : best->full_path));
        Level::OpenAsync(*best);
        g_countdown = 30;
        g_stage = Stage::WaitLevel;
        break;
    }
    case Stage::WaitLevel: {
        if (--g_countdown > 0) break;
        if (Level::IsAsyncLoadInProgress()) break;
        if (g_pending_terrain_load.load()) break;
        if (S.show_progress.load()) break;
        if (g_vista_only) {
            g_mp_vista_only = true;
            S.show_adjacent_terrain = true;

            S.cam_yaw   = 0.7f;
            S.cam_pitch = 0.65f;
            S.cam_dist  = 1.6f;
        }
        if (g_sky_shot) {

            g_mp_vista_only = false;
            g_flycam.pos[0] = g_mp.center[0];
            g_flycam.pos[1] = g_mp.center[1] +
                std::max(2.0f, g_mp.radius * 0.01f);
            g_flycam.pos[2] = g_mp.center[2];
            g_flycam.yaw = 0.0f;
            g_flycam.pitch = 0.55f;
            g_flycam.is_looking = false;
        }
        if (g_terrain_shot) {

            g_mp_vista_only = false;
            S.show_adjacent_terrain = false;
            g_mp.show_mist = false;
            g_mp.show_weather = false;
            g_mp.fx_show = false;
            g_mp.weather_mist_strength = 0.0f;
            g_mp.has_fog_theme = false;
            std::fill(std::begin(g_mp.fog_range),
                      std::end(g_mp.fog_range), 0.0f);
            std::fill(std::begin(g_mp.fog_density),
                      std::end(g_mp.fog_density), 0.0f);
            for (auto& key : g_mp.day_night_keyframes) {
                key.weather_mist_strength = 0.0f;
                key.has_fog_theme = false;
                std::fill(std::begin(key.fog_range),
                          std::end(key.fog_range), 0.0f);
                std::fill(std::begin(key.fog_density),
                          std::end(key.fog_density), 0.0f);
            }

            const MPPerMesh* primary_terrain = nullptr;
            for (const auto& mesh : g_mp.meshes) {
                if (!mesh.is_terrain ||
                    mesh.name.rfind("adjacent terrain", 0) == 0) {
                    continue;
                }
                primary_terrain = &mesh;
                break;
            }

            if (primary_terrain) {
                const float cx = primary_terrain->center[0];
                const float cy = primary_terrain->center[1];
                const float cz = primary_terrain->center[2];
                const float r = std::max(primary_terrain->radius, 1.0f);
                float camera_y = cy +
                    std::clamp(r * 0.15f, 8.0f, 80.0f);
                float min_cloud_y = std::numeric_limits<float>::max();
                auto consider_clouds = [&min_cloud_y](
                    int count, const float layers[4][4]) {
                    for (int i = 0; i < std::min(count, 4); ++i) {
                        if (layers[i][0] > 0.0f) {
                            min_cloud_y = std::min(min_cloud_y,
                                                   layers[i][1]);
                        }
                    }
                };
                consider_clouds(g_mp.cloud_layer_count, g_mp.cloud_layer);
                for (const auto& key : g_mp.day_night_keyframes) {
                    consider_clouds(key.cloud_layer_count, key.cloud_layer);
                }
                if (min_cloud_y < std::numeric_limits<float>::max()) {
                    camera_y = std::min(camera_y, min_cloud_y - 8.0f);
                    camera_y = std::max(camera_y, cy + 2.0f);
                }

                g_flycam.pos[0] = cx;
                g_flycam.pos[1] = camera_y;
                g_flycam.pos[2] = cz - r * 0.60f;
                const float dx = cx - g_flycam.pos[0];
                const float dy = cy - g_flycam.pos[1];
                const float dz = cz - g_flycam.pos[2];
                g_flycam.yaw = std::atan2(dx, dz);
                g_flycam.pitch = std::atan2(
                    dy, std::sqrt(dx * dx + dz * dz));
            } else {
                const float r = std::max(1.0f, g_mp.radius);
                g_flycam.pos[0] = g_mp.center[0];
                g_flycam.pos[1] = g_mp.center[1] + r * 0.01f;
                g_flycam.pos[2] = g_mp.center[2] - r * 0.35f;
                g_flycam.yaw = 0.0f;
                g_flycam.pitch = -0.12f;
            }
            g_flycam.is_looking = false;
        }
        if (g_pitch_offset != 0.0f) {
            g_flycam.pitch = std::clamp(g_flycam.pitch + g_pitch_offset,
                                        -1.4f, 1.4f);
        }
        if (g_time_of_day >= 0.0f) {
            g_mp.time_of_day_override = true;
            g_mp.time_of_day_override_value =
                std::clamp(g_time_of_day / 24.0f, 0.0f, 1.0f);
        }
        if (g_show_dig_spots) {
            std::vector<std::size_t> dig_markers;
            for (std::size_t i = 0; i < g_level_spawn_markers.size(); ++i) {
                if (g_level_spawn_markers[i].kind == 4) {
                    dig_markers.push_back(i);
                }
            }
            if (dig_markers.empty()) {
                finish("loaded level has no dig spots", false);
                break;
            }

            S.show_dig_spots = true;
            S.show_spawn_markers = false;
            S.show_ent_npcs = false;
            S.show_ent_text = false;
            S.show_adjacent_terrain = false;
            g_mp.show_sky = false;
            g_mp.show_weather = false;
            g_mp.show_mist = false;
            g_mp.time_of_day_override = true;
            g_mp.time_of_day_override_value = 0.5f;

            std::size_t selected = dig_markers.front();
            if (g_select_dig_spot) {
                for (std::size_t index : dig_markers) {
                    const auto found = g_level_entity_contents.find(
                        g_level_spawn_markers[index].entity_hash);
                    if (found != g_level_entity_contents.end() &&
                        (!found->second.initial_items.empty() ||
                         !found->second.potential_items.empty())) {
                        selected = index;
                        break;
                    }
                }
            }

            if (g_select_dig_spot) {
                const auto& marker = g_level_spawn_markers[selected];
                const float target_y = marker.z + 0.5f;
                g_flycam.pos[0] = marker.x - 8.0f;
                g_flycam.pos[1] = marker.z + 8.0f;
                g_flycam.pos[2] = marker.y - 13.0f;
                const float dx = marker.x - g_flycam.pos[0];
                const float dy = target_y - g_flycam.pos[1];
                const float dz = marker.y - g_flycam.pos[2];
                g_flycam.yaw = std::atan2(dx, dz);
                g_flycam.pitch = std::atan2(
                    dy, std::sqrt(dx * dx + dz * dz));
                UI::select_level_marker(selected);
            } else {
                float min_x = std::numeric_limits<float>::max();
                float min_y = std::numeric_limits<float>::max();
                float min_z = std::numeric_limits<float>::max();
                float max_x = std::numeric_limits<float>::lowest();
                float max_y = std::numeric_limits<float>::lowest();
                float max_z = std::numeric_limits<float>::lowest();
                for (std::size_t index : dig_markers) {
                    const auto& marker = g_level_spawn_markers[index];
                    min_x = std::min(min_x, marker.x);
                    min_y = std::min(min_y, marker.y);
                    min_z = std::min(min_z, marker.z);
                    max_x = std::max(max_x, marker.x);
                    max_y = std::max(max_y, marker.y);
                    max_z = std::max(max_z, marker.z);
                }
                const float center_x = (min_x + max_x) * 0.5f;
                const float center_y = (min_y + max_y) * 0.5f;
                const float center_z = (min_z + max_z) * 0.5f;
                const float span = std::max(
                    25.0f, std::max(max_x - min_x, max_y - min_y));
                g_flycam.pos[0] = center_x;
                g_flycam.pos[1] = max_z + span * 0.75f;
                g_flycam.pos[2] = center_y - span * 0.9f;
                const float dx = center_x - g_flycam.pos[0];
                const float dy = center_z - g_flycam.pos[1];
                const float dz = center_y - g_flycam.pos[2];
                g_flycam.yaw = std::atan2(dx, dz);
                g_flycam.pitch = std::atan2(
                    dy, std::sqrt(dx * dx + dz * dz));
            }
            g_flycam.is_looking = false;
            OutputLog::success(
                "autopilot: framed " + std::to_string(dig_markers.size()) +
                " Brightwood dig spot(s)");
        }
        if (g_select_entity || g_show_entity_model) {
            std::size_t selected = g_level_spawn_markers.size();
            int best_score = -1;
            for (std::size_t i = 0; i < g_level_spawn_markers.size(); ++i) {
                const auto& marker = g_level_spawn_markers[i];
                if (marker.kind != 3 && marker.kind != 1) continue;
                bool has_rendered_model = false;
                for (const MPPerMesh& mesh : g_mp.meshes) {
                    if (!mesh.is_entity_model) continue;
                    for (const auto& range : mesh.pick_ranges) {
                        if (range.gdb_entity_hash == marker.entity_hash) {
                            has_rendered_model = true;
                            break;
                        }
                    }
                    if (has_rendered_model) break;
                }
                if (!has_rendered_model) continue;
                const uint32_t gameplay_hash = marker.creature_entity_hash
                    ? marker.creature_entity_hash : marker.entity_hash;
                const auto found =
                    g_level_entity_gameplay.find(gameplay_hash);
                if (found == g_level_entity_gameplay.end()) continue;
                const auto& details = found->second;
                int score = marker.kind == 3 ? 100 : 25;
                score += static_cast<int>(details.core_fields.size()) * 5;
                score += static_cast<int>(details.combat_fields.size()) * 3;
                if (!details.faction_name.empty()) score += 10;
                if (!details.combat_profile_name.empty()) score += 10;
                if (score > best_score) {
                    best_score = score;
                    selected = i;
                }
            }
            if (selected == g_level_spawn_markers.size()) {
                finish("loaded level has no entity with gameplay details",
                       false);
                break;
            }

            const auto& marker = g_level_spawn_markers[selected];
            S.show_spawn_markers = marker.kind == 1;
            S.show_ent_npcs = marker.kind == 3;
            S.show_dig_spots = false;
            S.show_containers = false;
            S.show_ent_text = false;
            S.show_entity_models = true;
            S.show_adjacent_terrain = false;
            g_mp.show_sky = false;
            g_mp.show_weather = false;
            g_mp.show_mist = false;
            g_mp.time_of_day_override = true;
            g_mp.time_of_day_override_value = 0.5f;

            float min_x = std::numeric_limits<float>::max();
            float min_y = std::numeric_limits<float>::max();
            float min_z = std::numeric_limits<float>::max();
            float max_x = std::numeric_limits<float>::lowest();
            float max_y = std::numeric_limits<float>::lowest();
            float max_z = std::numeric_limits<float>::lowest();
            for (const MPPerMesh& mesh : g_mp.meshes) {
                if (!mesh.is_entity_model) continue;
                for (const auto& range : mesh.pick_ranges) {
                    if (range.gdb_entity_hash != marker.entity_hash) continue;
                    const float radius = std::max(range.radius, 0.1f);
                    min_x = std::min(min_x, range.center[0] - radius);
                    min_y = std::min(min_y, range.center[1] - radius);
                    min_z = std::min(min_z, range.center[2] - radius);
                    max_x = std::max(max_x, range.center[0] + radius);
                    max_y = std::max(max_y, range.center[1] + radius);
                    max_z = std::max(max_z, range.center[2] + radius);
                }
            }
            const bool have_bounds = min_x <= max_x && min_y <= max_y &&
                                     min_z <= max_z;
            const float target_x = have_bounds
                ? (min_x + max_x) * 0.5f : marker.x;
            const float target_y = have_bounds
                ? (min_y + max_y) * 0.5f : marker.z + 1.0f;
            const float target_z = have_bounds
                ? (min_z + max_z) * 0.5f : marker.y;
            const float radius = have_bounds
                ? std::max(0.75f, std::max({max_x - min_x,
                                            max_y - min_y,
                                            max_z - min_z}) * 0.5f)
                : 1.0f;
            g_flycam.pos[0] = target_x - radius * 1.7f;
            g_flycam.pos[1] = target_y + radius * 0.20f;
            g_flycam.pos[2] = target_z - radius * 3.0f;
            const float dx = target_x - g_flycam.pos[0];
            const float dy = target_y - g_flycam.pos[1];
            const float dz = target_z - g_flycam.pos[2];
            g_flycam.yaw = std::atan2(dx, dz);
            g_flycam.pitch = std::atan2(
                dy, std::sqrt(dx * dx + dz * dz));
            g_flycam.is_looking = false;
            if (g_select_entity) {
                if (marker.kind == 3) {
                    UI::select_level_entity_model(marker.entity_hash);
                } else {
                    UI::select_level_marker(selected);
                }
            }

            const uint32_t gameplay_hash = marker.creature_entity_hash
                ? marker.creature_entity_hash : marker.entity_hash;
            const auto found = g_level_entity_gameplay.find(gameplay_hash);
            const std::string details_name =
                found != g_level_entity_gameplay.end()
                    ? found->second.entity_name : std::string();
            OutputLog::success(
                std::string("autopilot: ") +
                (g_select_entity ? "selected entity "
                                 : "framed entity model ") +
                (!marker.name.empty() ? marker.name : details_name));
        }
        if (g_show_add_menu) {
            float pos[3] = {g_mp.center[0], g_mp.center[2], g_mp.center[1]};
            if (!g_level_spawn_markers.empty()) {
                pos[0] = g_level_spawn_markers.front().x;
                pos[1] = g_level_spawn_markers.front().y;
                pos[2] = g_level_spawn_markers.front().z;
            }
            UI::request_level_add_menu_at(pos);
        }
        g_countdown = g_settle_frames;
        g_stage = Stage::Settle;
        break;
    }
    case Stage::Settle: {
        if (--g_countdown > 0) break;
        if (g_shot_path.empty()) {
            finish("level loaded (no capture requested)", true);
            break;
        }
        g_capture_now = true;
        g_stage = Stage::Capture;
        break;
    }
    default:
        break;
    }
}

void AutoPilot_Capture(ID3D11Device* device,
                       ID3D11DeviceContext* context,
                       IDXGISwapChain* swapchain) {
    if (!g_capture_now) return;
    g_capture_now = false;

    ID3D11Texture2D* back = nullptr;
    if (FAILED(swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                    (void**)&back)) || !back) {
        finish("could not get backbuffer", false);
        return;
    }
    D3D11_TEXTURE2D_DESC desc = {};
    back->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    ID3D11Texture2D* staging = nullptr;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging)) ||
        !staging) {
        back->Release();
        finish("could not create staging texture", false);
        return;
    }
    context->CopyResource(staging, back);
    back->Release();

    D3D11_MAPPED_SUBRESOURCE map = {};
    if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
        staging->Release();
        finish("could not map staging texture", false);
        return;
    }

    const int w = (int)desc.Width;
    const int h = (int)desc.Height;
    const bool bgra = (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                       desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
    std::vector<uint8_t> rgba((size_t)w * h * 4);
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = (const uint8_t*)map.pData +
                             (size_t)y * map.RowPitch;
        uint8_t* dst = rgba.data() + (size_t)y * w * 4;
        if (bgra) {
            for (int x = 0; x < w; ++x) {
                dst[x * 4 + 0] = src[x * 4 + 2];
                dst[x * 4 + 1] = src[x * 4 + 1];
                dst[x * 4 + 2] = src[x * 4 + 0];
                dst[x * 4 + 3] = 0xFF;
            }
        } else {
            std::memcpy(dst, src, (size_t)w * 4);
            for (int x = 0; x < w; ++x) dst[x * 4 + 3] = 0xFF;
        }
    }
    context->Unmap(staging, 0);
    staging->Release();

    if (tex_export_png(g_shot_path, rgba.data(), w, h)) {
        finish("captured " + std::to_string(w) + "x" + std::to_string(h) +
               " to " + g_shot_path, true);
    } else {
        finish("failed to write " + g_shot_path, false);
    }
}

#endif
