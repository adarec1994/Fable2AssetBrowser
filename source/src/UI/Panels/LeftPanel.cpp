#include "../UI_Panels.h"
#include "PanelInternal.h"
#include "NewLevelDialog.h"
#include "../UI_Main.h"
#include "../OutputLog.h"
#include "../ModelPreview.h"
#include "../EntityModelResolver.h"
#include "../ContentTabs.h"
#include "DetailsPanel.h"
#include "../../Level/Creation/GameRegistry.h"
#include "../../Level/Creation/LandscapeAuthoring.h"
#include "../../Level/Creation/NewLevel.h"
#include "../Quest/QuestNodeView.h"

#include "../../ISO/IsoDump.h"
#include "../../Quest/QuestInjection.h"
#include "../../Entity/NpcAuthoring.h"
#include "../../Entity/StaticPropAuthoring.h"
#include "../../Level/Editing/LevelEdit.h"
#include "../../Level/Core/LevelLoader.h"
#include "../../Level/Database/TextBank.h"
#include "../../Level/Core/LevelExport.h"
#include "../../animations/AnimDataFile.h"
#include "../../animations/AnimPlayer.h"
#include "../../animations/AnimRigMap.h"
#include "../../MDL/ModelParser.h"

#include "../../Lua.h"
#include "../../Utilities/Progress.h"
#include "../../Utilities/Utils.h"
#include "../../Utilities/GameBackup.h"
#include "../../BNKCore.cpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "IconsFontAwesome6.h"
#include <filesystem>
#include <algorithm>
#include <atomic>
#include <limits>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <cstring>
#include <cmath>

extern ModelPreview g_mp;

namespace {

struct EntityPreviewCompletion {
    std::uint64_t request = 0;
    int entity_index = -1;
    MDLInfo model_info;
    std::vector<MDLMeshGeom> meshes;
    std::string primary_model_path;
    std::uint32_t primary_model_hash = 0;
};

std::atomic<std::uint64_t> g_entity_preview_request{0};
std::mutex g_entity_preview_mutex;
std::vector<EntityPreviewCompletion> g_entity_preview_completions;

NpcAuthoring::Definition g_new_npc;
int g_new_npc_template_index = -1;
char g_new_npc_template_filter[128]{};
std::string g_new_npc_error;
StaticPropAuthoring::Definition g_new_static_prop;
int g_new_static_prop_model_index = -1;
char g_new_static_prop_model_filter[128]{};
std::string g_new_static_prop_error;
enum class NewEntityKind : int {
    Npc = 0,
    StaticProp = 1,
};
NewEntityKind g_new_entity_kind = NewEntityKind::Npc;
bool g_open_create_npc_requested = false;

void select_new_npc_template(int index) {
    if (index < 0 ||
        static_cast<std::size_t>(index) >= g_global_entity_catalog.size()) {
        return;
    }
    if (g_global_entity_catalog[static_cast<std::size_t>(index)].kind !=
        Gdb::EntityCatalogKind::Creature) {
        return;
    }
    const std::string internal_name = g_new_npc.internal_name;
    const std::string display_name = g_new_npc.display_name;
    g_new_npc = NpcAuthoring::Definition{};
    g_new_npc.internal_name = internal_name;
    g_new_npc.display_name = display_name;
    g_new_npc_template_index = index;

    const Gdb::CreatureCatalogEntry& entity =
        g_global_entity_catalog[static_cast<std::size_t>(index)];
    g_new_npc.template_name = entity.display_name.empty()
        ? entity.name : entity.display_name;
    g_new_npc.template_entity = entity.entity_hash;
    g_new_npc.model_hashes = entity.model_hashes;

    const auto gameplay = g_global_entity_gameplay.find(entity.entity_hash);
    if (gameplay == g_global_entity_gameplay.end()) {
        g_new_npc_error =
            "That entity has no indexed NPC gameplay components.";
        return;
    }
    const Gdb::EntityGameplayDetails& details = gameplay->second;
    g_new_npc.creature_component = details.creature_component_record;
    g_new_npc.health_component = details.health_component_record;
    g_new_npc.combat_component = details.combat_component_record;
    g_new_npc.faction_component = details.faction_component_record;
    g_new_npc.faction_record = details.faction_record;
    g_new_npc.faction_name = details.faction_name;
    g_new_npc.combat_profile_record = details.combat_profile_record;
    g_new_npc.combat_profile_name = details.combat_profile_name;
    for (const auto& option : g_global_entity_gameplay_options.factions) {
        if (option.record_hash == g_new_npc.faction_record) {
            g_new_npc.faction_name = option.label;
            break;
        }
    }
    for (const auto& option :
         g_global_entity_gameplay_options.combat_profiles) {
        if (option.record_hash == g_new_npc.combat_profile_record) {
            g_new_npc.combat_profile_name = option.label;
            break;
        }
    }
    for (const Gdb::EntityGameplayField& source : details.core_fields) {
        NpcAuthoring::FieldValue value;
        value.label = source.label;
        value.display_value = source.value;
        value.field_hash = source.field_hash;
        value.raw_value = source.raw_value;
        value.value_type = source.value_type;
        g_new_npc.core_fields.push_back(std::move(value));
    }
    for (const Gdb::EntityGameplayField& source : details.combat_fields) {
        NpcAuthoring::FieldValue value;
        value.label = source.label;
        value.display_value = source.value;
        value.field_hash = source.field_hash;
        value.raw_value = source.raw_value;
        value.value_type = source.value_type;
        g_new_npc.combat_fields.push_back(std::move(value));
    }
    g_new_npc_error.clear();
}

}

void request_open_create_npc() {
    g_open_create_npc_requested = true;
}

static const char* const kLeftPanelTabLabels[] = {
    "BNK List", "File Tree", "Levels", "Lua Scripts",
    "Models", "Textures", "Audio", "Animations", "Items", "Entities",
    "Quests"
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

static const FlatAssetEntry* find_model_by_path_hash_left(uint32_t h) {
    return FindGlobalModelAssetByPathHash(h);
}



static const FlatAssetEntry* find_model_by_path_left(
    const std::string& path) {
    if (path.empty()) return nullptr;
    auto norm = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        std::replace(s.begin(), s.end(), '/', '\\');
        return s;
    };
    const std::string want = norm(path);
    const size_t ws = want.find_last_of('\\');
    const std::string want_leaf =
        ws == std::string::npos ? want : want.substr(ws + 1);
    const FlatAssetEntry* leaf_hit = nullptr;
    for (const auto& mf : S.all_mdl_files) {
        const std::string full = norm(mf.full_path);
        if (full == want) return &mf;
        if (!leaf_hit) {
            const size_t fs = full.find_last_of('\\');
            const std::string leaf =
                fs == std::string::npos ? full : full.substr(fs + 1);
            if (leaf == want_leaf) leaf_hit = &mf;
        }
    }
    return leaf_hit;
}

static void apply_entity_preview_completions() {
    std::vector<EntityPreviewCompletion> completed;
    {
        std::lock_guard<std::mutex> lock(g_entity_preview_mutex);
        completed.swap(g_entity_preview_completions);
    }
    for (EntityPreviewCompletion& result : completed) {
        if (result.request != g_entity_preview_request.load() ||
            result.entity_index != S.selected_entity ||
            ContentTabs::ActiveKind() != ContentTabs::Kind::Entity) {
            continue;
        }
        if (result.meshes.empty()) {
            OutputLog::warn("entity preview: no renderable model parts found");
            continue;
        }
        S.hex_data.clear();
        S.mdl_info_ok = true;
        S.mdl_info = std::move(result.model_info);
        S.mdl_meshes = std::move(result.meshes);
        S.current_mdl_path = std::move(result.primary_model_path);
        S.current_mdl_path_hash = result.primary_model_hash;
        S.item_model_active = false;
        S.selected_item = -1;
        S.show_item_details = false;
        S.entity_model_active = true;
        S.show_entity_details = true;
        S.cam_yaw = 3.14159265f;
        S.cam_pitch = 0.2f;
        S.cam_dist = 3.0f;
        S.pending_model_tab_capture = true;
        S.pending_preview_build = true;
    }
}

static void load_entity_preview(int entity_index) {
    if (entity_index < 0 ||
        entity_index >= static_cast<int>(g_global_entity_catalog.size())) {
        return;
    }
    const auto& entity = g_global_entity_catalog[entity_index];
    if (entity.model_hashes.empty()) {
        OutputLog::warn("entity preview: no model assets resolve for '" +
                        (entity.display_name.empty() ? entity.name
                                                     : entity.display_name) +
                        "'");
        return;
    }

    const std::uint64_t request = ++g_entity_preview_request;
    g_mp.has_model = false;
    S.mdl_info_ok = false;
    S.mdl_meshes.clear();
    progress_open(0, "Loading full entity model...");
    std::thread([request, entity_index,
                 model_hashes = entity.model_hashes]() mutable {
        EntityPreviewCompletion result;
        result.request = request;
        result.entity_index = entity_index;
        EntityModels::ResolvedModel resolved;
        std::string error;
        if (EntityModels::Resolve(model_hashes, resolved, &error)) {
            result.model_info = std::move(resolved.info);
            result.meshes = std::move(resolved.meshes);
            result.primary_model_path =
                std::move(resolved.primary_model_path);
            result.primary_model_hash = resolved.primary_model_hash;
        }

        {
            std::lock_guard<std::mutex> lock(g_entity_preview_mutex);
            g_entity_preview_completions.push_back(std::move(result));
        }
        progress_done();
    }).detach();
}

bool select_entity_by_query(const std::string& query) {
    std::string needle = query;
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });

    int best = -1;
    for (int i = 0; i < static_cast<int>(g_global_entity_catalog.size()); ++i) {
        const auto& entity = g_global_entity_catalog[static_cast<size_t>(i)];
        std::string name = entity.name;
        std::string display = entity.display_name;
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        std::transform(display.begin(), display.end(), display.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (name == needle || display == needle) {
            best = i;
            break;
        }
        if (best < 0 &&
            (name.find(needle) != std::string::npos ||
             display.find(needle) != std::string::npos)) {
            best = i;
        }
    }
    if (best < 0) return false;

    const auto& entity = g_global_entity_catalog[static_cast<size_t>(best)];
    const std::string& label = entity.display_name.empty()
        ? entity.name : entity.display_name;
    ContentTabs::OpenEntity(best, label);
    load_entity_preview(best);
    return true;
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
                S.show_gdb_render = false;
                g_pending_mdl_full_path = e.full_path;
                g_pending_mdl_load = true;
                g_pending_mdl_index = (int)i;
            } else if (kind == 1) {
                S.show_gdb_render = false;
                g_pending_tex_load = true;
                g_pending_tex_index = (int)i;
            } else if (kind == 2) {

                S.show_gdb_render = false;
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
DrillState g_tree_drill;

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

std::string lua_script_list_label(const LuaFileUI& e) {
    std::string label = e.path;
    if (!S.root_dir.empty() && label.rfind(S.root_dir, 0) == 0) {
        label.erase(0, S.root_dir.size());
        while (!label.empty() && (label.front() == '\\' || label.front() == '/')) {
            label.erase(label.begin());
        }
    } else if (label.rfind("iso://", 0) == 0) {
        label.erase(0, 6);
    }
    if (label.empty()) {
        label = e.filename.empty() ? e.path : e.filename;
    }
    return label;
}

void select_lua_script(size_t idx) {
    S.viewing_lua = true;
    S.viewing_adb = false;
    S.show_gdb_render = false;
    S.selected_bnk.clear();
    S.global_search.clear();
    S.files.clear();
    S.files.reserve(S.lua_files.size());
    S.selected_file_index = -1;

    for (size_t i = 0; i < S.lua_files.size(); ++i) {
        S.files.push_back({(int)i, S.lua_files[i].filename,
                           S.lua_files[i].size});
    }

    if (idx >= S.lua_files.size()) {
        return;
    }

    S.selected_file_index = (int)idx;

    const std::string lua_path = S.lua_files[idx].path;
    const std::string lua_title = S.lua_files[idx].filename;
    ContentTabs::OpenLua(lua_path, lua_title, false);

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

    S.lua_preview_selected = (int)idx;
    S.lua_preview_title    = lua_title;
    S.lua_preview_content.clear();
    S.lua_preview_loading  = true;
    S.lua_preview_is_quest = false;
    S.quest_preview_select_nodes = false;
    const uint64_t preview_request = ++S.lua_preview_request;
    S.show_lua_render      = true;
    S.show_gdb_render      = false;

    OutputLog::info("Decompiling Lua: " + lua_title);
    progress_open(0, "Decompiling " + lua_title + "...");
    std::thread([lua_path, preview_request]() {
        std::string content = read_lua_file_content(lua_path);
        ContentTabs::CompleteLua(lua_path, content);
        if (S.lua_preview_request.load() == preview_request) {
            S.lua_preview_content = std::move(content);
            S.lua_preview_loading = false;
        }
        progress_done();
    }).detach();
}

bool quest_source_has_debug_symbols(std::string bnk_path) {
    std::replace(bnk_path.begin(), bnk_path.end(), '\\', '/');
    const size_t slash = bnk_path.find_last_of('/');
    if (slash != std::string::npos) bnk_path.erase(0, slash + 1);
    std::transform(bnk_path.begin(), bnk_path.end(), bnk_path.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return bnk_path == "gamescripts.bnk";
}

void select_quest_script(size_t idx) {
    if (idx >= S.all_quest_files.size()) return;

    const FlatAssetEntry entry = S.all_quest_files[idx];
    const std::string quest_tab_key = entry.full_path.empty()
        ? entry.bnk_path + "#" + std::to_string(entry.file_index)
        : entry.full_path;
    const std::string quest_tab_title = entry.full_path.empty()
        ? entry.name : entry.full_path;
    ContentTabs::OpenLua(quest_tab_key, quest_tab_title, true);
    S.selected_quest = (int)idx;
    S.selected_item = -1;
    S.show_item_details = false;
    S.item_model_active = false;
    S.selected_entity = -1;
    S.show_entity_details = false;
    S.entity_model_active = false;
    S.viewing_lua = false;
    S.viewing_adb = false;
    S.show_gdb_render = false;

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
    S.texture_window_width = 0;
    S.texture_window_height = 0;
#else
    g_mp.has_model = false;
#endif

    S.lua_preview_selected = -1;
    S.lua_preview_title = quest_tab_title;
    S.lua_preview_content.clear();
    S.lua_preview_loading = true;
    S.lua_preview_is_quest = true;
    S.quest_preview_select_nodes = true;
    const uint64_t preview_request = ++S.lua_preview_request;
    QuestUI::Clear();
    QuestUI::RefreshReferenceCatalog();
    S.show_lua_render = true;

    OutputLog::info("Decompiling quest Lua: " + entry.full_path);
    progress_open(0, "Decompiling " + entry.name + "...");
    std::thread([entry, quest_tab_key, preview_request]() {
        std::string content;
        try {
            const auto bytes =
                BnkCache::extract_bytes(entry.bnk_path, entry.file_index);
            if (bytes.empty()) {
                content = "-- Error: empty quest script entry";
            } else if (bytes.size() > 10 * 1024 * 1024) {
                content = "-- Error: quest script is too large to preview (>10MB)";
            } else {
                const bool is_bytecode =
                    bytes.size() >= 4 && bytes[0] == 0x1B &&
                    bytes[1] == 'L' && bytes[2] == 'u' &&
                    bytes[3] == 'a';
                if (is_bytecode) {
                    content = decompile_lua51_bytecode(bytes.data(),
                                                       bytes.size());
                } else {
                    content.assign(bytes.begin(), bytes.end());
                }
            }
        } catch (const std::exception& ex) {
            content = std::string("-- Error: ") + ex.what();
        } catch (...) {
            content = "-- Error: extracting quest Lua failed";
        }
        ContentTabs::CompleteLua(quest_tab_key, content);
        if (S.lua_preview_request.load() == preview_request) {
            QuestUI::SetQuestSource(
                entry.full_path.empty() ? entry.name : entry.full_path,
                content);
            if (S.lua_preview_request.load() == preview_request) {
                S.lua_preview_content = std::move(content);
                S.lua_preview_loading = false;
            }
        }
        progress_done();
    }).detach();
}

void show_authored_quest(const std::string& quest_id) {
    ++S.lua_preview_request;
    if (!QuestUI::OpenAuthoredQuest(quest_id)) return;
    ContentTabs::OpenCustomQuest(quest_id,
                                "Custom quest: " + quest_id);

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
    S.lua_preview_title = "Custom quest: " + quest_id;
    S.lua_preview_content = QuestUI::ActiveAuthoredLua();
    S.lua_preview_loading = false;
    S.lua_preview_is_quest = true;
    S.quest_preview_select_nodes = true;
    S.show_lua_render = true;
}

bool shipped_quest_id_exists(const std::string& quest_id) {
    std::string wanted = quest_id;
    std::transform(wanted.begin(), wanted.end(), wanted.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    for (const FlatAssetEntry& entry : S.all_quest_files) {
        std::string stem = std::filesystem::path(
            entry.name.empty() ? entry.full_path : entry.name).stem().string();
        std::transform(stem.begin(), stem.end(), stem.begin(),
                       [](unsigned char c) {
                           return char(std::tolower(c));
                       });
        if (stem == wanted) return true;
    }
    return false;
}

std::atomic<bool> g_quest_injection_busy{false};
void inject_active_authored_quest();

std::string authored_quest_entry_path(const std::string& quest_id) {
    std::string lower = quest_id;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return "scripts/quests/" + lower + ".lua";
}

bool collect_quest_injection_targets(
    const std::string& quest_id,
    std::vector<QuestInjection::BankTarget>& targets,
    std::string& error) {
    targets.clear();
    error.clear();
    const std::filesystem::path data =
        std::filesystem::path(S.root_dir) / "data";
    for (const char* filename : {"gamescripts.bnk",
                                 "gamescripts_r.bnk"}) {
        const std::filesystem::path path = data / filename;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) continue;

        QuestInjection::BankTarget target;
        target.path = path.string();
        target.gameflow_lua_index = BnkCache::find_index(
            target.path, "scripts/quests/gameflow.lua");
        target.gameflow_text_index = BnkCache::find_index(
            target.path, "scripts/quests/gameflow.txt");
        target.quest_script_index = BnkCache::find_index(
            target.path, authored_quest_entry_path(quest_id));
        if (target.gameflow_lua_index < 0 ||
            target.gameflow_text_index < 0) {
            error = std::string(filename) +
                    " does not contain gameflow.lua and gameflow.txt";
            return false;
        }
        try {
            target.gameflow_source = BnkCache::extract_bytes(
                target.path, target.gameflow_text_index);
        } catch (const std::exception& ex) {
            error = std::string("Could not read ") + filename + ": " +
                    ex.what();
            return false;
        }
        targets.push_back(std::move(target));
    }
    if (targets.empty()) {
        error = "No loose gamescripts.bnk files were found under the "
                "selected Fable 2 root.";
        return false;
    }
    return true;
}

void inject_active_authored_quest() {
    {
        std::string backup_error;
        if (!GameBackup::RequireBackup(backup_error)) {
            OutputLog::error("quest save: " + backup_error);
            return;
        }
    }
    if (g_quest_injection_busy.exchange(true)) return;
    std::string validation_error;
    if (!QuestUI::ValidateActiveAuthoredQuest(validation_error)) {
        g_quest_injection_busy = false;
        show_error_box(validation_error);
        return;
    }
    if (LevelEdit::Dirty()) {
        g_quest_injection_busy = false;
        show_error_box(
            "Save the referenced level first. Its normal level backup "
            "will protect the NPC/container changes.");
        return;
    }
    const std::string quest_id = QuestUI::ActiveAuthoredQuestId();
    const std::string quest_lua = QuestUI::ActiveAuthoredQuestLua();
    const std::string eligibility =
        QuestUI::ActiveAuthoredEligibilityLua();
    const auto localized_text = QuestUI::ActiveAuthoredTextEntries();
    const std::string root = S.root_dir;
    std::vector<QuestInjection::BankTarget> targets;
    std::string error;
    if (!collect_quest_injection_targets(quest_id, targets, error)) {
        g_quest_injection_busy = false;
        show_error_box(error);
        return;
    }

    progress_open(100, "Saving " + quest_id + "...");
    std::thread([root, quest_id, quest_lua, eligibility, localized_text,
                 targets = std::move(targets)]() mutable {
        std::string result;
        std::string error;
        const bool ok = QuestInjection::Inject(
            root, quest_id, quest_lua, eligibility, localized_text, targets,
            result, error);
        for (const QuestInjection::BankTarget& target : targets) {
            BnkCache::invalidate(target.path);
        }
        if (ok) {
            OutputLog::success("quest saved: " + result);
            show_completion_box(result);
        } else {
            OutputLog::error("quest save failed: " + error);
            show_error_box("Quest save failed:\n" + error);
        }
        progress_done();
        g_quest_injection_busy = false;
    }).detach();
}

void drill_back(DrillState& d) {
    d.target_t = 0.0f;

}

bool drill_settled(const DrillState& d) {
    return std::abs(d.anim_t - d.target_t) < 0.001f;
}

std::vector<std::pair<uint32_t, std::string>> npc_reference_options(
    uint32_t field_hash) {
    std::unordered_map<uint32_t, std::string> unique;
    for (const auto& entry : g_global_entity_gameplay) {
        auto add = [&](const std::vector<Gdb::EntityGameplayField>& fields) {
            for (const auto& field : fields) {
                if (field.field_hash == field_hash &&
                    (field.value_type == 4 || field.value_type == 6 ||
                     field.value_type == 7) && field.raw_value != 0) {
                    unique.try_emplace(field.raw_value, field.value);
                }
            }
        };
        add(entry.second.core_fields);
        add(entry.second.combat_fields);
    }
    std::vector<std::pair<uint32_t, std::string>> out(unique.begin(),
                                                       unique.end());
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.second < b.second;
    });
    return out;
}

void draw_npc_value_field(NpcAuthoring::FieldValue& field) {
    ImGui::PushID(static_cast<int>(field.field_hash));
    if (field.value_type == 0) {
        bool value = field.raw_value != 0;
        if (ImGui::Checkbox(field.label.c_str(), &value)) {
            field.raw_value = value ? 1u : 0u;
            field.display_value = value ? "Yes" : "No";
        }
    } else if (field.value_type == 1 || field.value_type == 5) {
        int value = static_cast<int32_t>(field.raw_value);
        if (ImGui::InputInt(field.label.c_str(), &value)) {
            field.raw_value = static_cast<uint32_t>(value);
            field.display_value = std::to_string(value);
        }
    } else if (field.value_type == 3) {
        float value = 0.0f;
        std::memcpy(&value, &field.raw_value, sizeof(value));
        if (ImGui::InputFloat(field.label.c_str(), &value, 0.0f, 0.0f,
                              "%.3f")) {
            std::memcpy(&field.raw_value, &value, sizeof(value));
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "%.3f", value);
            field.display_value = buffer;
        }
    } else if (field.value_type == 4 || field.value_type == 6 ||
               field.value_type == 7) {
        std::vector<std::pair<uint32_t, std::string>> options =
            npc_reference_options(field.field_hash);
        std::string preview = field.display_value;
        if (preview.empty()) {
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "0x%08X",
                          field.raw_value);
            preview = buffer;
        }
        if (ImGui::BeginCombo(field.label.c_str(), preview.c_str())) {
            for (const auto& option : options) {
                const bool selected = option.first == field.raw_value;
                if (ImGui::Selectable(option.second.c_str(), selected)) {
                    field.raw_value = option.first;
                    field.display_value = option.second;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::Text("%s: %s", field.label.c_str(),
                    field.display_value.c_str());
    }
    ImGui::PopID();
}

void draw_npc_template_picker() {
    const char* preview = g_new_npc.template_entity == 0
        ? "Select NPC template..." : g_new_npc.template_name.c_str();
    if (!ImGui::BeginCombo("Full model + animations", preview)) return;
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##new_npc_template_filter",
                             "Search NPC templates...",
                             g_new_npc_template_filter,
                             sizeof(g_new_npc_template_filter));
    std::string filter = g_new_npc_template_filter;
    std::transform(filter.begin(), filter.end(), filter.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    std::vector<int> visible;
    visible.reserve(g_global_entity_catalog.size());
    for (int i = 0; i < static_cast<int>(g_global_entity_catalog.size());
         ++i) {
        const auto& entity = g_global_entity_catalog[static_cast<size_t>(i)];
        if (g_global_entity_gameplay.find(entity.entity_hash) ==
            g_global_entity_gameplay.end()) continue;
        std::string text = entity.name + " " + entity.display_name;
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        if (filter.empty() || text.find(filter) != std::string::npos) {
            visible.push_back(i);
        }
    }
    ImGui::Separator();
    ImGui::BeginChild("##new_npc_templates", ImVec2(0.0f, 280.0f));
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const int index = visible[static_cast<size_t>(row)];
            const auto& entity =
                g_global_entity_catalog[static_cast<size_t>(index)];
            const std::string& label = entity.display_name.empty()
                ? entity.name : entity.display_name;
            const bool selected = index == g_new_npc_template_index;
            if (ImGui::Selectable(label.c_str(), selected)) {
                select_new_npc_template(index);
                ImGui::CloseCurrentPopup();
            }
        }
    }
    clipper.End();
    ImGui::EndChild();
    ImGui::EndCombo();
}

void draw_static_prop_model_picker() {
    const char* preview = g_new_static_prop.model_path.empty()
        ? "Select model..." : g_new_static_prop.model_path.c_str();
    if (!ImGui::BeginCombo("Model", preview)) return;
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##new_static_prop_model_filter",
                             "Search models...",
                             g_new_static_prop_model_filter,
                             sizeof(g_new_static_prop_model_filter));
    std::string filter = g_new_static_prop_model_filter;
    std::transform(filter.begin(), filter.end(), filter.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    std::vector<int> visible;
    visible.reserve(S.all_mdl_files.size());
    for (int i = 0; i < static_cast<int>(S.all_mdl_files.size()); ++i) {
        const FlatAssetEntry& model = S.all_mdl_files[size_t(i)];
        std::string searchable = model.name + " " + model.full_path;
        std::transform(searchable.begin(), searchable.end(),
                       searchable.begin(), [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        if (filter.empty() ||
            searchable.find(filter) != std::string::npos) {
            visible.push_back(i);
        }
    }
    ImGui::Separator();
    ImGui::BeginChild("##new_static_prop_models",
                      ImVec2(0.0f, 300.0f));
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd;
             ++row) {
            const int index = visible[size_t(row)];
            const FlatAssetEntry& model = S.all_mdl_files[size_t(index)];
            const std::string label =
                model.name.empty() ? model.full_path : model.name;
            const bool selected = index == g_new_static_prop_model_index;
            ImGui::PushID(index);
            if (ImGui::Selectable(label.c_str(), selected)) {
                g_new_static_prop_model_index = index;
                g_new_static_prop.model_path = model.full_path;
                g_new_static_prop_error.clear();
                ImGui::CloseCurrentPopup();
            }
            if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(model.full_path.c_str());
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }
    }
    clipper.End();
    ImGui::EndChild();
    ImGui::EndCombo();
}

void draw_npc_named_record_combo(const char* label,
                                 uint32_t& selected_record,
                                 std::string& selected_name,
                                 bool faction) {
    const std::vector<Gdb::EntityGameplayOption>& options = faction
        ? g_global_entity_gameplay_options.factions
        : g_global_entity_gameplay_options.combat_profiles;
    const char* preview = selected_name.empty() ? "Inherit template"
                                                 : selected_name.c_str();
    if (ImGui::BeginCombo(label, preview)) {
        const bool inherited = selected_record == 0;
        if (ImGui::Selectable("Inherit template", inherited)) {
            selected_record = 0;
            selected_name.clear();
        }
        if (inherited) ImGui::SetItemDefaultFocus();
        ImGui::Separator();
        for (const auto& option : options) {
            const bool selected = selected_record == option.record_hash;
            if (ImGui::Selectable(option.label.c_str(), selected)) {
                selected_record = option.record_hash;
                selected_name = option.label;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

void draw_create_entity_modal() {
    ImGui::SetNextWindowSize(ImVec2(720.0f, 620.0f),
                             ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Create Entity##modal", nullptr,
                                ImGuiWindowFlags_NoResize)) return;

    int entity_kind = static_cast<int>(g_new_entity_kind);
    const char* entity_kinds[] = {"NPC", "Static prop"};
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::Combo("Entity type", &entity_kind, entity_kinds,
                     static_cast<int>(std::size(entity_kinds)))) {
        g_new_entity_kind = static_cast<NewEntityKind>(entity_kind);
        g_new_npc_error.clear();
        g_new_static_prop_error.clear();
    }

    ImGui::BeginChild("##create_entity_form", ImVec2(0.0f, -44.0f),
                      false);
    if (g_new_entity_kind == NewEntityKind::StaticProp) {
        ImGui::SeparatorText("Identity");
        ImGui::InputTextWithHint("Entity ID", "QPROP_ChildhoodSkip",
                                 &g_new_static_prop.internal_name);

        ImGui::Spacing();
        ImGui::SeparatorText("Appearance");
        draw_static_prop_model_picker();
        ImGui::Spacing();
        ImGui::TextWrapped(
            "A static prop is a lightweight named world object. It receives "
            "only a model and transform: no AI, targeting, action-use, "
            "sale-sign, readable, inventory, or physics behaviour.");
        ImGui::TextDisabled(
            "Quest Blueprint nodes reference the placed instance by name; "
            "the node does not copy the entity's component details.");
        if (!g_new_static_prop_error.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.95f, 0.42f, 0.38f, 1.0f), "%s",
                               g_new_static_prop_error.c_str());
        }
    } else {
        ImGui::SeparatorText("Identity");
        ImGui::InputTextWithHint("NPC ID", "QNPC_MyCharacter",
                                 &g_new_npc.internal_name);
        ImGui::InputText("Display name", &g_new_npc.display_name);

        ImGui::Spacing();
        ImGui::SeparatorText("Appearance and behaviour");
        draw_npc_template_picker();
        if (g_new_npc.template_entity != 0) {
        ImGui::TextDisabled(
            "The complete model, eyes, hair, rig, animations, and unlisted "
            "GDB values are inherited from this template.");
        const std::string model_parts_label = "Model parts (" +
            std::to_string(g_new_npc.model_hashes.size()) + ')';
        if (ImGui::TreeNode(model_parts_label.c_str())) {
            for (uint32_t hash : g_new_npc.model_hashes) {
                const FlatAssetEntry* model =
                    FindGlobalModelAssetByPathHash(hash);
                if (model) ImGui::BulletText("%s", model->full_path.c_str());
                else ImGui::BulletText("Unresolved model 0x%08X", hash);
            }
            ImGui::TreePop();
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Gameplay");
        draw_npc_named_record_combo("Faction / allegiance",
                                    g_new_npc.faction_record,
                                    g_new_npc.faction_name, true);
        draw_npc_named_record_combo("Combat profile",
                                    g_new_npc.combat_profile_record,
                                    g_new_npc.combat_profile_name, false);

        if (!g_new_npc.core_fields.empty()) {
            ImGui::Spacing();
            ImGui::SeparatorText("Core stats");
            for (auto& field : g_new_npc.core_fields) {
                draw_npc_value_field(field);
            }
        }
        if (!g_new_npc.combat_fields.empty()) {
            ImGui::Spacing();
            ImGui::SeparatorText("Combat");
            for (auto& field : g_new_npc.combat_fields) {
                draw_npc_value_field(field);
            }
        }
        }
        if (!g_new_npc_error.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.95f, 0.42f, 0.38f, 1.0f), "%s",
                               g_new_npc_error.c_str());
        }
    }
    ImGui::EndChild();

    const bool saving_static =
        g_new_entity_kind == NewEntityKind::StaticProp;
    const bool can_save = saving_static
        ? StaticPropAuthoring::IsValidInternalName(
              g_new_static_prop.internal_name) &&
              !g_new_static_prop.model_path.empty()
        : g_new_npc.template_entity != 0 &&
              g_new_npc.creature_component != 0;
    ImGui::BeginDisabled(!can_save);
    if (ImGui::Button("Save Entity", ImVec2(140.0f, 0.0f))) {
        uint32_t saved_entity_hash = 0;
        std::string result;
        std::string error;
        bool saved_ok = false;
        if (saving_static) {
            StaticPropAuthoring::CatalogEntry saved;
            saved_ok = StaticPropAuthoring::Save(
                S.root_dir, g_new_static_prop, saved, result, error);
            saved_entity_hash = saved.entity_hash;
        } else {
            saved_ok = NpcAuthoring::Save(
                S.root_dir, g_new_npc, saved_entity_hash, result, error);
        }
        if (saved_ok) {
            if (!saving_static) {
                TextBank::Invalidate();
                TextBank::LoadForRoot(S.root_dir);
            }
            Level::BuildGlobalEntityCatalog();
            int saved_index = -1;
            for (int i = 0;
                 i < static_cast<int>(g_global_entity_catalog.size()); ++i) {
                if (g_global_entity_catalog[static_cast<size_t>(i)]
                        .entity_hash == saved_entity_hash) {
                    saved_index = i;
                    break;
                }
            }
            if (saved_index >= 0) {
                const auto& saved =
                    g_global_entity_catalog[static_cast<size_t>(saved_index)];
                const std::string label = saved.display_name.empty()
                    ? saved.name : saved.display_name;
                ContentTabs::OpenEntity(saved_index, label);
                load_entity_preview(saved_index);
            }
            OutputLog::success("entity save: " + result);
            show_completion_box(result);
            g_new_npc = NpcAuthoring::Definition{};
            g_new_npc_template_index = -1;
            g_new_npc_error.clear();
            g_new_static_prop = StaticPropAuthoring::Definition{};
            g_new_static_prop_model_index = -1;
            g_new_static_prop_error.clear();
            ImGui::CloseCurrentPopup();
        } else {
            (saving_static ? g_new_static_prop_error : g_new_npc_error) =
                std::move(error);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f))) {
        g_new_npc = NpcAuthoring::Definition{};
        g_new_npc_template_index = -1;
        g_new_npc_error.clear();
        g_new_static_prop = StaticPropAuthoring::Definition{};
        g_new_static_prop_model_index = -1;
        g_new_static_prop_error.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

}

bool select_quest_script_by_query(const std::string& query) {
    std::string needle = query;
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });

    for (size_t i = 0; i < S.all_quest_files.size(); ++i) {
        const FlatAssetEntry& entry = S.all_quest_files[i];
        std::string haystack = entry.name + " " + entry.full_path;
        std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (haystack.find(needle) == std::string::npos) continue;

        S.quest_filter = query;
        select_quest_script(i);
        return true;
    }
    return false;
}

void open_tree_bnk_drill_from_entry(const std::string& parent_bnk_path,
                                    int file_index,
                                    const std::string& entry_name) {
    if (parent_bnk_path.empty() || file_index < 0) return;

    const std::filesystem::path tmpdir =
        std::filesystem::temp_directory_path() / "f2_tree_bnk_drill";
    std::error_code ec;
    std::filesystem::create_directories(tmpdir, ec);

    const std::string temp_name =
        std::to_string(std::hash<std::string>{}(
            parent_bnk_path + "::" + entry_name + "::" +
            std::to_string(file_index))) + ".bnk";
    const std::filesystem::path tmp_bnk = tmpdir / temp_name;

    try {
        extract_one(parent_bnk_path, file_index, tmp_bnk.string());
        drill_open_bnk(g_tree_drill, tmp_bnk.string(), true);
        const std::string title =
            std::filesystem::path(entry_name).filename().string();
        if (!title.empty()) {
            g_tree_drill.title = title;
        }
    } catch (const std::exception& e) {
        OutputLog::error(std::string("File tree BNK open failed: ") +
                         e.what());
    } catch (...) {
        OutputLog::error("File tree BNK open failed.");
    }
}

#ifdef _WIN32
void draw_left_panel(ID3D11Device* device) {
#else
void draw_left_panel() {
#endif

    apply_entity_preview_completions();
    ImGui::BeginChild("left_panel", ImVec2(0, 0), true);

    static int s_active_tab = 1;
    if (g_open_create_npc_requested) s_active_tab = 9;

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
    const ImU32 kGoldLabel = IM_COL32(255, 215, 0, 255);
    if (tab_button("Levels", s_active_tab == 6, kGoldLabel)) s_active_tab = 6;
    ImGui::SameLine(0, 2);
    const ImU32 kLuaLabel = IM_COL32(80, 220, 120, 255);
    if (tab_button("Lua Scripts", s_active_tab == 7, kLuaLabel)) s_active_tab = 7;

    const ImU32 kPurpleLabel = IM_COL32(200, 130, 255, 255);
    if (tab_button("Models",   s_active_tab == 2, kPurpleLabel)) s_active_tab = 2;
    ImGui::SameLine(0, 2);
    if (tab_button("Textures", s_active_tab == 3, kPurpleLabel)) s_active_tab = 3;
    ImGui::SameLine(0, 2);
    if (tab_button("Audio",    s_active_tab == 4, kPurpleLabel)) s_active_tab = 4;
    ImGui::SameLine(0, 2);
    if (tab_button("Animations", s_active_tab == 5, kPurpleLabel)) s_active_tab = 5;

    const ImU32 kItemsLabel = IM_COL32(255, 175, 90, 255);
    if (tab_button("Items", s_active_tab == 8, kItemsLabel)) s_active_tab = 8;
    ImGui::SameLine(0, 2);
    const ImU32 kEntitiesLabel = IM_COL32(110, 220, 165, 255);
    if (tab_button("Entities", s_active_tab == 9, kEntitiesLabel)) s_active_tab = 9;
    ImGui::SameLine(0, 2);
    const ImU32 kQuestsLabel = IM_COL32(100, 200, 255, 255);
    if (tab_button("Quests", s_active_tab == 10, kQuestsLabel)) s_active_tab = 10;

    if (DetailsPanel::Active()) {
        ImGui::SameLine(0, 2);
        const ImU32 kDetailsLabel = IM_COL32(255, 230, 120, 255);
        if (tab_button("Details", s_active_tab == 11, kDetailsLabel)) {
            s_active_tab = 11;
        }
    } else if (s_active_tab == 11) {
        s_active_tab = 1;
    }

    ImGui::Separator();

    auto draw_flat_asset_tab = [](const char* ,
                                  std::vector<FlatAssetEntry>& entries,
                                  std::string& filter,
                                  const char* child_id,
                                  int kind,
                                  float footer_h = 0.0f,
                                  bool dedup_by_name_size = true,
                                  const char* drag_type = nullptr) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint(("##" + std::string(child_id) + "_filter").c_str(),
                                 "Filter", &filter);

        std::string flow = filter;
        std::transform(flow.begin(), flow.end(), flow.begin(), ::tolower);

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

                if (drag_type && !e.full_path.empty() &&
                    ImGui::BeginDragDropSource(
                        ImGuiDragDropFlags_SourceAllowNullID)) {
                    ImGui::SetDragDropPayload(drag_type,
                                              e.full_path.c_str(),
                                              e.full_path.size());
                    ImGui::TextUnformatted(e.name.c_str());
                    ImGui::EndDragDropSource();
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
                                               false);
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
                                               true);
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
                                        S.show_gdb_render = false;
                                        g_pending_mdl_full_path = it.name;
                                        g_pending_mdl_load = true;
                                        g_pending_mdl_index = (int)j;
                                    } else if (ln.size() >= 4 &&
                                               ln.rfind(".tex") == ln.size() - 4) {
                                        S.show_gdb_render = false;
                                        g_pending_tex_load = true;
                                        g_pending_tex_index = (int)j;
                                    } else if (ln.size() >= 4 &&
                                               ln.rfind(".gdb") == ln.size() - 4) {
                                        open_gdb_viewer_for_bnk_entry(
                                            g_bnk_drill.bnk_path,
                                            it.index,
                                            it.name);
                                    } else if (ln.size() >= 4 &&
                                               ln.rfind(".wav") == ln.size() - 4) {

                                        S.show_gdb_render = false;
                                        open_audio_player_for_selected((int)j);
                                    }
                                    break;
                                }
                            }
                        } else if (g_bnk_drill.kind == DrillKind::Adb) {
                            S.viewing_adb = true;
                            S.viewing_lua = false;
                            S.show_gdb_render = false;
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
                            select_lua_script((size_t)idx);
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
            drill_step_anim(g_tree_drill, ImGui::GetIO().DeltaTime);

            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const float page_w = avail.x;
            const float page_h = avail.y;
            const float kVisEps = 0.0001f;
            const bool a_visible = g_tree_drill.anim_t < 1.0f - kVisEps;
            const bool b_visible = g_tree_drill.anim_t > 0.0f + kVisEps;

            ImGui::BeginChild("file_tree", ImVec2(0, 0), false);
            ImGui::BeginChild("##tree_drill_container",
                              ImVec2(page_w, page_h),
                              false,
                              ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::SetScrollX(g_tree_drill.anim_t * page_w);

            ImGui::BeginChild("##tree_page_a", ImVec2(page_w, page_h), false);
            if (a_visible) {
                if (g_tree_last_root_dir != S.root_dir && !S.bnk_paths.empty()
                    && !g_tree_building.load() && !g_tree_built.load())
                {
                    start_tree_build_for_root(S.root_dir, S.bnk_paths);
                }

                TreeNode& tree_render_root = g_tree_root;

                if (g_tree_building.load()) {
                    ImVec2 inner_avail = ImGui::GetContentRegionAvail();
                    float elapsed =
                        (float)ImGui::GetTime() - g_tree_build_start_time;

                    float dot_cycle = fmodf(elapsed * 2.0f, 4.0f);
                    int dot_count = (int)dot_cycle;
                    std::string dots(dot_count, '.');
                    std::string loading_text = "Loading file tree" + dots;

                    ImVec2 text_size =
                        ImGui::CalcTextSize(loading_text.c_str());
                    ImVec2 pos((inner_avail.x - text_size.x) * 0.5f,
                               (inner_avail.y - text_size.y) * 0.5f);
                    if (pos.x < 0) pos.x = 0;
                    if (pos.y < 0) pos.y = 0;
                    ImGui::SetCursorPos(pos);
                    ImGui::TextUnformatted(loading_text.c_str());

                    if (elapsed > 10.0f) {
                        ImVec2 warning_size =
                            ImGui::CalcTextSize("(this may take some time)");
                        ImVec2 warning_pos(
                            (inner_avail.x - warning_size.x) * 0.5f,
                            pos.y + text_size.y + 10.0f);
                        if (warning_pos.x < 0) warning_pos.x = 0;
                        ImGui::SetCursorPos(warning_pos);
                        ImGui::TextUnformatted("(this may take some time)");
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
            }
            ImGui::EndChild();

            ImGui::SameLine(0.0f, 0.0f);

            ImGui::BeginChild("##tree_page_b", ImVec2(page_w, page_h), false);
            if (b_visible) {
                const bool b_can_click =
                    (g_tree_drill.target_t == 1.0f) &&
                    drill_settled(g_tree_drill);

                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                      ImVec4(0.30f, 0.45f, 0.65f, 0.40f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                      ImVec4(0.40f, 0.60f, 0.90f, 0.55f));
                if (ImGui::Button(ICON_FA_ARROW_LEFT "##tree_drill_back")) {
                    drill_back(g_tree_drill);
                }
                ImGui::PopStyleColor(3);
                ImGui::SameLine();
                ImGui::TextUnformatted(g_tree_drill.title.c_str());

                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##tree_drill_filter", "Filter",
                                         &g_tree_drill.filter);

                std::string flt = g_tree_drill.filter;
                std::transform(flt.begin(), flt.end(), flt.begin(),
                               ::tolower);
                std::vector<int> vis;
                vis.reserve(g_tree_drill.items.size());
                for (size_t i = 0; i < g_tree_drill.items.size(); ++i) {
                    if (flt.empty()) {
                        vis.push_back((int)i);
                    } else {
                        std::string n =
                            std::filesystem::path(g_tree_drill.items[i].name)
                                .filename().string();
                        std::transform(n.begin(), n.end(), n.begin(),
                                       ::tolower);
                        if (n.find(flt) != std::string::npos) {
                            vis.push_back((int)i);
                        }
                    }
                }

                ImGui::BeginChild("##tree_drill_list", ImVec2(0, 0), false);
                ImGuiListClipper drill_clipper;
                drill_clipper.Begin((int)vis.size());
                while (drill_clipper.Step()) {
                    for (int r = drill_clipper.DisplayStart;
                         r < drill_clipper.DisplayEnd; ++r) {
                        int idx = vis[(size_t)r];
                        const BNKItemUI& it =
                            g_tree_drill.items[(size_t)idx];
                        ImGui::PushID(r);

                        std::string label =
                            std::filesystem::path(it.name).filename().string();
                        const bool selected =
                            (S.selected_bnk == g_tree_drill.bnk_path &&
                             S.selected_file_index >= 0 &&
                             S.selected_file_index < (int)S.files.size() &&
                             S.files[(size_t)S.selected_file_index].index ==
                                 it.index);
                        if (ImGui::Selectable(
                                label.c_str(), selected,
                                ImGuiSelectableFlags_SpanAllColumns) &&
                            b_can_click) {
                            if (S.selected_bnk != g_tree_drill.bnk_path) {
                                S.viewing_adb = false;
                                S.viewing_lua = false;
                                S.global_search.clear();
                                S.selected_nested_bnk.clear();
                                S.selected_nested_index = -1;
                                pick_bnk(g_tree_drill.bnk_path);
                            }
                            if (g_tree_drill.from_nested) {
                                S.selected_nested_temp_path =
                                    g_tree_drill.bnk_path;
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
                                               ln.rfind(".tex") ==
                                                   ln.size() - 4) {
                                        g_pending_tex_load = true;
                                        g_pending_tex_index = (int)j;
                                    } else if (ln.size() >= 4 &&
                                               ln.rfind(".wav") ==
                                                   ln.size() - 4) {
                                        open_audio_player_for_selected((int)j);
                                    }
                                    break;
                                }
                            }
                        }

                        file_hex_context_menu(g_tree_drill.bnk_path,
                                              it.index,
                                              g_tree_drill.from_nested,
                                              it.name);
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
            ImGui::EndChild();
        }

        if (s_active_tab == 2) {

            const float footer_h = ImGui::GetFrameHeightWithSpacing();
            draw_flat_asset_tab("Models", S.all_mdl_files, S.mdl_filter,
                                "models_list", 0, footer_h,
                                true, "F2_MODEL");

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
                    "No MDLs indexed yet - open a Fable 2 root "
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
                                "textures_list", 1, footer_h);

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
                    "No textures indexed yet - open a Fable 2 root "
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
                                "audio_list", 2, footer_h);

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
                    "No audio indexed yet - open a Fable 2 root "
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

            const uint32_t want_bones = g_mp.bone_count;
            size_t authored_count = 0;
            const bool can_filter_by_authored =
                g_mp.has_model && S.current_mdl_path_hash != 0 &&
                !S.anim_clips.empty();
            if (can_filter_by_authored) {
                const uint64_t authored_sig =
                    Anim::model_animation_binding_revision() ^
                    (uint64_t(S.current_mdl_path_hash) << 32) ^
                    uint64_t(S.anim_clips.size());
                if (S.anim_authored_signature != authored_sig ||
                    S.anim_authored_cache.size() != S.anim_clips.size()) {
                    authored_count =
                        Anim::build_model_animation_cache_for_hash(
                            S.current_mdl_path_hash, S.anim_clips.size(),
                            S.anim_authored_cache);
                    S.anim_authored_signature = authored_sig;
                } else {
                    authored_count = 0;
                    for (uint8_t v : S.anim_authored_cache) {
                        if (v) ++authored_count;
                    }
                }
            }
            const bool has_authored_filter =
                can_filter_by_authored && authored_count > 0;
            if (has_authored_filter) {
                ImGui::Checkbox("Authored model",
                                &S.anim_authored_only);
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Show clips referenced by GDB animation records for "
                        "this exact model path hash.");
                }
            } else if (g_mp.has_model && S.current_mdl_path_hash != 0) {
                ImGui::TextDisabled("No authored animation set for model");
            }
            const bool can_filter_by_skeleton =
                Anim::global_data_file().is_open() &&
                g_mp.has_model && want_bones > 0;
            if (can_filter_by_skeleton) {
                ImGui::Checkbox("Compatible rig",
                                &S.anim_compatible_only);
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Show clips whose AnimBank track map matches this "
                        "model's bone names. Falls back to the old %u-bone "
                        "track-count gate when no track map is available.",
                        want_bones);
                }
            }
            const bool filter_by_authored =
                S.anim_authored_only && has_authored_filter;
            const bool filter_by_bones =
                !filter_by_authored &&
                S.anim_compatible_only && can_filter_by_skeleton;
            if (filter_by_bones) {
                const uint64_t sig = Anim::rig_compatibility_signature(
                    S.mdl_info, want_bones, S.anim_clips,
                    Anim::global_data_file().is_open());
                if (S.anim_compat_signature != sig ||
                    S.anim_compat_cache.size() != S.anim_clips.size()) {
                    Anim::build_rig_compatibility_cache(
                        S.mdl_info, want_bones, S.anim_clips,
                        S.anim_compat_cache, S.anim_compat_matches,
                        S.anim_compat_named_tracks);
                    S.anim_compat_signature = sig;
                }
            }

            std::vector<int> vis;
            vis.reserve(S.anim_clips.size());
            std::string flow = S.anim_filter;
            std::transform(flow.begin(), flow.end(), flow.begin(), ::tolower);
            for (size_t i = 0; i < S.anim_clips.size(); ++i) {
                if (filter_by_authored) {
                    if (i >= S.anim_authored_cache.size() ||
                        !S.anim_authored_cache[i]) {
                        continue;
                    }
                } else if (filter_by_bones) {
                    if (i >= S.anim_compat_cache.size() ||
                        !S.anim_compat_cache[i]) {
                        continue;
                    }
                }
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
            {
                ImGui::TextDisabled("%d / %zu%s",
                                    (int)vis.size(),
                                    S.anim_clips.size(),
                                    filter_by_authored
                                        ? " authored model"
                                        : (filter_by_bones ? " rig match" : ""));
                if (filter_by_bones) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%u bones)", want_bones);
                } else if (filter_by_authored) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%zu exact)", authored_count);
                }
                if (S.dev_mode) {
                    ImGui::Separator();
                }
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
                        const int clip_idx = vis[(size_t)row];
                        const auto& c = S.anim_clips[(size_t)clip_idx];
                        ImGui::PushID(row);
                        bool selected =
                            (S.anim_selected_clip == clip_idx);
                        char label[64];
                        float dur_s = Anim::clip_duration_seconds(c);
                        std::snprintf(label, sizeof(label), "%s  (%.2fs)",
                                      c.name.c_str(), dur_s);
                        if (ImGui::Selectable(label, selected,
                                              ImGuiSelectableFlags_SpanAllColumns)) {
                            S.anim_selected_clip = clip_idx;
                            Anim::global_player().play(
                                &S.anim_clips[(size_t)clip_idx],
                                Anim::global_player().is_loop());
                        }
                        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(c.name.c_str());
                            ImGui::Text("Duration: %.3f s  (%.0f fps)",
                                        dur_s, c.fps);
                            if (Anim::global_data_file().is_open()) {
                                auto h = Anim::global_data_file().parse_clip_header(c);
                                if (h.ok) {
                                    ImGui::Text("Tracks: %u / model bones: %u%s",
                                                h.bone_count, want_bones,
                                                h.bone_count == want_bones
                                                    ? "  track-count match"
                                                    : "");
                                }
                            }
                            if (c.track_map) {
                                ImGui::Text("Track map: %zu / %zu model-name matches",
                                            (clip_idx >= 0 &&
                                             (size_t)clip_idx < S.anim_compat_matches.size())
                                                ? (size_t)S.anim_compat_matches[(size_t)clip_idx]
                                                : 0u,
                                            (clip_idx >= 0 &&
                                             (size_t)clip_idx < S.anim_compat_named_tracks.size())
                                                ? (size_t)S.anim_compat_named_tracks[(size_t)clip_idx]
                                                : 0u);
                            }
                            ImGui::Text("Events: %zu", c.events.size());
                            if (S.dev_mode) {
                                ImGui::Text("offset=0x%08X frames=%u bytes=%u",
                                            c.data_offset, c.toc_frame_count,
                                            c.data_size_bytes);
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

        if (s_active_tab == 8) {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##item_filter", "Filter",
                                     &S.item_filter);
            std::string flow = S.item_filter;
            std::transform(flow.begin(), flow.end(), flow.begin(),
                           ::tolower);
            if (g_item_details.empty()) {
                ImGui::TextDisabled("No items indexed yet.");
                ImGui::TextDisabled(
                    "Open (load) a level to populate the item list.");
            } else {
                std::vector<int> vis;
                vis.reserve(g_item_details.size());
                for (int i = 0; i < (int)g_item_details.size(); ++i) {


                    if (g_item_details[i].is_money) continue;
                    if (flow.empty()) { vis.push_back(i); continue; }
                    std::string low = g_item_details[i].display_name;
                    std::transform(low.begin(), low.end(), low.begin(),
                                   ::tolower);
                    if (low.find(flow) != std::string::npos) {
                        vis.push_back(i);
                    }
                }
                if (S.dev_mode) {
                    ImGui::TextDisabled("%zu items (%zu shown)",
                                        g_item_details.size(),
                                        vis.size());
                }
                ImGui::BeginChild("items_list", ImVec2(0, 0), false);
                ImGuiListClipper clipper;
                clipper.Begin((int)vis.size());
                while (clipper.Step()) {
                    for (int r = clipper.DisplayStart;
                         r < clipper.DisplayEnd; ++r) {
                        const int idx = vis[r];
                        const auto& it = g_item_details[idx];
                        ImGui::PushID(idx);
                        const bool sel = (S.selected_item == idx);
                        const char* row_name =
                            it.display_name.empty() ? it.label.c_str()
                                                    : it.display_name
                                                          .c_str();
                        if (ImGui::Selectable(row_name, sel)) {
                            extern std::atomic<bool> g_item_icon_dirty;
                            ContentTabs::OpenItem(idx, row_name);
                            S.selected_item = idx;
                            S.show_item_details = true;
                            g_item_icon_dirty = true;
#ifdef _WIN32
                            const FlatAssetEntry* hit = nullptr;
                            if (!it.model_path.empty()) {
                                hit = find_model_by_path_left(
                                    it.model_path);
                            }
                            if (!hit && it.model_path_hash) {
                                hit = find_model_by_path_hash_left(
                                    it.model_path_hash);
                            }
                            if (hit) {
                                extern std::atomic<bool>
                                    g_pending_mdl_is_item;
                                load_flat_asset_entry(*hit, 0);
                                g_pending_mdl_is_item = true;
                            }
#endif
                        }
                        ImGui::PopID();
                    }
                }
                clipper.End();
                ImGui::EndChild();
            }
        }

        if (s_active_tab == 9) {
            const bool create_entity_clicked =
                ImGui::Button("Create Entity", ImVec2(-1.0f, 0.0f));
            if (create_entity_clicked || g_open_create_npc_requested) {
                if (g_open_create_npc_requested) {
                    g_new_entity_kind = NewEntityKind::Npc;
                }
                g_open_create_npc_requested = false;
                g_new_npc = NpcAuthoring::Definition{};
                g_new_npc_template_index = -1;
                g_new_npc_template_filter[0] = 0;
                g_new_npc_error.clear();
                g_new_static_prop = StaticPropAuthoring::Definition{};
                g_new_static_prop_model_index = -1;
                g_new_static_prop_model_filter[0] = 0;
                g_new_static_prop_error.clear();
                if (S.selected_entity >= 0 &&
                    static_cast<std::size_t>(S.selected_entity) <
                        g_global_entity_catalog.size() &&
                    g_global_entity_catalog[
                        static_cast<std::size_t>(S.selected_entity)].kind ==
                        Gdb::EntityCatalogKind::Creature) {
                    select_new_npc_template(S.selected_entity);
                }
                ImGui::OpenPopup("Create Entity##modal");
            }
            draw_create_entity_modal();

            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##entity_filter", "Filter entities",
                                     &S.entity_filter);
            std::string filter = S.entity_filter;
            std::transform(filter.begin(), filter.end(), filter.begin(),
                           [](unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });

            if (g_global_entity_catalog.empty()) {
                ImGui::TextDisabled("No entities indexed yet.");
                ImGui::TextDisabled("Open a Fable 2 game root to index them.");
            } else {
                static uint64_t cached_catalog_revision =
                    std::numeric_limits<uint64_t>::max();
                static std::string cached_filter;
                static std::vector<std::string> searchable_entities;
                static std::vector<int> visible;
                bool catalog_changed = false;
                if (cached_catalog_revision !=
                    g_global_entity_catalog_revision) {
                    searchable_entities.clear();
                    searchable_entities.reserve(g_global_entity_catalog.size());
                    for (const auto& entity : g_global_entity_catalog) {
                        std::string searchable = entity.name + " " +
                            entity.display_name;
                        const auto gameplay =
                            g_global_entity_gameplay.find(entity.entity_hash);
                        if (gameplay != g_global_entity_gameplay.end()) {
                            searchable += " " + gameplay->second.faction_name;
                            searchable += " " +
                                gameplay->second.combat_profile_name;
                        }
                        std::transform(
                            searchable.begin(), searchable.end(),
                            searchable.begin(), [](unsigned char c) {
                                return static_cast<char>(std::tolower(c));
                            });
                        searchable_entities.push_back(std::move(searchable));
                    }
                    cached_catalog_revision =
                        g_global_entity_catalog_revision;
                    catalog_changed = true;
                }
                if (catalog_changed || cached_filter != filter) {
                    visible.clear();
                    visible.reserve(searchable_entities.size());
                    for (int i = 0;
                         i < static_cast<int>(searchable_entities.size()); ++i) {
                        if (filter.empty() ||
                            searchable_entities[static_cast<size_t>(i)].find(
                                filter) != std::string::npos) {
                            visible.push_back(i);
                        }
                    }
                    cached_filter = filter;
                }
                if (S.dev_mode) {
                    ImGui::TextDisabled("%zu / %zu entities", visible.size(),
                                        g_global_entity_catalog.size());
                }
                ImGui::BeginChild("entities_list", ImVec2(0, 0), false);
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(visible.size()));
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart;
                         row < clipper.DisplayEnd; ++row) {
                        const int index = visible[static_cast<std::size_t>(row)];
                        const auto& entity = g_global_entity_catalog[index];
                        const std::string& label =
                            entity.display_name.empty()
                                ? entity.name : entity.display_name;
                        ImGui::PushID(index);
                        if (ImGui::Selectable(label.c_str(),
                                              S.selected_entity == index,
                                              ImGuiSelectableFlags_SpanAllColumns)) {
                            ContentTabs::OpenEntity(index, label);
                            load_entity_preview(index);
                        }
                        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(label.c_str());
                            if (S.dev_mode && label != entity.name) {
                                ImGui::TextDisabled("%s", entity.name.c_str());
                            }
                            ImGui::Text("Model parts: %zu",
                                        entity.model_hashes.size());
                            ImGui::EndTooltip();
                        }
                        ImGui::PopID();
                    }
                }
                clipper.End();
                ImGui::EndChild();
            }
        }

        if (s_active_tab == 10) {
            static std::string new_quest_id = "QO000_NewQuest";
            static std::string new_quest_error;
            if (ImGui::Button("Create Quest", ImVec2(-1.0f, 0.0f))) {
                new_quest_error.clear();
                ImGui::OpenPopup("Create Quest##modal");
            }
            ImGui::SetNextWindowSize(ImVec2(430.0f, 0.0f),
                                     ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal("Create Quest##modal", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                if (ImGui::IsWindowAppearing()) {
                    ImGui::SetKeyboardFocusHere();
                }
                ImGui::SetNextItemWidth(390.0f);
                ImGui::InputTextWithHint("##new_quest_id", "Quest ID",
                                         &new_quest_id);
                if (!new_quest_error.empty()) {
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 390.0f);
                    ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                                       "%s", new_quest_error.c_str());
                    ImGui::PopTextWrapPos();
                }
                if (ImGui::Button("Create")) {
                    if (shipped_quest_id_exists(new_quest_id)) {
                        new_quest_error =
                            "A shipped quest already uses this ID.";
                    } else {
                        ++S.lua_preview_request;
                        if (QuestUI::CreateNewBlueprintQuest(
                                new_quest_id, new_quest_error)) {
                            const std::string title =
                                "Custom quest: " + new_quest_id;
                            ContentTabs::OpenCustomQuest(
                                new_quest_id, title);
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
                            S.lua_preview_title = title;
                            S.lua_preview_content =
                                QuestUI::ActiveAuthoredLua();
                            S.lua_preview_loading = false;
                            S.lua_preview_is_quest = true;
                            S.quest_preview_select_nodes = true;
                            S.show_lua_render = true;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##quest_filter", "Filter quests",
                                     &S.quest_filter);
            std::string flow = S.quest_filter;
            std::transform(flow.begin(), flow.end(), flow.begin(),
                           [](unsigned char c) {
                               return char(std::tolower(c));
                           });

            std::vector<int> vis;
            vis.reserve(S.all_quest_files.size());
            for (size_t i = 0; i < S.all_quest_files.size(); ++i) {
                const FlatAssetEntry& e = S.all_quest_files[i];
                std::string haystack = e.name + " " + e.full_path;
                std::transform(haystack.begin(), haystack.end(),
                               haystack.begin(),
                               [](unsigned char c) {
                                   return char(std::tolower(c));
                               });
                if (flow.empty() || haystack.find(flow) != std::string::npos) {
                    vis.push_back((int)i);
                }
            }

            if (S.dev_mode) {
                ImGui::TextDisabled("%d / %zu quest scripts",
                                    (int)vis.size(),
                                    S.all_quest_files.size());
                ImGui::Separator();
            }

            ImGui::BeginChild("quests_list", ImVec2(0, 0), false);
            static std::string s_delete_quest_id;
            const std::vector<std::string> authored_quests =
                QuestUI::AuthoredQuestIds();
            bool showed_authored_heading = false;
            for (const std::string& quest_id : authored_quests) {
                std::string lower_id = quest_id;
                std::transform(lower_id.begin(), lower_id.end(),
                               lower_id.begin(), [](unsigned char c) {
                                   return char(std::tolower(c));
                               });
                if (!flow.empty() &&
                    lower_id.find(flow) == std::string::npos) continue;
                if (!showed_authored_heading) {
                    ImGui::TextDisabled("CUSTOM QUESTS");
                    showed_authored_heading = true;
                }
                const bool selected = QuestUI::IsAuthoredQuestActive() &&
                                      QuestUI::ActiveAuthoredQuestId() ==
                                          quest_id;
                if (ImGui::Selectable(quest_id.c_str(), selected,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    show_authored_quest(quest_id);
                }
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Delete Quest")) {
                        s_delete_quest_id = quest_id;
                    }
                    ImGui::EndPopup();
                }
            }
            if (!s_delete_quest_id.empty() &&
                !ImGui::IsPopupOpen("Delete custom quest?")) {
                ImGui::OpenPopup("Delete custom quest?");
            }
            if (ImGui::BeginPopupModal("Delete custom quest?", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Delete %s? This removes its blueprint file "
                            "for good.",
                            s_delete_quest_id.c_str());
                if (ImGui::Button("Delete", ImVec2(120, 0))) {
                    const std::string id = s_delete_quest_id;
                    s_delete_quest_id.clear();
                    ContentTabs::CloseCustomQuest(id);
                    std::string derr;
                    if (QuestUI::DeleteAuthoredQuest(id, derr)) {
                        OutputLog::success("quest deleted: " + id);
                    } else {
                        OutputLog::error("quest delete: " + derr);
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    s_delete_quest_id.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            if (showed_authored_heading && !vis.empty()) {
                ImGui::Separator();
            }
            if (S.all_quest_files.empty()) {
                if (tree_build_in_progress()) {
                    ImGui::TextDisabled("Indexing quest scripts...");
                } else {
                    ImGui::TextDisabled("No embedded quest scripts found.");
                    ImGui::TextDisabled(
                        "Open a Fable 2 root containing gamescripts.bnk or gamescripts_r.bnk.");
                }
            } else {
                ImGuiListClipper clipper;
                clipper.Begin((int)vis.size());
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart;
                         row < clipper.DisplayEnd; ++row) {
                        const int idx = vis[(size_t)row];
                        const FlatAssetEntry& e =
                            S.all_quest_files[(size_t)idx];
                        const bool selected = S.selected_quest == idx;

                        ImGui::PushID(idx);
                        if (ImGui::Selectable(
                                e.name.c_str(), selected,
                                ImGuiSelectableFlags_SpanAllColumns)) {
                            select_quest_script((size_t)idx);
                        }
                        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(e.full_path.c_str());
                            ImGui::Text("Source: %s",
                                std::filesystem::path(e.bnk_path)
                                    .filename().string().c_str());
                            ImGui::TextUnformatted(
                                quest_source_has_debug_symbols(e.bnk_path)
                                    ? "Symbol-rich script"
                                    : "Stripped runtime script");
                            ImGui::Text("Size: %u bytes", e.size);
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
            struct LvlMap {
                const char* path;
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

            auto norm = [](std::string s) -> std::string {
                std::transform(s.begin(), s.end(), s.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                std::replace(s.begin(), s.end(), '/', '\\');
                return s;
            };

            std::unordered_map<std::string, std::string> path_to_name;
            for (const auto& g : kLevelGroups) {
                for (const auto& m : g.entries) {
                    path_to_name[norm(m.path)] = m.name;
                }
            }

            {
                const bool new_level_busy =
                    Level::IsAsyncLoadInProgress() ||
                    Level::IsExportInProgress() ||
                    tree_build_in_progress();
                if (new_level_busy) ImGui::BeginDisabled();
                if (ImGui::Button("+ New Level", ImVec2(-1, 0))) {
                    if (!level_edit_click_guard("Level creation")) {
                        NewLevelDialog::Open();
                    }
                }
                if (new_level_busy) ImGui::EndDisabled();
            }
            NewLevelDialog::Draw();

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
            static FlatAssetEntry s_delete_level_entry{};
            static std::string s_delete_level_name;
            if (S.all_level_files.empty()) {
                ImGui::TextDisabled("No .engine_level files indexed yet.");
                ImGui::TextDisabled("Open a Fable 2 root to populate the list.");
            } else {
                auto draw_entry = [&](const FlatAssetEntry& e,
                                      const std::string& friendly)
                {
                    ImGui::PushID(&e);
                    const bool level_busy =
                        Level::IsAsyncLoadInProgress() ||
                        Level::IsExportInProgress();
                    if (level_busy) ImGui::BeginDisabled();
                    if (ImGui::Selectable(friendly.c_str(), false,
                                          ImGuiSelectableFlags_SpanAllColumns))
                    {
                        if (!level_edit_click_guard("Level loading")) {
                            S.show_item_details = false;
                            S.selected_item = -1;
                            S.show_entity_details = false;
                            S.selected_entity = -1;
                            ContentTabs::OpenLevel(e, friendly);
                        }
                    }
                    if (level_busy) ImGui::EndDisabled();
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
                                extern std::string          g_pending_heightmap_view_kind;
                                g_pending_heightmap_view_rgba = std::move(rgba);
                                g_pending_heightmap_view_w    = hw;
                                g_pending_heightmap_view_h    = hh;
                                g_pending_heightmap_view_name = friendly;
                                g_pending_heightmap_view_kind = "Heightmap";
                                g_pending_heightmap_view_load = true;
                            }
                        }
                        if (S.dev_mode && ImGui::MenuItem("Open PF99")) {
                            std::vector<uint8_t> rgba;
                            int pw = 0, ph = 0;
                            if (Level::RenderPf99ToRGBA(e, rgba, pw, ph)) {
                                extern std::atomic<bool>    g_pending_heightmap_view_load;
                                extern std::vector<uint8_t> g_pending_heightmap_view_rgba;
                                extern int                  g_pending_heightmap_view_w;
                                extern int                  g_pending_heightmap_view_h;
                                extern std::string          g_pending_heightmap_view_name;
                                extern std::string          g_pending_heightmap_view_kind;
                                g_pending_heightmap_view_rgba = std::move(rgba);
                                g_pending_heightmap_view_w    = pw;
                                g_pending_heightmap_view_h    = ph;
                                g_pending_heightmap_view_name = friendly + "_pf99";
                                g_pending_heightmap_view_kind = "PF99";
                                g_pending_heightmap_view_load = true;
                            }
                        }
                        if (ImGui::BeginMenu("Export")) {
                            if (ImGui::MenuItem("GLB")) {
                                Level::ExportAsync(e,
                                    Level::ExportFormat::GLB);
                            }
                            if (ImGui::MenuItem("FBX")) {
                                Level::ExportAsync(e,
                                    Level::ExportFormat::FBX);
                            }
                            ImGui::EndMenu();
                        }
                        if (Level::Creation::IsCustomLooseLevel(e)) {
                            ImGui::Separator();
                            if (ImGui::MenuItem("Delete Level")) {
                                s_delete_level_entry = e;
                                s_delete_level_name = friendly;
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

                std::unordered_map<std::string, const FlatAssetEntry*> by_path;
                for (const auto& e : S.all_level_files) {
                    by_path[norm(e.full_path)] = &e;
                }

                std::unordered_set<const FlatAssetEntry*> placed;
                std::unordered_set<std::string> placed_paths;

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
                    std::vector<std::pair<const FlatAssetEntry*, std::string>> rows;
                    for (const auto& m : g.entries) {
                        auto it = by_path.find(norm(m.path));
                        if (it == by_path.end()) continue;
                        if (!matches_filter(m.name, it->second->full_path)) continue;
                        rows.push_back({it->second, std::string(m.name)});
                        placed.insert(it->second);
                        placed_paths.insert(norm(it->second->full_path));
                    }
                    if (rows.empty()) continue;

                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImVec4(1.0f, 0.84f, 0.0f, 1.0f));
                    ImGui::TextUnformatted(g.heading);
                    ImGui::PopStyleColor();
                    ImGui::Indent(8.0f);
                    for (const auto& [e, friendly] : rows) {
                        draw_entry(*e, friendly);
                    }
                    ImGui::Unindent(8.0f);
                    ImGui::Spacing();
                }

                auto is_loose_source = [](const FlatAssetEntry& e) {
                    std::string src = e.bnk_path;
                    std::transform(src.begin(), src.end(), src.begin(),
                        [](unsigned char c){ return (char)std::tolower(c); });
                    return src.size() < 4 ||
                           src.compare(src.size() - 4, 4, ".bnk") != 0;
                };
                std::vector<std::pair<const FlatAssetEntry*, std::string>> custom;
                for (const auto& e : S.all_level_files) {
                    if (placed.count(&e)) continue;
                    if (!is_loose_source(e)) continue;
                    if (!placed_paths.insert(norm(e.full_path)).second) continue;
                    std::filesystem::path p = e.full_path;

                    
                    auto region = p.parent_path().parent_path()
                                      .filename().string();
                    std::string label = region.empty()
                        ? e.name
                        : Level::Creation::GetCustomLevelDisplayName(
                              Level::Creation::ResolveGameDataDir(), region);
                    if (!matches_filter(label, e.full_path)) continue;
                    custom.push_back({&e, label});
                    placed.insert(&e);
                }
                if (!custom.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImVec4(1.0f, 0.84f, 0.0f, 1.0f));
                    ImGui::TextUnformatted("Custom Levels");
                    ImGui::PopStyleColor();
                    ImGui::Indent(8.0f);
                    for (const auto& [e, label] : custom) {
                        draw_entry(*e, label);
                    }
                    ImGui::Unindent(8.0f);
                    ImGui::Spacing();
                }

                std::vector<std::pair<const FlatAssetEntry*, std::string>> leftover;
                for (const auto& e : S.all_level_files) {
                    if (placed.count(&e)) continue;
                    if (placed_paths.count(norm(e.full_path))) continue;
                    std::filesystem::path p = e.full_path;
                    auto parent = p.parent_path().filename().string();
                    std::string label = parent.empty()
                        ? e.name : parent + " - " + e.name;
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

            if (!s_delete_level_name.empty() &&
                !ImGui::IsPopupOpen("Delete custom level?")) {
                ImGui::OpenPopup("Delete custom level?");
            }
            if (ImGui::BeginPopupModal("Delete custom level?", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Delete %s? This permanently removes its "
                            "files from data\\worlds\\albion.",
                            s_delete_level_name.c_str());
                if (ImGui::Button("Delete", ImVec2(120, 0))) {
                    const FlatAssetEntry doomed = s_delete_level_entry;
                    s_delete_level_name.clear();
                    
                    std::string off_msg;
                    LevelEdit::SetEnabled(false, off_msg);
                    LevelEdit::ClearEdits();
                    ContentTabs::CloseLevelByPath(doomed.full_path);
                    std::string derr;
                    if (Level::Creation::DeleteCustomLevel(doomed,
                                                           derr)) {
                        refresh_loose_file_index();
                    } else {
                        OutputLog::error("delete level: " + derr);
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    s_delete_level_name.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::EndChild();
        }

        if (s_active_tab == 7) {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##lua_scripts_filter", "Filter",
                                     &S.lua_filter);
            std::string flow = S.lua_filter;
            std::transform(flow.begin(), flow.end(), flow.begin(),
                           [](unsigned char c){ return std::tolower(c); });

            std::vector<int> vis;
            vis.reserve(S.lua_files.size());
            for (size_t i = 0; i < S.lua_files.size(); ++i) {
                const std::string label = lua_script_list_label(S.lua_files[i]);
                if (flow.empty()) {
                    vis.push_back((int)i);
                    continue;
                }

                std::string haystack = label + " " + S.lua_files[i].path;
                std::transform(haystack.begin(), haystack.end(),
                               haystack.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                if (haystack.find(flow) != std::string::npos) {
                    vis.push_back((int)i);
                }
            }

            if (S.dev_mode) {
                ImGui::TextDisabled("%d / %zu scripts",
                                    (int)vis.size(), S.lua_files.size());
                ImGui::Separator();
            }

            ImGui::BeginChild("lua_scripts_list", ImVec2(0, 0), false);
            if (S.lua_files.empty()) {
                ImGui::TextDisabled("No Lua scripts indexed yet.");
                ImGui::TextDisabled("Open a Fable 2 root to populate the list.");
            } else {
                ImGuiListClipper clipper;
                clipper.Begin((int)vis.size());
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart;
                         row < clipper.DisplayEnd; ++row) {
                        const int idx = vis[(size_t)row];
                        const LuaFileUI& e = S.lua_files[(size_t)idx];
                        const std::string label = lua_script_list_label(e);
                        const bool selected =
                            S.viewing_lua && S.selected_file_index == idx;

                        ImGui::PushID(idx);
                        if (ImGui::Selectable(label.c_str(), selected,
                                              ImGuiSelectableFlags_SpanAllColumns)) {
                            select_lua_script((size_t)idx);
                        }
                        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(e.path.c_str());
                            ImGui::Text("Size: %u bytes", e.size);
                            ImGui::EndTooltip();
                        }
                        ImGui::PopID();
                    }
                }
                clipper.End();
            }
            ImGui::EndChild();
        }

        if (s_active_tab == 11) {
            DetailsPanel::Draw();
        }

    ImGui::EndChild();
}

void RequestQuestInjection() { inject_active_authored_quest(); }
bool QuestInjectionBusy() { return g_quest_injection_busy.load(); }
