#include "ContentTabs.h"

#include "UI_Panels.h"
#include "Quest/QuestNodeView.h"
#include "IconsFontAwesome6.h"
#include "../BNKCore.cpp"

#include "ModelPreview.h"
#include "OutputLog.h"
#include "Quest/QuestNodeView.h"
#include "../Level/Creation/LandscapeAuthoring.h"
#include "../Level/Editing/LevelEdit.h"
#include "../Level/Terrain/TerrainEdit.h"
#include "../Level/Terrain/TerrainPaint.h"
#include "../Level/Core/LevelLoader.h"
#include "../Utilities/State.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

extern ModelPreview g_mp;
extern bool g_mp_initialized;

namespace ContentTabs {
namespace {

struct Tab {
    std::uint64_t id = 0;
    Kind kind = Kind::None;
    std::string title;
    std::string key;

    MDLInfo model_info;
    std::vector<MDLMeshGeom> model_meshes;
    std::string model_path;
    std::uint32_t model_path_hash = 0;
    float cam_yaw = 3.14159265f;
    float cam_pitch = 0.2f;
    float cam_dist = 3.0f;
    int item_index = -1;
    int entity_index = -1;

    FlatAssetEntry level_entry{};
    std::shared_ptr<ModelPreview> level_preview;
    FlyCam level_camera{};

    std::string lua_content;
    bool lua_loading = false;
    std::string authored_quest_id;
};

struct LuaCompletion {
    std::string key;
    std::string content;
};

std::vector<Tab> g_tabs;
std::uint64_t g_active_id = 0;
std::uint64_t g_select_id = 0;
std::uint64_t g_next_id = 1;
std::uint64_t g_level_edit_tab_id = 0;
bool g_keep_selection = false;
std::mutex g_completion_mutex;
std::vector<LuaCompletion> g_completions;

void release_preview_meshes(ModelPreview& preview) {
#ifdef _WIN32
    for (MPPerMesh& mesh : preview.meshes) {
        if (mesh.vb) mesh.vb->Release();
        if (mesh.ib) mesh.ib->Release();
        auto release_srv = [&](ID3D11ShaderResourceView*& srv) {
            if (srv && srv != preview.default_srv) srv->Release();
            srv = nullptr;
        };
        release_srv(mesh.srv_diffuse);
        release_srv(mesh.srv_normal);
        release_srv(mesh.srv_specular);
        release_srv(mesh.srv_metallic);
        release_srv(mesh.srv_extra);
        mesh.vb = nullptr;
        mesh.ib = nullptr;
    }
#endif
    preview.meshes.clear();
}

#ifdef _WIN32
void release_level_theme_textures(ModelPreview& preview) {
    auto release_srv = [](ID3D11ShaderResourceView*& srv) {
        if (srv) srv->Release();
        srv = nullptr;
    };
    for (ID3D11ShaderResourceView*& srv : preview.cloud_density_srv) {
        release_srv(srv);
    }
    release_srv(preview.sky_overlay_srv);
    release_srv(preview.sky_sun_disc_srv);
    release_srv(preview.sky_moon_srv);
    release_srv(preview.sky_moon_glare_srv);
    release_srv(preview.sky_sun_beams_srv);
    release_srv(preview.sky_sun_glare_srv);
    for (auto& entry : preview.fx_tex_srv) {
        if (entry.second) entry.second->Release();
    }
    preview.fx_tex_srv.clear();
}

void detach_level_theme_textures(ModelPreview& preview) {
    for (ID3D11ShaderResourceView*& srv : preview.cloud_density_srv) {
        srv = nullptr;
    }
    preview.sky_overlay_srv = nullptr;
    preview.sky_sun_disc_srv = nullptr;
    preview.sky_moon_srv = nullptr;
    preview.sky_moon_glare_srv = nullptr;
    preview.sky_sun_beams_srv = nullptr;
    preview.sky_sun_glare_srv = nullptr;
}
#endif

void release_level_preview(Tab& tab) {
    if (!tab.level_preview) return;
    release_preview_meshes(*tab.level_preview);
#ifdef _WIN32
    release_level_theme_textures(*tab.level_preview);
#endif
    tab.level_preview->fx_system.clear();
    tab.level_preview.reset();
}

void capture_level_preview(Tab& tab) {
    if (tab.kind != Kind::Level || tab.level_preview ||
        !S.terrain_mode || !g_mp.has_model || !g_mp.no_tilt ||
        Level::IsAsyncLoadInProgress()) {
        return;
    }
    auto snapshot = std::make_shared<ModelPreview>(g_mp);
    snapshot->meshes = std::move(g_mp.meshes);
    snapshot->fx_system = std::move(g_mp.fx_system);
    snapshot->fx_tex_srv = std::move(g_mp.fx_tex_srv);
    tab.level_camera = g_flycam;
    tab.level_preview = std::move(snapshot);

#ifdef _WIN32
    detach_level_theme_textures(g_mp);
#endif
    g_mp.range_edit_xforms.clear();
    g_mp.has_model = false;
    g_mp.no_tilt = false;
}

bool restore_level_preview(Tab& tab) {
    if (!tab.level_preview) return false;
    release_preview_meshes(g_mp);
#ifdef _WIN32
    release_level_theme_textures(g_mp);
    ID3D11Texture2D* current_color = g_mp.color;
    ID3D11RenderTargetView* current_rtv = g_mp.rtv;
    ID3D11ShaderResourceView* current_srv = g_mp.srv;
    ID3D11Texture2D* current_depth = g_mp.depth;
    ID3D11DepthStencilView* current_dsv = g_mp.dsv;
#endif
    const int current_width = g_mp.width;
    const int current_height = g_mp.height;
    std::swap(g_mp, *tab.level_preview);
#ifdef _WIN32
    g_mp.color = current_color;
    g_mp.rtv = current_rtv;
    g_mp.srv = current_srv;
    g_mp.depth = current_depth;
    g_mp.dsv = current_dsv;
#endif
    g_mp.width = current_width;
    g_mp.height = current_height;
    g_flycam = tab.level_camera;
    tab.level_preview.reset();
    return true;
}

std::string normalized(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

std::string model_title(const std::string& path) {
    std::string title = std::filesystem::path(path).filename().string();
    return title.empty() ? std::string("Model") : title;
}

std::size_t find_id(std::uint64_t id) {
    for (std::size_t i = 0; i < g_tabs.size(); ++i) {
        if (g_tabs[i].id == id) return i;
    }
    return g_tabs.size();
}

std::size_t find_key(Kind kind, const std::string& key) {
    const std::string wanted = normalized(key);
    for (std::size_t i = 0; i < g_tabs.size(); ++i) {
        if (g_tabs[i].kind == kind && normalized(g_tabs[i].key) == wanted) {
            return i;
        }
    }
    return g_tabs.size();
}

Tab* active_tab() {
    const std::size_t index = find_id(g_active_id);
    return index < g_tabs.size() ? &g_tabs[index] : nullptr;
}

bool level_edit_session_active() {
    return LevelEdit::Enabled() || LevelEdit::Dirty() ||
           LevelEdit::Saving();
}

void sync_level_edit_tab() {
    if (!level_edit_session_active()) {
        g_level_edit_tab_id = 0;
        return;
    }
    if (find_id(g_level_edit_tab_id) < g_tabs.size()) return;

    const Tab* tab = active_tab();
    if (tab && tab->kind == Kind::Level) {
        g_level_edit_tab_id = tab->id;
        return;
    }
    for (const Tab& candidate : g_tabs) {
        if (candidate.kind == Kind::Level && candidate.level_preview) {
            g_level_edit_tab_id = candidate.id;
            return;
        }
    }
}

bool level_load_blocks_tab_switch() {
    if (Level::IsAsyncLoadInProgress()) {
        OutputLog::warn(
            "Wait for the current level load to finish before switching tabs.");
        return true;
    }
    return false;
}

void reset_model_specific_ui() {
    S.anim_selected_clip = -1;
    S.anim_authored_signature = 0;
    S.anim_authored_cache.clear();
    S.anim_compat_signature = 0;
    S.anim_compat_cache.clear();
    S.anim_compat_matches.clear();
    S.anim_compat_named_tracks.clear();
    S.bone_rot_deltas.clear();
    S.bone_anim_rot_absolute.clear();
    S.bone_anim_rot_present.clear();
    S.bone_anim_trans_delta.clear();
    S.bone_anim_trans_present.clear();
    S.bone_anim_pose_active = false;
    S.selected_bone = -1;
    S.bone_rotate_mode = false;
}

void save_active_state() {
    Tab* tab = active_tab();
    if (!tab) return;
    if (tab->kind == Kind::Level) {
        if (level_edit_session_active()) g_level_edit_tab_id = tab->id;
        capture_level_preview(*tab);
        return;
    }
    if (tab->kind == Kind::Model || tab->kind == Kind::Item ||
        tab->kind == Kind::Entity) {
        tab->cam_yaw = S.cam_yaw;
        tab->cam_pitch = S.cam_pitch;
        tab->cam_dist = S.cam_dist;
        return;
    }
    if (tab->kind == Kind::Lua || tab->kind == Kind::Quest ||
        tab->kind == Kind::CustomQuest) {
        tab->lua_content = S.lua_preview_content;
        tab->lua_loading = S.lua_preview_loading.load();
    }
}

void show_lua(Tab& tab) {
    ++S.lua_preview_request;
    S.show_gdb_render = false;
    S.show_lua_render = true;
    S.terrain_mode = false;
    S.item_model_active = false;
    S.selected_item = -1;
    S.show_item_details = false;
    S.entity_model_active = false;
    S.selected_entity = -1;
    S.show_entity_details = false;
    S.lua_preview_selected = -1;
    S.lua_preview_title = tab.title;
    S.lua_preview_content = tab.lua_content;
    S.lua_preview_loading = tab.lua_loading;
    S.lua_preview_is_quest =
        tab.kind == Kind::Quest || tab.kind == Kind::CustomQuest;
    S.quest_preview_select_nodes = S.lua_preview_is_quest;

    if (tab.kind == Kind::CustomQuest) {
        if (QuestUI::OpenAuthoredQuest(tab.authored_quest_id)) {
            S.lua_preview_content = QuestUI::ActiveAuthoredLua();
            S.lua_preview_loading = false;
            tab.lua_content = S.lua_preview_content;
            tab.lua_loading = false;
        }
    } else if (tab.kind == Kind::Quest) {
        QuestUI::Clear();
        QuestUI::RefreshReferenceCatalog();
        if (!tab.lua_content.empty()) {
            QuestUI::SetQuestSource(tab.title, tab.lua_content);
        }
    }
}

bool activate(std::size_t index) {
    if (index >= g_tabs.size()) return false;
    Tab& target = g_tabs[index];
    if (target.id == g_active_id) return true;

    sync_level_edit_tab();
    if (level_load_blocks_tab_switch()) {
        g_select_id = g_active_id;
        g_keep_selection = true;
        return false;
    }
    if (target.kind == Kind::Level && level_edit_session_active() &&
        target.id != g_level_edit_tab_id) {
        OutputLog::warn(
            "Save any changes and turn off level editing, or restore defaults, "
            "before opening another level.");
        g_select_id = g_active_id;
        g_keep_selection = true;
        return false;
    }

    save_active_state();
    g_active_id = target.id;
    S.content_tabs_visible = true;

    if (target.kind == Kind::Model || target.kind == Kind::Item ||
        target.kind == Kind::Entity) {
        if ((target.kind == Kind::Item || target.kind == Kind::Entity) &&
            target.model_meshes.empty()) {
            ++S.lua_preview_request;
            S.show_gdb_render = false;
            S.show_lua_render = false;
            S.item_model_active = target.kind == Kind::Item;
            S.selected_item = S.item_model_active ? target.item_index : -1;
            S.show_item_details = S.selected_item >= 0;
            S.entity_model_active = target.kind == Kind::Entity;
            S.selected_entity = S.entity_model_active
                ? target.entity_index : -1;
            S.show_entity_details = S.selected_entity >= 0;
            return true;
        }
        ++S.lua_preview_request;
        S.mdl_info = target.model_info;
        S.mdl_meshes = target.model_meshes;
        S.mdl_info_ok = !target.model_meshes.empty();
        S.current_mdl_path = target.model_path;
        S.current_mdl_path_hash = target.model_path_hash;
        S.item_model_active = target.kind == Kind::Item;
        S.selected_item = target.item_index;
        S.show_item_details = target.kind == Kind::Item &&
                              target.item_index >= 0;
        S.entity_model_active = target.kind == Kind::Entity;
        S.selected_entity = target.entity_index;
        S.show_entity_details = target.kind == Kind::Entity &&
                                target.entity_index >= 0;
        S.cam_yaw = target.cam_yaw;
        S.cam_pitch = target.cam_pitch;
        S.cam_dist = target.cam_dist;
        S.show_gdb_render = false;
        S.show_lua_render = false;
        S.terrain_mode = false;
        g_mp.no_tilt = false;
        reset_model_specific_ui();
        g_mp.has_model = false;
        if (!target.model_meshes.empty()) {
            S.pending_model_tab_capture = false;
            S.pending_preview_build = true;
        }
        return true;
    }

    if (target.kind == Kind::Level) {
        ++S.lua_preview_request;
        S.show_gdb_render = false;
        S.show_lua_render = false;
        S.item_model_active = false;
        S.selected_item = -1;
        S.show_item_details = false;
        S.entity_model_active = false;
        S.selected_entity = -1;
        S.show_entity_details = false;
        if (restore_level_preview(target)) {
            S.terrain_mode = true;
            g_mp.has_model = true;
            g_mp.no_tilt = true;
            return true;
        }



        for (Tab& tab : g_tabs) {
            if (tab.id != target.id && tab.kind == Kind::Level) {
                release_level_preview(tab);
            }
        }
        S.terrain_mode = false;
        Level::OpenAsync(target.level_entry);
        return true;
    }

    show_lua(target);
    return true;
}

void apply_lua_completions() {
    std::vector<LuaCompletion> completions;
    {
        std::lock_guard<std::mutex> lock(g_completion_mutex);
        completions.swap(g_completions);
    }
    for (LuaCompletion& completion : completions) {
        for (Tab& tab : g_tabs) {
            if ((tab.kind != Kind::Lua && tab.kind != Kind::Quest) ||
                normalized(tab.key) != normalized(completion.key)) {
                continue;
            }
            tab.lua_content = std::move(completion.content);
            tab.lua_loading = false;
            if (tab.id == g_active_id) {
                S.lua_preview_content = tab.lua_content;
                S.lua_preview_loading = false;
                if (tab.kind == Kind::Quest) {
                    QuestUI::SetQuestSource(tab.title, tab.lua_content);
                }
            }
            break;
        }
    }
}

void close_id(std::uint64_t id) {
    sync_level_edit_tab();
    const std::size_t index = find_id(id);
    if (index >= g_tabs.size()) return;
    const bool was_active = g_active_id == id;
    if (g_tabs[index].kind == Kind::Level &&
        ((level_edit_session_active() && id == g_level_edit_tab_id) ||
         (was_active && Level::IsAsyncLoadInProgress()))) {
        g_select_id = g_active_id;
        g_keep_selection = true;
        return;
    }
    if (was_active) save_active_state();
    release_level_preview(g_tabs[index]);
    g_tabs.erase(g_tabs.begin() + static_cast<std::ptrdiff_t>(index));
    if (!was_active) return;

    if (g_tabs.empty()) {
        g_active_id = 0;
        g_select_id = 0;
        S.content_tabs_visible = false;
        S.pending_model_tab_capture = false;
        S.pending_preview_build = false;
        S.show_lua_render = false;
        S.lua_preview_content.clear();
        S.lua_preview_title.clear();
        S.mdl_info_ok = false;
        S.mdl_meshes.clear();
        S.current_mdl_path.clear();
        S.current_mdl_path_hash = 0;
        S.item_model_active = false;
        S.selected_item = -1;
        S.show_item_details = false;
        S.entity_model_active = false;
        S.selected_entity = -1;
        S.show_entity_details = false;
        S.terrain_mode = false;
        reset_model_specific_ui();
        MP_Release(g_mp);
        g_mp.has_model = false;
        g_mp_initialized = false;
        return;
    }

    std::size_t next = std::min(index, g_tabs.size() - 1);
    if (level_edit_session_active()) {
        const std::size_t edit_index = find_id(g_level_edit_tab_id);
        if (edit_index < g_tabs.size()) next = edit_index;
    }
    g_active_id = 0;
    g_select_id = g_tabs[next].id;
    activate(next);
}

const char* kind_tooltip(Kind kind) {
    switch (kind) {
        case Kind::Model: return "Model";
        case Kind::Item: return "Item";
        case Kind::Entity: return "Entity";
        case Kind::Level: return "Level";
        case Kind::Lua: return "Lua script";
        case Kind::Quest: return "Quest script";
        case Kind::CustomQuest: return "Custom quest";
        default: return "Content";
    }
}

}

void CaptureCurrentModel() {
    if (!S.mdl_info_ok || S.mdl_meshes.empty() ||
        S.current_mdl_path.empty()) {
        return;
    }

    Kind kind = S.entity_model_active ? Kind::Entity
              : S.item_model_active ? Kind::Item
                                    : Kind::Model;
    std::size_t index = g_tabs.size();
    if (kind == Kind::Item) {
        Tab* active = active_tab();
        if (active && active->kind == Kind::Item &&
            active->item_index == S.selected_item) {
            index = find_id(active->id);
        }
    }
    if (kind == Kind::Entity) {
        Tab* active = active_tab();
        if (active && active->kind == Kind::Entity &&
            active->entity_index == S.selected_entity) {
            index = find_id(active->id);
        }
    }
    if (index >= g_tabs.size() && kind == Kind::Model) {
        index = find_key(kind, S.current_mdl_path);
    }

    Tab tab;
    if (index < g_tabs.size()) tab = g_tabs[index];
    if (!tab.id) tab.id = g_next_id++;
    tab.kind = kind;
    if (tab.title.empty()) tab.title = model_title(S.current_mdl_path);
    if (kind == Kind::Model) tab.key = S.current_mdl_path;
    tab.model_path = S.current_mdl_path;
    tab.model_path_hash = S.current_mdl_path_hash;
    tab.model_info = S.mdl_info;
    tab.model_meshes = S.mdl_meshes;
    tab.item_index = kind == Kind::Item ? S.selected_item : -1;
    tab.entity_index = kind == Kind::Entity ? S.selected_entity : -1;
    tab.cam_yaw = S.cam_yaw;
    tab.cam_pitch = S.cam_pitch;
    tab.cam_dist = S.cam_dist;

    if (index < g_tabs.size()) {
        g_tabs[index] = std::move(tab);
    } else {
        g_tabs.push_back(std::move(tab));
        index = g_tabs.size() - 1;
    }
    g_active_id = g_tabs[index].id;
    g_select_id = g_active_id;
    S.content_tabs_visible = true;
    S.show_gdb_render = false;
    S.show_lua_render = false;
}

void OpenItem(int item_index, const std::string& title) {
    if (level_load_blocks_tab_switch()) return;
    save_active_state();
    const std::string key = "item:" + std::to_string(item_index);
    std::size_t index = find_key(Kind::Item, key);
    if (index >= g_tabs.size()) {
        Tab tab;
        tab.id = g_next_id++;
        tab.kind = Kind::Item;
        tab.title = title.empty() ? std::string("Item") : title;
        tab.key = key;
        tab.item_index = item_index;
        g_tabs.push_back(std::move(tab));
        index = g_tabs.size() - 1;
    }
    g_active_id = g_tabs[index].id;
    g_select_id = g_active_id;
    S.content_tabs_visible = true;
    S.show_gdb_render = false;
    S.show_lua_render = false;
    S.item_model_active = true;
    S.selected_item = item_index;
    S.show_item_details = item_index >= 0;
    S.entity_model_active = false;
    S.selected_entity = -1;
    S.show_entity_details = false;
    ++S.lua_preview_request;
}

void OpenEntity(int entity_index, const std::string& title) {
    if (level_load_blocks_tab_switch()) return;
    save_active_state();
    const std::string key = "entity:" + std::to_string(entity_index);
    std::size_t index = find_key(Kind::Entity, key);
    if (index >= g_tabs.size()) {
        Tab tab;
        tab.id = g_next_id++;
        tab.kind = Kind::Entity;
        tab.title = title.empty() ? std::string("Entity") : title;
        tab.key = key;
        tab.entity_index = entity_index;
        g_tabs.push_back(std::move(tab));
        index = g_tabs.size() - 1;
    }
    g_active_id = g_tabs[index].id;
    g_select_id = g_active_id;
    S.content_tabs_visible = true;
    S.show_gdb_render = false;
    S.show_lua_render = false;
    S.entity_model_active = true;
    S.selected_entity = entity_index;
    S.show_entity_details = entity_index >= 0;
    S.item_model_active = false;
    S.selected_item = -1;
    S.show_item_details = false;
    ++S.lua_preview_request;
}

void OpenLevel(const FlatAssetEntry& entry, const std::string& title) {
    const std::string key = entry.full_path.empty() ? entry.name
                                                     : entry.full_path;
    std::size_t index = find_key(Kind::Level, key);
    sync_level_edit_tab();
    if (level_load_blocks_tab_switch()) return;
    if (level_edit_session_active() &&
        (index >= g_tabs.size() ||
         g_tabs[index].id != g_level_edit_tab_id)) {
        OutputLog::warn(
            "Save any changes and turn off level editing, or restore defaults, "
            "before opening another level.");
        return;
    }
    save_active_state();
    if (index >= g_tabs.size()) {
        Tab tab;
        tab.id = g_next_id++;
        tab.kind = Kind::Level;
        tab.title = title.empty() ? model_title(key) : title;
        tab.key = key;
        tab.level_entry = entry;
        g_tabs.push_back(std::move(tab));
        index = g_tabs.size() - 1;
    }
    if (g_active_id == g_tabs[index].id && S.terrain_mode) {
        restore_level_preview(g_tabs[index]);
        g_select_id = g_active_id;
        return;
    }
    g_active_id = 0;
    g_select_id = g_tabs[index].id;
    activate(index);
}

void OpenLua(const std::string& key, const std::string& title,
             bool is_quest) {
    if (level_load_blocks_tab_switch()) return;
    save_active_state();
    const Kind kind = is_quest ? Kind::Quest : Kind::Lua;
    std::size_t index = find_key(kind, key);
    if (index >= g_tabs.size()) {
        Tab tab;
        tab.id = g_next_id++;
        tab.kind = kind;
        tab.title = title.empty() ? model_title(key) : title;
        tab.key = key;
        tab.lua_loading = true;
        g_tabs.push_back(std::move(tab));
        index = g_tabs.size() - 1;
    } else {
        g_tabs[index].lua_loading = true;
    }
    g_active_id = g_tabs[index].id;
    g_select_id = g_active_id;
    S.content_tabs_visible = true;
}

void OpenCustomQuest(const std::string& quest_id,
                     const std::string& title) {
    if (level_load_blocks_tab_switch()) return;
    save_active_state();
    const std::string key = "custom-quest:" + quest_id;
    std::size_t index = find_key(Kind::CustomQuest, key);
    if (index >= g_tabs.size()) {
        Tab tab;
        tab.id = g_next_id++;
        tab.kind = Kind::CustomQuest;
        tab.title = title;
        tab.key = key;
        tab.authored_quest_id = quest_id;
        tab.lua_loading = false;
        g_tabs.push_back(std::move(tab));
        index = g_tabs.size() - 1;
    }
    g_active_id = g_tabs[index].id;
    g_select_id = g_active_id;
    S.content_tabs_visible = true;
}

void CloseCustomQuest(const std::string& quest_id) {
    for (const Tab& tab : g_tabs) {
        if (tab.kind == Kind::CustomQuest &&
            tab.authored_quest_id == quest_id) {
            close_id(tab.id);
            return;
        }
    }
}

void CloseLevelByPath(const std::string& full_path) {
    for (const Tab& tab : g_tabs) {
        if (tab.kind == Kind::Level &&
            tab.level_entry.full_path == full_path) {
            close_id(tab.id);
            return;
        }
    }
}

void FixLooseEntryIndices(const std::string& loose_dir) {
    for (Tab& tab : g_tabs) {
        if (tab.kind != Kind::Level) continue;
        if (tab.level_entry.bnk_path != loose_dir) continue;
        std::string key = tab.level_entry.full_path;
        std::replace(key.begin(), key.end(), '\\', '/');
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) {
                           return char(std::tolower(c));
                       });
        const int idx = BnkCache::find_index(loose_dir, key);
        if (idx >= 0) tab.level_entry.file_index = idx;
    }
}

void CompleteLua(const std::string& key, const std::string& content) {
    std::lock_guard<std::mutex> lock(g_completion_mutex);
    g_completions.push_back({key, content});
}

bool HasTabs() {
    return !g_tabs.empty();
}

Kind ActiveKind() {
    const Tab* tab = active_tab();
    return tab ? tab->kind : Kind::None;
}

bool ActiveHasModel() {
    const Tab* tab = active_tab();
    return tab && (tab->kind == Kind::Model || tab->kind == Kind::Item ||
                   tab->kind == Kind::Entity) &&
           !tab->model_meshes.empty();
}

const FlatAssetEntry* ActiveLevelEntry() {
    const Tab* tab = active_tab();
    return tab && tab->kind == Kind::Level ? &tab->level_entry : nullptr;
}

void DrawTabBar() {
    apply_lua_completions();
    if (g_tabs.empty()) return;
    sync_level_edit_tab();

    std::uint64_t close = 0;
    const ImVec2 strip_pos = ImGui::GetCursorScreenPos();
    const float strip_width = ImGui::GetContentRegionAvail().x;
    const ImGuiTabBarFlags bar_flags =
        ImGuiTabBarFlags_Reorderable |
        ImGuiTabBarFlags_AutoSelectNewTabs |
        ImGuiTabBarFlags_FittingPolicyScroll;
    if (ImGui::BeginTabBar("##content_tabs", bar_flags)) {
        for (std::size_t i = 0; i < g_tabs.size(); ++i) {
            Tab& tab = g_tabs[i];
            bool open = true;
            const bool protected_level =
                tab.kind == Kind::Level &&
                ((level_edit_session_active() &&
                  tab.id == g_level_edit_tab_id) ||
                 (tab.id == g_active_id &&
                  Level::IsAsyncLoadInProgress()));
            bool* open_ptr = protected_level ? nullptr : &open;
            const ImGuiTabItemFlags flags =
                tab.id == g_select_id ? ImGuiTabItemFlags_SetSelected
                                      : ImGuiTabItemFlags_None;
            const std::string label = tab.title + "##content_tab_" +
                                      std::to_string(tab.id);
            const bool visible =
                ImGui::BeginTabItem(label.c_str(), open_ptr, flags);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\n%s", kind_tooltip(tab.kind),
                                  tab.key.c_str());
            }
            if (visible) {
                if (g_active_id != tab.id) activate(i);
                ImGui::EndTabItem();
            }
            if (!open) close = tab.id;
        }

        ImGui::EndTabBar();
    }

    
    
    
    const FlatAssetEntry* active_level = ActiveLevelEntry();
    const bool level_save_active =
        active_level && Level::Creation::IsCustomLooseLevel(*active_level);
    const bool quest_active =
        !level_save_active && QuestUI::IsAuthoredQuestActive();
    auto save_level_working_copy = [&] {
        std::string msg;
        if (LevelEdit::SaveWorkingCopy(msg)) {
            OutputLog::success("level save: " + msg);
        } else {
            OutputLog::error("level save: " + msg);
            return;
        }
        if (active_level && TerrainEdit::IsDirty()) {
            std::string terr;
            if (Level::Creation::SaveSculptedHeights(*active_level,
                                                     terr)) {
                OutputLog::success(
                    "level save: sculpted terrain written to the level's "
                    ".ghf");
            } else {
                OutputLog::error("level save: terrain: " + terr);
            }
        }
        if (TerrainPaint::Dirty()) {
            std::string perr;
            if (TerrainPaint::SaveSidecar(perr)) {
                OutputLog::success("level save: terrain paint saved");
            } else {
                OutputLog::error("level save: paint: " + perr);
            }
        }
    };
    if (quest_active || level_save_active) {
        const ImVec2 after_bar = ImGui::GetCursorScreenPos();
        const float button_w =
            ImGui::CalcTextSize(ICON_FA_FLOPPY_DISK).x +
            ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetCursorScreenPos(
            ImVec2(strip_pos.x + strip_width - button_w, strip_pos.y));
        const bool busy = quest_active && QuestInjectionBusy();
        if (busy) ImGui::BeginDisabled();
        if (ImGui::Button(ICON_FA_FLOPPY_DISK "##save_quest")) {
            if (quest_active) {
                ImGui::OpenPopup("Save custom quest?##modal");
            } else {
                save_level_working_copy();
            }
        }
        if (busy) ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                busy ? "Quest save is in progress..."
                : quest_active
                    ? "Save quest to the game scripts (Ctrl+S)"
                    : "Save this level so it can be worked on later "
                      "(Ctrl+S)");
        }
        ImGui::SetCursorScreenPos(after_bar);
    }
    if ((quest_active || level_save_active) &&
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S,
                        ImGuiInputFlags_RouteGlobal)) {
        if (level_save_active) {
            save_level_working_copy();
        } else if (!QuestInjectionBusy()) {
            ImGui::OpenPopup("Save custom quest?##modal");
        }
    }
    if (ImGui::BeginPopupModal("Save custom quest?##modal", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Save %s to the game scripts?",
                    QuestUI::ActiveAuthoredQuestId().c_str());
        if (ImGui::Button("Save Quest")) {
            ImGui::CloseCurrentPopup();
            RequestQuestInjection();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (g_keep_selection) {
        g_keep_selection = false;
    } else if (g_select_id == g_active_id) {
        g_select_id = 0;
    }
    if (close) close_id(close);
}

void CloseActive() {
    if (g_active_id) close_id(g_active_id);
}

void Clear() {
    for (Tab& tab : g_tabs) release_level_preview(tab);
    g_tabs.clear();
    g_active_id = 0;
    g_select_id = 0;
    g_level_edit_tab_id = 0;
    S.content_tabs_visible = false;
    S.pending_model_tab_capture = false;
    std::lock_guard<std::mutex> lock(g_completion_mutex);
    g_completions.clear();
}

}
