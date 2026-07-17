#include "RenderPanel.h"
#include "../../Utilities/State.h"
#include "../ModelPreview.h"
#include "../EntityModelResolver.h"
#include "../ContentTabs.h"
#include "../../textures/export/TextureExport.h"
#include "../../Level/Terrain/TerrainTextureRegistry.h"
#include "../../Level/Terrain/EhfLodThumbnails.h"
#include "../../Level/Terrain/TerrainEdit.h"
#include "../../Level/Terrain/TerrainPaint.h"
#include "../../Level/Editing/LevelEdit.h"
#include "../../Level/Core/LevelLoader.h"
#include "../../Level/Database/TextBank.h"
#include "../../textures/TexParser.h"
#include "../../Utilities/DebugTrace.h"
#include "../LevelGizmo.h"
#include "../../animations/AnimBank.h"
#include "../../animations/AnimDataFile.h"
#include "../../animations/AnimPlayer.h"
#include "../../animations/AnimRigMap.h"
#include "../IconButton.h"
#include "IconsFontAwesome6.h"
#include "../OutputLog.h"
#include "../Panels/LandscapePanel.h"
#include "../Quest/QuestNodeView.h"
#include "../Quest/Blueprint/BlueprintEditor.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cctype>
#include <limits>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#include <d3d11.h>
#include <windows.h>
#include <DirectXMath.h>
#else
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#endif

extern ModelPreview g_mp;
extern bool g_mp_initialized;
extern FlyCam g_flycam;

void render_panel_handle_flycam(float dt);
extern std::atomic<bool> g_item_icon_dirty;
#ifdef _WIN32
bool spawn_level_model_at(ID3D11Device* device,
                          const std::string& model_path,
                          const float engine_pos[3]);
bool append_level_entity_model_at(
    ID3D11Device* device,
    const std::vector<uint32_t>& model_hashes,
    size_t marker_index,
    const float engine_pos[3]);
int spawn_level_container_at(
    ID3D11Device* device,
    const std::string& model_path,
    const float engine_pos[3],
    const LevelEdit::ContainerTemplateInfo& info);
#endif

bool g_skel_overlay_show = false;

int g_highlight_mesh_idx    = -1;
int g_isolate_mesh_idx      = -1;
int g_selected_level_mesh_idx = -1;
uint32_t g_selected_level_pick_id = 0;
uint64_t g_selected_level_hash = 0;



static bool details_panel_docked() {
    const FlatAssetEntry* lv = ContentTabs::ActiveLevelEntry();
    return lv && LandscapePanel::AppliesTo(*lv);
}



bool is_player_start_marker(const LevelSpawnMarker& marker)
{
    std::string low = marker.name;
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return low.rfind("startfrom", 0) == 0 ||
           low.rfind("teleportto", 0) == 0;
}

#ifdef _WIN32
ID3D11ShaderResourceView* g_tex_popout_srv = nullptr;
#else
unsigned int g_tex_popout_gl = 0;
#endif
std::string g_tex_popout_name;
bool        g_tex_popout_open    = false;

int         g_tex_popout_mesh_idx = -1;

bool        g_tex_popout_show_uvs = false;

namespace {
struct TerrainEditUI {
    int   tool             = 0;
    float brush_size       = 32.f;
    float brush_strength   = 1.f;
    bool  has_changes      = false;
    bool  open_save_confirm= false;
    bool  hover_valid      = false;
    float hover_x = 0.f, hover_y = 0.f, hover_z = 0.f;
};
static TerrainEditUI g_te_ui;
}

#ifdef _WIN32
ID3D11ShaderResourceView* g_heightmap_popout_srv = nullptr;
#endif
std::string          g_heightmap_popout_name;
std::string          g_heightmap_popout_kind = "Heightmap";
int                  g_heightmap_popout_w    = 0;
int                  g_heightmap_popout_h    = 0;
bool                 g_heightmap_popout_open = false;
std::vector<uint8_t> g_heightmap_popout_rgba;

std::atomic<bool>    g_pending_heightmap_view_load{false};
std::vector<uint8_t> g_pending_heightmap_view_rgba;
int                  g_pending_heightmap_view_w = 0;
int                  g_pending_heightmap_view_h = 0;
std::string          g_pending_heightmap_view_name;
std::string          g_pending_heightmap_view_kind = "Heightmap";

namespace UI {

    namespace {

static void draw_entity_gameplay_details(
    const Gdb::EntityGameplayDetails& details,
    bool show_entity_name = true);
static void draw_property_details(const Gdb::PropertyDetails& details);

#ifndef _WIN32
static void draw_entity_gameplay_details(
    const Gdb::EntityGameplayDetails& details,
    bool show_entity_name)
{
    ImGui::Spacing();
    ImGui::Separator();
    if (show_entity_name && !details.entity_name.empty()) {
        ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "%s",
                           details.entity_name.c_str());
    }
    ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f),
                       "Entity gameplay");
    if (!details.faction_name.empty()) {
        ImGui::Text("Faction / allegiance: %s",
                    details.faction_name.c_str());
    }
    if (!details.combat_profile_name.empty()) {
        ImGui::TextWrapped("Combat profile: %s",
                           details.combat_profile_name.c_str());
    }
    if (!details.core_fields.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                           "Core stats:");
        for (const auto& field : details.core_fields) {
            ImGui::Text("%s: %s", field.label.c_str(),
                        field.value.c_str());
        }
    }
    if (!details.combat_fields.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                           "Combat:");
        for (const auto& field : details.combat_fields) {
            ImGui::Text("%s: %s", field.label.c_str(),
                        field.value.c_str());
        }
    }
}
#endif

void draw_placeholder() {

    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin,
                      ImVec2(origin.x + region.x, origin.y + region.y),
                      IM_COL32(20, 22, 28, 255));
    ImGui::Dummy(region);
}

void draw_item_tab_content() {
    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        origin, ImVec2(origin.x + region.x, origin.y + region.y),
        IM_COL32(20, 22, 28, 255));

    ImGui::BeginChild("##item_tab_content", region, false);
    if (S.selected_item < 0 ||
        S.selected_item >= static_cast<int>(g_item_details.size())) {
        ImGui::TextDisabled("Item data is no longer available.");
        ImGui::EndChild();
        return;
    }
    const Gdb::ItemDetail& item =
        g_item_details[static_cast<std::size_t>(S.selected_item)];
    std::string name;
    if (item.name_tag) TextBank::Lookup(item.name_tag, name);
    if (name.empty()) {
        name = item.display_name.empty() ? item.label : item.display_name;
    }
    ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "%s",
                       name.c_str());
    if (item.money >= 0) ImGui::Text("Value: %d gold", item.money);

    std::string description;
    if (item.desc_tag) TextBank::Lookup(item.desc_tag, description);
    if (!description.empty()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(description.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!item.stats.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Stats");
        for (const auto& stat : item.stats) {
            ImGui::Text("%s: %s", stat.first.c_str(), stat.second.c_str());
        }
    }
    ImGui::EndChild();
}

void draw_entity_tab_content() {
    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        origin, ImVec2(origin.x + region.x, origin.y + region.y),
        IM_COL32(20, 22, 28, 255));

    ImGui::BeginChild("##entity_tab_content", region, false);
    if (S.selected_entity < 0 ||
        S.selected_entity >=
            static_cast<int>(g_global_entity_catalog.size())) {
        ImGui::TextDisabled("Entity data is no longer available.");
        ImGui::EndChild();
        return;
    }
    const auto& entity =
        g_global_entity_catalog[static_cast<std::size_t>(S.selected_entity)];
    ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "%s",
                       (entity.display_name.empty() ? entity.name
                                                    : entity.display_name)
                           .c_str());
    ImGui::TextDisabled("No renderable model was found for this entity.");
    const auto gameplay = g_global_entity_gameplay.find(entity.entity_hash);
    if (gameplay != g_global_entity_gameplay.end()) {
        draw_entity_gameplay_details(gameplay->second);
    }
    ImGui::EndChild();
}

bool is_adjacent_terrain_mesh_name(const std::string& name)
{
    return name.rfind("adjacent terrain", 0) == 0;
}

std::string clean_level_model_name(std::string name)
{
    const char* prefixes[] = { "prop: ", "engine_level: " };
    for (const char* prefix : prefixes) {
        const size_t n = std::strlen(prefix);
        if (name.rfind(prefix, 0) == 0) {
            name.erase(0, n);
            break;
        }
    }

    size_t hash = name.find('#');
    if (hash != std::string::npos) name.resize(hash);
    size_t inst = name.find(" (");
    if (inst != std::string::npos) name.resize(inst);

    std::replace(name.begin(), name.end(), '\\', '/');
    size_t slash = name.find_last_of('/');
    if (slash != std::string::npos) name = name.substr(slash + 1);

    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".mdl") {
        name.resize(name.size() - 4);
    }
    return name.empty() ? std::string("(unnamed)") : name;
}

std::string level_model_key_from_mesh_name(const std::string& name)
{
    std::string key = clean_level_model_name(name);
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return key;
}

std::string clean_level_material_name(std::string name)
{
    const size_t hash = name.find('#');
    if (hash != std::string::npos) {
        name.erase(0, hash + 1);
    } else {
        return clean_level_model_name(std::move(name));
    }

    const size_t inst = name.find(" (");
    if (inst != std::string::npos) name.resize(inst);

    return name.empty() ? std::string("(unnamed)") : name;
}

void draw_lua_source() {
    if (S.lua_preview_loading) {
        ImGui::TextDisabled("Decompiling...");
        return;
    }
    if (S.lua_preview_content.empty()) {
        ImGui::TextDisabled("(empty)");
        return;
    }

    ImVec2 sz = ImGui::GetContentRegionAvail();
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0.08f, 0.08f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text,           ImVec4(0.85f, 0.92f, 0.82f, 1.0f));
    ImGui::InputTextMultiline(
        "##lua_text",
        const_cast<char*>(S.lua_preview_content.c_str()),
        S.lua_preview_content.size() + 1,
        sz,
        ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleColor(4);
}

void draw_lua_in_panel() {
    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin,
                      ImVec2(origin.x + region.x, origin.y + region.y),
                      IM_COL32(18, 18, 22, 255));

    if (!S.lua_preview_is_quest) {
        draw_lua_source();
        return;
    }

    if (ImGui::BeginTabBar("##quest_preview_tabs")) {
        const bool authored = QuestUI::IsAuthoredQuestActive();
        const ImGuiTabItemFlags node_flags =
            S.quest_preview_select_nodes
                ? ImGuiTabItemFlags_SetSelected
                : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem(authored ? "Quest Flow" : "Node View",
                                nullptr, node_flags)) {
            if (S.lua_preview_loading) {
                ImGui::TextDisabled("Decompiling and building quest graph...");
            } else {
                QuestUI::DrawNodeView();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Lua Script")) {
            draw_lua_source();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    S.quest_preview_select_nodes = false;
}

void draw_gdb_in_panel() {
    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin,
                      ImVec2(origin.x + region.x, origin.y + region.y),
                      IM_COL32(18, 20, 23, 255));

    auto hex32 = [](uint32_t v) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08X", v);
        return std::string(buf);
    };
    auto hex4 = [](size_t v) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04X", (unsigned)(v & 0xFFFFu));
        return std::string(buf);
    };
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        return s;
    };
    auto row_label = [](const GdbViewerRow& r) -> std::string {
        if (!r.name.empty()) return r.name;
        if (!r.hash_name.empty()) return r.hash_name;
        return "(unnamed)";
    };
    auto record_kind = [](const GdbViewerRow& r) -> const char* {
        (void)r;
        return "RecordData";
    };
    auto detail = [](const char* name, const std::string& value) {
        ImGui::TreeNodeEx(name,
                          ImGuiTreeNodeFlags_Leaf |
                          ImGuiTreeNodeFlags_NoTreePushOnOpen |
                          ImGuiTreeNodeFlags_Bullet,
                          "%s | %s", name, value.c_str());
    };
    auto hash_detail_value = [&](uint32_t hash, const std::string& name) {
        std::string v = hex32(hash);
        if (!name.empty()) v += "  " + name;
        return v;
    };

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f, 0.93f, 1.0f, 1.0f));
    ImGui::TextUnformatted(S.gdb_view_title.empty()
                               ? "GDB"
                               : S.gdb_view_title.c_str());
    ImGui::PopStyleColor();

    const float btn_w = ImGui::CalcTextSize("Close").x +
                        ImGui::GetStyle().FramePadding.x * 2.0f + 8.0f;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x +
                    ImGui::GetCursorPosX() - btn_w);
    if (ImGui::SmallButton("Close##gdb_render")) {
        S.show_gdb_render = false;
    }
    ImGui::Separator();

    ImGui::SetNextItemWidth(340.0f);
    ImGui::InputTextWithHint("##gdb_filter", "Filter name/parent/hash",
                             &S.gdb_view_filter);
    ImGui::SameLine();
    ImGui::TextDisabled("%zu row(s)", S.gdb_view_rows.size());

    const std::string filter = lower(S.gdb_view_filter);
    std::vector<int> visible;
    visible.reserve(S.gdb_view_rows.size());
    for (size_t i = 0; i < S.gdb_view_rows.size(); ++i) {
        const auto& r = S.gdb_view_rows[i];
        if (filter.empty()) {
            visible.push_back((int)i);
            continue;
        }
        std::string hay = lower(row_label(r));
        hay += " " + lower(r.hash_name);
        hay += " " + lower(r.parent_name);
        hay += " " + lower(r.skeleton_file_name);
        hay += " " + lower(r.retarget_skeleton_file_name);
        hay += " " + lower(hex4(size_t(r.record_index) + 1));
        hay += " " + lower(r.model_path_name);
        for (const std::string& model_name : r.model_path_names) {
            hay += " " + lower(model_name);
        }
        hay += " " + lower(hex32(r.hash));
        hay += " " + lower(hex32(r.parent_hash));
        hay += " " + lower(hex32(r.model_path_hash));
        hay += " " + lower(hex32(r.skeleton_file_hash));
        hay += " " + lower(hex32(r.retarget_skeleton_file_hash));
        for (uint32_t model_hash : r.model_path_hashes) {
            hay += " " + lower(hex32(model_hash));
        }
        if (hay.find(filter) != std::string::npos) {
            visible.push_back((int)i);
        }
    }

    ImGui::Separator();
    ImGui::BeginChild("##gdb_tree_body", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (int row_i : visible) {
        const auto& r = S.gdb_view_rows[(size_t)row_i];
        const std::string id = hex4(size_t(r.record_index) + 1);
        const std::string label = row_label(r);

        ImGui::PushID(row_i);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
        if (row_i == 0) flags |= ImGuiTreeNodeFlags_DefaultOpen;
        const bool open = ImGui::TreeNodeEx(
            "##gdb_record", flags, "%s  %s", id.c_str(), label.c_str());
        if (open) {
            detail(record_kind(r), label);
            detail("Index", hex4(size_t(r.record_index) + 1));
            detail("Hash", hash_detail_value(r.hash, r.hash_name));
            if (r.parent_hash != 0 || !r.parent_name.empty()) {
                detail("Parent",
                       hash_detail_value(r.parent_hash, r.parent_name));
            }
            if (r.model_path_hash != 0) {
                detail("ModelPathHash",
                       hash_detail_value(r.model_path_hash,
                                         r.model_path_name));
            }
            if (r.skeleton_file_hash != 0) {
                detail("SkeletonFile",
                       hash_detail_value(r.skeleton_file_hash,
                                         r.skeleton_file_name));
            }
            if (r.retarget_skeleton_file_hash != 0) {
                detail("RetargetSkeletonFile",
                       hash_detail_value(r.retarget_skeleton_file_hash,
                                         r.retarget_skeleton_file_name));
            }
            if (r.model_path_hashes.size() > 1) {
                const bool models_open = ImGui::TreeNodeEx(
                    "##gdb_model_hashes",
                    ImGuiTreeNodeFlags_SpanAvailWidth,
                    "ModelPathHashes | %zu", r.model_path_hashes.size());
                if (models_open) {
                    for (size_t mi = 0; mi < r.model_path_hashes.size(); ++mi) {
                        const std::string name =
                            mi < r.model_path_names.size()
                                ? r.model_path_names[mi]
                                : std::string();
                        detail("ModelPathHash",
                               hash_detail_value(r.model_path_hashes[mi],
                                                 name));
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

#ifdef _WIN32

namespace {
ID3D11SamplerState*  g_tex_point_sampler = nullptr;
ID3D11DeviceContext* g_tex_preview_ctx   = nullptr;

void ensure_point_sampler(ID3D11Device* device) {
    if (g_tex_point_sampler || !device) return;
    if (!g_tex_preview_ctx) {

        device->GetImmediateContext(&g_tex_preview_ctx);
    }
    D3D11_SAMPLER_DESC desc{};
    desc.Filter   = D3D11_FILTER_MIN_MAG_MIP_POINT;
    desc.AddressU = desc.AddressV = desc.AddressW =
        D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.MinLOD   = 0.0f;
    desc.MaxLOD   = D3D11_FLOAT32_MAX;
    device->CreateSamplerState(&desc, &g_tex_point_sampler);
}

void bind_point_sampler_cb(const ImDrawList* ,
                           const ImDrawCmd*  ) {
    if (g_tex_preview_ctx && g_tex_point_sampler) {
        g_tex_preview_ctx->PSSetSamplers(0, 1, &g_tex_point_sampler);
    }
}
}

void draw_texture_in_panel(ID3D11Device* device) {
    ensure_point_sampler(device);

    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(origin,
                      ImVec2(origin.x + region.x, origin.y + region.y),
                      IM_COL32(20, 22, 28, 255));

    if (!S.texture_window_srv || S.texture_window_width <= 0 || S.texture_window_height <= 0) {
        const char* msg = S.texture_window_name.empty()
            ? "Texture decode failed"
            : S.texture_window_name.c_str();
        ImVec2 sz = ImGui::CalcTextSize(msg);
        ImVec2 pos(origin.x + (region.x - sz.x) * 0.5f,
                   origin.y + (region.y - sz.y) * 0.5f);
        dl->AddText(pos, IM_COL32(255, 90, 90, 230), msg);
        ImGui::Dummy(region);
        return;
    }

    float tw = (float)S.texture_window_width;
    float th = (float)S.texture_window_height;
    float scale = std::min(region.x / tw, region.y / th);
    if (scale > 4.0f) scale = 4.0f;
    float dw = tw * scale;
    float dh = th * scale;
    float x0 = origin.x + (region.x - dw) * 0.5f;
    float y0 = origin.y + (region.y - dh) * 0.5f;

    if (g_tex_point_sampler) {
        dl->AddCallback(bind_point_sampler_cb, nullptr);
    }
    dl->AddImage((ImTextureID)S.texture_window_srv,
                 ImVec2(x0, y0),
                 ImVec2(x0 + dw, y0 + dh));
    if (g_tex_point_sampler) {
        dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }

    {
        ImGui::SetCursorScreenPos(ImVec2(x0, y0));
        ImGui::InvisibleButton("##tex_preview_hit", ImVec2(dw, dh));
        if (S.tex_info_ok && !S.texture_blob.empty() &&
            ImGui::BeginPopupContextItem()) {
            tex_export_menu_blob(S.texture_window_name,
                                 S.texture_blob,
                                 S.texture_mip_index);
            ImGui::EndPopup();
        }
    }

    if (S.tex_info_ok && !S.texture_blob.empty()) {
        const int total = (int)S.tex_info.Mips.size();

        if (S.texture_mip_index < 0) S.texture_mip_index = 0;
        if (S.texture_mip_index >= std::max(1, total))
            S.texture_mip_index = std::max(0, total - 1);

        int mw = 0, mh = 0;
        if (S.texture_mip_index >= 0 && S.texture_mip_index < total) {
            const auto& mm = S.tex_info.Mips[(size_t)S.texture_mip_index];
            mw = mm.HasWH ? (int)mm.MipWidth
                          : std::max(1, (int)S.tex_info.TextureWidth  >> S.texture_mip_index);
            mh = mm.HasWH ? (int)mm.MipHeight
                          : std::max(1, (int)S.tex_info.TextureHeight >> S.texture_mip_index);
        }

        const float kOverlayW = 230.0f;
        ImGui::SetNextWindowPos(ImVec2(origin.x + region.x - kOverlayW - 8.0f,
                                       origin.y + 6.0f));
        ImGui::SetNextWindowSize(ImVec2(kOverlayW, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.78f);
        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##tex_mip_selector", nullptr, fl)) {

            if (total > 1) {
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Mip");
                ImGui::SameLine();
                if (ImGui::ArrowButton("##mip_prev", ImGuiDir_Left)) {
                    if (S.texture_mip_index > 0) {
                        S.texture_mip_index--;
                        S.pending_texture_mip_change = true;
                    }
                }
                ImGui::SameLine();
                ImGui::Text("%d / %d", S.texture_mip_index, total - 1);
                ImGui::SameLine();
                if (ImGui::ArrowButton("##mip_next", ImGuiDir_Right)) {
                    if (S.texture_mip_index < total - 1) {
                        S.texture_mip_index++;
                        S.pending_texture_mip_change = true;
                    }
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(%dx%d)", mw, mh);
            } else {

                ImGui::TextDisabled("%dx%d", mw, mh);
            }

            if (ImGui::Checkbox("R", &S.tex_show_r))
                S.pending_texture_mip_change = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("G", &S.tex_show_g))
                S.pending_texture_mip_change = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("B", &S.tex_show_b))
                S.pending_texture_mip_change = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("A", &S.tex_show_a))
                S.pending_texture_mip_change = true;
        }
        ImGui::End();
    }

    ImGui::Dummy(region);
}

void apply_orbit_to_flycam() {
    float r = std::max(g_mp.radius, 0.5f) * std::max(S.cam_dist, 0.1f);
    const float target_y = g_mp.center[1] + S.cam_target_offset_y;
    float yaw   = S.cam_yaw;
    float pitch = S.cam_pitch;
    float cy = cosf(pitch);
    float sy = sinf(pitch);
    float cx = cosf(yaw);
    float sx = sinf(yaw);

    g_flycam.pos[0] = g_mp.center[0] + r * cy * sx;
    g_flycam.pos[1] = target_y + r * sy;
    g_flycam.pos[2] = g_mp.center[2] + r * cy * cx;

    g_flycam.yaw   = yaw + 3.14159265f;
    g_flycam.pitch = -pitch;
    g_flycam.is_looking = false;
}

static void project_bones_to_screen(
    const std::vector<float>& world_pose,
    uint32_t bone_count,
    const ImVec2& origin,
    const ImVec2& region,
    std::vector<ImVec2>& out_screen,
    std::vector<uint8_t>& out_visible)
{
    using namespace DirectX;

    out_screen.assign(bone_count, ImVec2(0, 0));
    out_visible.assign(bone_count, 0);
    if (bone_count == 0) return;

    float cy = cosf(g_flycam.yaw);
    float sy = sinf(g_flycam.yaw);
    float cp = cosf(g_flycam.pitch);
    float sp = sinf(g_flycam.pitch);
    XMVECTOR eye = XMVectorSet(g_flycam.pos[0], g_flycam.pos[1], g_flycam.pos[2], 1);
    XMVECTOR at  = XMVectorSet(g_flycam.pos[0] + sy * cp,
                               g_flycam.pos[1] + sp,
                               g_flycam.pos[2] + cy * cp, 1);
    XMVECTOR up  = XMVectorSet(0, 1, 0, 0);
    XMMATRIX V = XMMatrixLookAtLH(eye, at, up);

    float fov       = XMConvertToRadians(60.0f);
    float aspect    = region.x / std::max(1.0f, region.y);
    float far_plane = std::max(g_mp.radius * 100.0f, 100.0f);
    XMMATRIX P = XMMatrixPerspectiveFovLH(fov, aspect, 0.05f, far_plane);

    XMMATRIX W = XMMatrixIdentity();
    if (!g_mp.no_tilt) {
        const float tiltX = -XM_PIDIV2;
        XMMATRIX Tm = XMMatrixTranslation(-g_mp.center[0], -g_mp.center[1], -g_mp.center[2]);
        XMMATRIX Rx = XMMatrixRotationX(tiltX);
        XMMATRIX Tp = XMMatrixTranslation( g_mp.center[0],  g_mp.center[1],  g_mp.center[2]);
        XMMATRIX FlipX = XMMatrixScaling(-1.0f, 1.0f, 1.0f);
        W = Tm * Rx * Tp * FlipX;
    }
    XMMATRIX WVP = W * V * P;

    for (uint32_t i = 0; i < bone_count; ++i) {
        XMFLOAT4X4 wf;
        std::memcpy(&wf, &world_pose[(size_t)i * 16], sizeof(float) * 16);
        XMMATRIX Wp = XMLoadFloat4x4(&wf);
        XMVECTOR pos = XMVector3Transform(XMVectorSet(0, 0, 0, 1), Wp);
        XMVECTOR clip = XMVector4Transform(XMVectorSetW(pos, 1.0f), WVP);
        float w = XMVectorGetW(clip);
        if (w <= 0.05f) continue;
        float ndcx = XMVectorGetX(clip) / w;
        float ndcy = XMVectorGetY(clip) / w;
        if (ndcx < -1.5f || ndcx > 1.5f) continue;
        if (ndcy < -1.5f || ndcy > 1.5f) continue;
        out_screen[i].x = origin.x + (ndcx * 0.5f + 0.5f) * region.x;
        out_screen[i].y = origin.y + (1.0f - (ndcy * 0.5f + 0.5f)) * region.y;
        out_visible[i] = 1;
    }
}

#ifdef _WIN32
static void draw_gdb_placements_overlay(const ImVec2& origin,
                                        const ImVec2& region)
{
    using namespace DirectX;
    if (g_level_gdb_placements.empty()) return;
    if (!S.show_gdb_placements) return;

    float cy = cosf(g_flycam.yaw);
    float sy = sinf(g_flycam.yaw);
    float cp = cosf(g_flycam.pitch);
    float sp = sinf(g_flycam.pitch);
    XMVECTOR eye = XMVectorSet(g_flycam.pos[0], g_flycam.pos[1], g_flycam.pos[2], 1);
    XMVECTOR at  = XMVectorSet(g_flycam.pos[0] + sy * cp,
                               g_flycam.pos[1] + sp,
                               g_flycam.pos[2] + cy * cp, 1);
    XMVECTOR up  = XMVectorSet(0, 1, 0, 0);
    XMMATRIX V = XMMatrixLookAtLH(eye, at, up);
    float fov = XMConvertToRadians(60.0f);
    float aspect = region.x / std::max(1.0f, region.y);
    float far_plane = std::max(g_mp.radius * 100.0f, 1000.0f);
    XMMATRIX P = XMMatrixPerspectiveFovLH(fov, aspect, 0.05f, far_plane);
    XMMATRIX VP = V * P;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col_fixed = IM_COL32(255, 80, 80, 230);
    const ImU32 col_var   = IM_COL32(120, 220, 255, 180);

    const int   gw = g_pending_terrain_ghf_width;
    const int   gh = g_pending_terrain_ghf_height;
    const float tile = g_pending_terrain_ghf_tile_size > 0.0f
                         ? g_pending_terrain_ghf_tile_size : 0.5f;
    const auto& heights = g_pending_terrain_ghf_heights;
    const bool  have_terrain = (gw > 0 && gh > 0 &&
                                heights.size() == size_t(gw) * size_t(gh));
    auto sample_height = [&](float wx, float wy) -> float {
        if (!have_terrain) return 0.0f;
        const float gx = wx / tile;
        const float gy = wy / tile;
        int ix = int(gx); int iy = int(gy);
        if (ix < 0) ix = 0; else if (ix >= gw) ix = gw - 1;
        if (iy < 0) iy = 0; else if (iy >= gh) iy = gh - 1;
        return heights[size_t(iy) * size_t(gw) + size_t(ix)];
    };

    size_t drawn = 0;
    for (const auto& gp : g_level_gdb_placements) {
        const float rx = gp.x;
        const float ry = sample_height(gp.x, gp.y) + 1.0f;
        const float rz = gp.y;
        XMVECTOR clip = XMVector4Transform(XMVectorSet(rx, ry, rz, 1.0f), VP);
        float w = XMVectorGetW(clip);
        if (w <= 0.05f) continue;
        float ndcx = XMVectorGetX(clip) / w;
        float ndcy = XMVectorGetY(clip) / w;
        if (ndcx < -1.2f || ndcx > 1.2f) continue;
        if (ndcy < -1.2f || ndcy > 1.2f) continue;
        ImVec2 sp_screen;
        sp_screen.x = origin.x + (ndcx * 0.5f + 0.5f) * region.x;
        sp_screen.y = origin.y + (1.0f - (ndcy * 0.5f + 0.5f)) * region.y;

        const bool fixed = (gp.marker == 0x00004B40);
        const ImU32 col  = fixed ? col_fixed : col_var;
        const float r    = fixed ? 4.0f : 2.5f;
        dl->AddCircleFilled(sp_screen, r, col);
        if (fixed) {
            dl->AddCircle(sp_screen, r + 1.0f, IM_COL32(0, 0, 0, 200), 12, 1.0f);
        }
        if (fixed) {
            dl->AddText(ImVec2(sp_screen.x + r + 4.0f, sp_screen.y - 7.0f),
                        IM_COL32(255, 150, 150, 235), "Player start");
        }
        ++drawn;
    }

    char buf[96];
    std::snprintf(buf, sizeof(buf),
                  "gdb placements: %zu shown / %zu total",
                  drawn, g_level_gdb_placements.size());
    dl->AddText(ImVec2(origin.x + 14, origin.y + region.y - 22),
                IM_COL32(220, 220, 220, 200), buf);
}

static int g_sel_spawn_marker = -1;
static int g_sel_pending_sp = -1;
static int g_sel_pending_gen = -1;
static bool g_paint_composite_pending = false;
static bool g_marker_clear_selection = false;
static bool g_add_menu_requested = false;
static float g_add_menu_requested_pos[3] = {0, 0, 0};

static bool level_marker_visible(const LevelSpawnMarker& marker)
{
    if (::is_player_start_marker(marker)) return true;
    if (marker.kind == 0) return false;
    if (marker.kind == 4) return S.show_dig_spots;
    if (marker.is_container && S.show_containers) return true;
    if (marker.kind == 3) return S.show_ent_npcs;
    if (marker.kind == 6) return S.show_entity_models;
    if (marker.kind == 5) return false;
    return S.show_spawn_markers;
}

static std::string pretty_container_item_tag(std::string tag, int money)
{
    for (const char* pfx : {"INV_ITEM_", "OBJECT_", "TEXT_"}) {
        const size_t n = std::strlen(pfx);
        if (tag.size() > n && tag.compare(0, n, pfx) == 0) {
            tag = tag.substr(n);
            break;
        }
    }
    if (tag.size() > 5 &&
        tag.compare(tag.size() - 5, 5, "_NAME") == 0) {
        tag.resize(tag.size() - 5);
    }
    if (tag.find('_') != std::string::npos ||
        std::none_of(tag.begin(), tag.end(), [](unsigned char c) {
            return std::islower(c);
        })) {
        bool word_start = true;
        for (char& c : tag) {
            if (c == '_') {
                c = ' ';
                word_start = true;
            } else {
                c = word_start
                    ? char(std::toupper(static_cast<unsigned char>(c)))
                    : char(std::tolower(static_cast<unsigned char>(c)));
                word_start = false;
            }
        }
    }
    if (money >= 0) {
        tag += " (" + std::to_string(money) + " gold)";
    }
    return tag;
}

static std::string container_catalog_label(uint32_t record_hash)
{
    for (const auto& item : g_item_details) {
        if (item.record_hash == record_hash && !item.display_name.empty()) {
            return item.display_name;
        }
    }
    for (const auto& item : g_level_item_catalog) {
        if (item.record_hash == record_hash) {
            return pretty_container_item_tag(item.label, item.money);
        }
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", record_hash);
    return buf;
}

static std::string container_item_label(const Gdb::EntityContentsItem& item)
{
    if (!item.display_name.empty()) return item.display_name;
    std::string tag = !item.name_tag.empty() ? item.name_tag
                                             : item.entry_label;
    if (tag.empty()) return container_catalog_label(item.record_hash);
    return pretty_container_item_tag(std::move(tag), item.money);
}

static const char* container_yes_no_unknown(int value)
{
    if (value < 0) return "Not authored";
    return value != 0 ? "Yes" : "No";
}

static void draw_container_authored_rules(
    const Gdb::EntityContents& contents)
{
    const char* kind = "Inventory container";
    if (contents.is_dig_spot) kind = "Dig spot";
    else if (contents.has_chest_component) kind = "Chest";
    ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f), "%s", kind);

    if (contents.has_chest_component &&
        contents.silver_keys_needed > 0) {
        ImGui::Text("Silver keys required: %d",
                    contents.silver_keys_needed);
    }

    if (contents.item_repopulation_record != 0) {
        ImGui::Text("Can be stolen from: %s",
                    container_yes_no_unknown(
                        contents.can_be_stolen_from));
        const char* mode = "Never";
        if (contents.can_respawn_items_repeatedly > 0) {
            mode = "Repeatedly";
        } else if (contents.can_respawn_items_once > 0) {
            mode = "Once";
        } else if (contents.can_respawn_items_repeatedly < 0 &&
                   contents.can_respawn_items_once < 0) {
            mode = "Not authored";
        }
        ImGui::Text("Loot repopulation: %s", mode);
        if (contents.chance_of_respawning >= 0.0f) {
            ImGui::Text("Authored repopulation chance: %.1f%%",
                        contents.chance_of_respawning);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "The game applies its global item-respawn multiplier "
                    "and any active context multiplier at runtime.");
            }
        }
    } else {
        ImGui::Text("Loot repopulation: Not configured");
    }

    if (contents.is_dig_spot) {
        ImGui::Text("Dog can lead to: %s",
                    contents.dog_can_lead_to ? "Yes" : "No");
        if (contents.dog_lead_radius >= 0.0f) {
            ImGui::Text("Dog search radius: %.1f",
                        contents.dog_lead_radius);
        }
        if (contents.dog_lead_priority >= 0) {
            ImGui::Text("Dog lead priority: %d",
                        contents.dog_lead_priority);
        }
    }

}

static void draw_container_potential_items(
    const Gdb::EntityContents& contents,
    size_t max_rows = 12)
{
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                       "Potential items:");
    if (contents.potential_items.empty()) {
        ImGui::TextDisabled("  (none)");
        return;
    }

    const bool scroll = contents.potential_items.size() > max_rows;
    if (scroll) {
        ImGui::BeginChild("##potential_items_scroll",
                          ImVec2(0.0f, 240.0f), true);
    }
    const size_t count = contents.potential_items.size();
    for (size_t i = 0; i < count; ++i) {
        const auto& item = contents.potential_items[i];
        ImGui::PushID(int(i) + 0x5A00);
        ImGui::BulletText("%s", container_item_label(item).c_str());
        ImGui::PopID();
    }
    if (scroll) ImGui::EndChild();
}



static void draw_dig_spot_level_selector(uint32_t current_table,
                                         bool editable,
                                         bool is_addition,
                                         uint32_t entity_hash,
                                         int addition_index)
{
    struct LevelRow {
        int level = 0;
        uint32_t table = 0;
    };
    std::vector<LevelRow> rows;
    constexpr char kPrefix[] = "MarkerDiggingSpotLevel";
    constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;
    for (const auto& [hash, c] : g_level_entity_contents) {
        (void)hash;
        if (!c.potential_items_record) continue;
        if (c.entity_name.rfind(kPrefix, 0) != 0) continue;
        const int level = std::atoi(c.entity_name.c_str() + kPrefixLen);
        if (level <= 0) continue;
        bool duplicate = false;
        for (const LevelRow& row : rows) {
            if (row.level == level) { duplicate = true; break; }
        }
        if (!duplicate) rows.push_back({level, c.potential_items_record});
    }
    if (rows.empty()) return;
    std::sort(rows.begin(), rows.end(),
              [](const LevelRow& a, const LevelRow& b) {
                  return a.level < b.level;
              });

    std::string current_label = "-";
    for (const LevelRow& row : rows) {
        if (row.table == current_table) {
            current_label = "Level " + std::to_string(row.level);
            break;
        }
    }

    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                       "Dig spot level:");
    ImGui::BeginDisabled(!editable);
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::BeginCombo("##dig_spot_level", current_label.c_str())) {
        for (const LevelRow& row : rows) {
            const std::string label = "Level " + std::to_string(row.level);
            if (ImGui::Selectable(label.c_str(),
                                  row.table == current_table)) {
                if (is_addition) {
                    LevelEdit::SetAdditionLootTable(addition_index,
                                                    row.table);
                } else {
                    LevelEdit::SetContainerLootTable(entity_hash,
                                                     row.table);
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
}

static void draw_container_loot_table_editor(
    uint32_t entity_hash,
    const Gdb::EntityContents& contents)
{
    uint32_t current = contents.potential_items_record;
    const bool staged =
        LevelEdit::GetContainerLootTable(entity_hash, current);
    std::string current_label = "None";
    const Gdb::EntityContents* current_source = nullptr;
    if (current != 0) {
        for (const auto& [hash, candidate] : g_level_entity_contents) {
            (void)hash;
            if (candidate.potential_items_record != current) continue;
            current_source = &candidate;
            current_label = candidate.entity_name.empty()
                ? "Authored loot table" : candidate.entity_name;
            break;
        }
        if (!current_source) current_label = "Unknown authored table";
    }

    const bool editable = LevelEdit::Enabled() && !LevelEdit::Saving();
    if (contents.is_dig_spot) {
        draw_dig_spot_level_selector(current, editable, false,
                                     entity_hash, -1);
    }
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                       staged ? "Random loot (edited):" : "Random loot:");
    ImGui::BeginDisabled(!editable);
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::BeginCombo("##container_random_loot",
                          current_label.c_str())) {
        if (ImGui::Selectable("None", current == 0)) {
            LevelEdit::SetContainerLootTable(entity_hash, 0);
        }
        std::unordered_set<uint32_t> seen;
        for (const auto& [hash, candidate] : g_level_entity_contents) {
            (void)hash;
            if (!candidate.potential_items_record ||
                !seen.insert(candidate.potential_items_record).second) {
                continue;
            }
            std::string label = candidate.entity_name.empty()
                ? "Authored loot table" : candidate.entity_name;
            label += "##" + std::to_string(
                candidate.potential_items_record);
            if (ImGui::Selectable(
                    label.c_str(),
                    current == candidate.potential_items_record)) {
                LevelEdit::SetContainerLootTable(
                    entity_hash, candidate.potential_items_record);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (staged && editable) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Revert##container_loot")) {
            LevelEdit::ClearContainerLootTable(entity_hash);
        }
    }
    if (current_source) {
        draw_container_potential_items(*current_source);
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                           "Potential items:");
        ImGui::TextDisabled("  (none)");
    }
}

static void draw_entity_gameplay_details(
    const Gdb::EntityGameplayDetails& details,
    bool show_entity_name)
{
    ImGui::Spacing();
    ImGui::Separator();
    if (show_entity_name && !details.entity_name.empty()) {
        ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "%s",
                           details.entity_name.c_str());
    }
    ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f),
                       "Entity gameplay");
    if (!details.faction_name.empty()) {
        ImGui::Text("Faction / allegiance: %s",
                    details.faction_name.c_str());
    }
    if (!details.combat_profile_name.empty()) {
        ImGui::TextWrapped("Combat profile: %s",
                           details.combat_profile_name.c_str());
    }

    if (!details.core_fields.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                           "Core stats:");
        for (const auto& field : details.core_fields) {
            ImGui::Text("%s: %s", field.label.c_str(),
                        field.value.c_str());
        }
    }
    if (!details.combat_fields.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                           "Combat:");
        for (const auto& field : details.combat_fields) {
            ImGui::Text("%s: %s", field.label.c_str(),
                        field.value.c_str());
        }
    }

}

static void draw_property_details(const Gdb::PropertyDetails& details)
{
    ImGui::Spacing();
    ImGui::Separator();
    const std::string& title = !details.display_name.empty()
        ? details.display_name : details.building_entity_name;
    if (!title.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1.0f), "%s",
                           title.c_str());
    }
    ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f),
                       "Property");
    if (!details.building_entity_name.empty() &&
        details.building_entity_name != title) {
        ImGui::TextWrapped("Level instance: %s",
                           details.building_entity_name.c_str());
    }
    if (!details.building_type_name.empty()) {
        ImGui::Text("Type: %s", details.building_type_name.c_str());
    }
    if (!details.address.empty()) {
        ImGui::TextWrapped("Address: %s", details.address.c_str());
    }
    if (details.basic_sale_price >= 0) {
        ImGui::Text("Base sale price: %d gold",
                    details.basic_sale_price);
    }
    if (details.can_rent >= 0) {
        ImGui::Text("Can rent: %s", details.can_rent ? "Yes" : "No");
    }
    if (!details.has_building_record) {
        ImGui::TextDisabled(
            "The linked building data is not loaded in this scenario.");
        return;
    }
    const bool has_more =
        (!details.benefits.empty() &&
         details.benefits != "BUILDING_BENEFITS_NONE") ||
        (!details.anecdotes.empty() &&
         details.anecdotes != "BUILDING_ANECDOTES_NONE") ||
        !details.fields.empty();
    if (has_more) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f),
                           "Property Data:");
        if (!details.benefits.empty() &&
            details.benefits != "BUILDING_BENEFITS_NONE") {
            ImGui::TextWrapped("Benefits: %s", details.benefits.c_str());
        }
        if (!details.anecdotes.empty() &&
            details.anecdotes != "BUILDING_ANECDOTES_NONE") {
            ImGui::TextWrapped("Details: %s", details.anecdotes.c_str());
        }
        for (const auto& field : details.fields) {
            ImGui::TextWrapped("%s: %s", field.label.c_str(),
                               field.value.c_str());
        }
    }
}

static void draw_level_container_details(uint32_t entity_hash)
{
    auto found = g_level_entity_contents.find(entity_hash);
    if (found == g_level_entity_contents.end()) return;
    const Gdb::EntityContents& contents = found->second;

    ImGui::Separator();
    draw_container_authored_rules(contents);

    std::vector<uint32_t> staged_items;
    const bool staged =
        LevelEdit::GetChestContents(entity_hash, staged_items);
    if (staged || !contents.initial_items.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                           staged ? "Initial items (edited):"
                                  : "Initial items:");
        const size_t item_count = staged ? staged_items.size()
                                         : contents.initial_items.size();
        for (size_t i = 0; i < item_count; ++i) {
            const std::string label = staged
                ? container_catalog_label(staged_items[i])
                : container_item_label(contents.initial_items[i]);
            ImGui::BulletText("%s", label.c_str());
        }
        if (item_count == 0) ImGui::TextDisabled("  (empty)");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                           "Initial items:");
        ImGui::TextDisabled("  (none)");
    }
    draw_container_loot_table_editor(entity_hash, contents);
}

static void draw_addition_container_details(int addition_index)
{
    const bool editable = LevelEdit::Enabled() && !LevelEdit::Saving();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "%s",
                       LevelEdit::AdditionIsDigSpot(addition_index)
                           ? "New dig spot (unsaved)"
                           : "New container (unsaved)");
    std::vector<uint32_t> items;
    LevelEdit::GetAdditionChestItems(addition_index, items);
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                       "Initial items:");
    int remove_index = -1;
    for (size_t i = 0; i < items.size(); ++i) {
        ImGui::PushID(int(i) + 0x7300);
        if (editable) {
            if (ImGui::SmallButton("x")) remove_index = int(i);
            ImGui::SameLine();
        } else {
            ImGui::Bullet();
            ImGui::SameLine();
        }
        ImGui::TextUnformatted(container_catalog_label(items[i]).c_str());
        ImGui::PopID();
    }
    if (items.empty()) ImGui::TextDisabled("  (empty)");
    if (remove_index >= 0 && size_t(remove_index) < items.size()) {
        items.erase(items.begin() + remove_index);
        LevelEdit::SetAdditionChestItems(addition_index, items);
    }

    static int s_addition_picker = -1;
    static char s_addition_filter[64] = {};
    if (editable && ImGui::SmallButton("+ Add item")) {
        s_addition_picker = addition_index;
        s_addition_filter[0] = 0;
        ImGui::OpenPopup("##marker_container_item_picker");
    }
    if (ImGui::BeginPopup("##marker_container_item_picker")) {
        if (s_addition_picker != addition_index) {
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::SetNextItemWidth(260.0f);
            ImGui::InputTextWithHint("##marker_item_filter",
                                     "Search items...",
                                     s_addition_filter,
                                     sizeof(s_addition_filter));
            std::string filter = s_addition_filter;
            std::transform(filter.begin(), filter.end(), filter.begin(),
                           ::tolower);
            ImGui::BeginChild("##marker_item_rows",
                              ImVec2(300.0f, 260.0f), true);
            for (const auto& item : g_item_details) {
                const std::string label = item.display_name.empty()
                    ? item.label : item.display_name;
                std::string low = label;
                std::transform(low.begin(), low.end(), low.begin(),
                               ::tolower);
                if (!filter.empty() &&
                    low.find(filter) == std::string::npos) {
                    continue;
                }
                ImGui::PushID(int(item.record_hash));
                if (ImGui::Selectable(label.c_str())) {
                    items.push_back(item.record_hash);
                    LevelEdit::SetAdditionChestItems(addition_index, items);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        ImGui::EndPopup();
    }

    const uint32_t current =
        LevelEdit::GetAdditionLootTable(addition_index);
    if (LevelEdit::AdditionIsDigSpot(addition_index)) {
        draw_dig_spot_level_selector(current, editable, true, 0,
                                     addition_index);
    }
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                       "Random loot:");
    std::string current_label = "None";
    for (const auto& [hash, contents] : g_level_entity_contents) {
        (void)hash;
        if (contents.potential_items_record == current && current != 0) {
            current_label = contents.entity_name.empty()
                ? "Authored loot table" : contents.entity_name;
            break;
        }
    }
    ImGui::BeginDisabled(!editable);
    if (ImGui::BeginCombo("##marker_random_loot", current_label.c_str())) {
        if (ImGui::Selectable("None", current == 0)) {
            LevelEdit::SetAdditionLootTable(addition_index, 0);
        }
        std::unordered_set<uint32_t> seen_tables;
        for (const auto& [hash, contents] : g_level_entity_contents) {
            (void)hash;
            if (!contents.potential_items_record ||
                !seen_tables.insert(contents.potential_items_record).second) {
                continue;
            }
            const std::string label = contents.entity_name.empty()
                ? "Authored loot table" : contents.entity_name;
            if (ImGui::Selectable(
                    label.c_str(), current == contents.potential_items_record)) {
                LevelEdit::SetAdditionLootTable(
                    addition_index, contents.potential_items_record);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("authored into the level GDB on Save");
}

struct ContainerSpawnChoice {
    std::string label;
    std::string model_path;
    bool is_dive = false;
    LevelEdit::ContainerTemplateInfo info;
};

static uint32_t level_model_path_hash(const std::string& path)
{
    uint32_t hash = 0x811C9DC5u;
    for (unsigned char c : path) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<unsigned char>(c - 'A' + 'a');
        }
        if (c == '/') c = '\\';
        hash *= 0x01000193u;
        hash ^= uint32_t(c);
    }
    return hash;
}

static std::string container_spawn_label(std::string value)
{
    const size_t slash = value.find_last_of("/\\");
    if (slash != std::string::npos) value.erase(0, slash + 1);
    const size_t dot = value.rfind('.');
    if (dot != std::string::npos) value.resize(dot);
    for (char& c : value) {
        if (c == '_' || c == '-') c = ' ';
    }
    if (value.empty()) value = "Unnamed container";
    return value;
}

static std::vector<ContainerSpawnChoice> build_container_spawn_choices()
{
    static std::string cached_level;
    static size_t cached_contents = size_t(-1);
    static size_t cached_models = size_t(-1);
    static std::vector<ContainerSpawnChoice> cached;
    if (cached_level == g_pending_terrain_label &&
        cached_contents == g_level_entity_contents.size() &&
        cached_models == S.all_mdl_files.size()) {
        return cached;
    }
    std::unordered_map<uint32_t, std::string> models_by_hash;
    models_by_hash.reserve(S.all_mdl_files.size() * 2);
    for (const auto& model : S.all_mdl_files) {
        models_by_hash.emplace(level_model_path_hash(model.full_path),
                               model.full_path);
    }
    std::unordered_map<uint32_t, uint32_t> placement_models;
    for (const auto& placement : g_level_gdb_placements) {
        if (placement.hash && placement.model_path_hash) {
            placement_models.emplace(placement.hash,
                                     placement.model_path_hash);
        }
    }

    std::vector<ContainerSpawnChoice> out;
    std::unordered_set<std::string> seen;
    for (const auto& [entity_hash, contents] : g_level_entity_contents) {
        if (!contents.is_dig_spot && !contents.is_dive_spot &&
            g_level_entity_gameplay.count(entity_hash)) {
            continue;
        }
        if (!contents.is_dig_spot && !contents.is_dive_spot &&
            !contents.has_inventory_component &&
            !contents.has_chest_component) {
            continue;
        }

        uint32_t model_hash = contents.model_path_hash;
        if (!model_hash) {
            const auto placed = placement_models.find(entity_hash);
            if (placed != placement_models.end()) model_hash = placed->second;
        }
        ContainerSpawnChoice choice;
        choice.is_dive = contents.is_dive_spot;
        choice.info.entity_name = contents.entity_name;
        choice.info.is_dig_spot = contents.is_dig_spot;
        choice.info.silver_keys_needed =
            std::max(0, contents.silver_keys_needed);
        choice.info.entity_template = contents.entity_template;
        choice.info.transform_component_field =
            contents.transform_component_field;
        choice.info.transform_component_template =
            contents.transform_component_template;
        choice.info.physics_file_hash = contents.physics_file_hash;
        choice.info.potential_items_record =
            contents.potential_items_record;
        for (const auto& item : contents.initial_items) {
            choice.info.initial_items.push_back(item.record_hash);
        }
        if (model_hash) {
            const auto model = models_by_hash.find(model_hash);
            if (model != models_by_hash.end()) {
                choice.model_path = model->second;
            }
            const auto prop =
                g_level_prop_entity_templates.find(model_hash);
            if (prop != g_level_prop_entity_templates.end()) {
                const auto& donor = prop->second;
                if (!choice.info.entity_template) {
                    choice.info.entity_template = donor.template_hash;
                }
                if (!choice.info.transform_component_field) {
                    choice.info.transform_component_field =
                        donor.comp_field_hash;
                }
                if (!choice.info.transform_component_template) {
                    choice.info.transform_component_template =
                        donor.comp_template_hash;
                }
                if (!choice.info.physics_file_hash) {
                    choice.info.physics_file_hash = donor.physics_file_hash;
                }
            }
        }
        if (!choice.info.entity_template ||
            !choice.info.transform_component_field) {
            continue;
        }
        choice.label = container_spawn_label(
            !choice.model_path.empty() ? choice.model_path
                                       : choice.info.entity_name);
        char key[96];
        std::snprintf(key, sizeof(key), "%d:%08X:%08X",
                      choice.info.is_dig_spot ? 1 : 0,
                      choice.info.entity_template, model_hash);
        if (!seen.insert(key).second) continue;
        out.push_back(std::move(choice));
    }
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) {
                  if (a.info.is_dig_spot != b.info.is_dig_spot) {
                      return !a.info.is_dig_spot;
                  }
                  return a.label < b.label;
              });
    cached_level = g_pending_terrain_label;
    cached_contents = g_level_entity_contents.size();
    cached_models = S.all_mdl_files.size();
    cached = out;
    return cached;
}

static std::string quest_level_id_from_path(std::string path)
{
    std::replace(path.begin(), path.end(), '/', '\\');
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    const std::string root = "worlds\\albion\\";
    const size_t root_pos = lower.find(root);
    if (root_pos != std::string::npos) {
        path.erase(0, root_pos + root.size());
        lower.erase(0, root_pos + root.size());
    }
    const std::string suffix =
        "\\defaultscenario\\defaultscenario.engine_level";
    const size_t suffix_pos = lower.rfind(suffix);
    if (suffix_pos != std::string::npos) path.resize(suffix_pos);
    if (path.empty()) path = g_pending_terrain_label;
    return path;
}

static bool build_quest_level_reference(
    const LevelSpawnMarker& marker,
    size_t marker_index,
    QuestUI::LevelReferenceCandidate& candidate)
{
    candidate = QuestUI::LevelReferenceCandidate{};
    candidate.is_npc = marker.kind == 3;
    candidate.is_container = marker.is_container;
    candidate.level_path = g_pending_terrain_level_entry.full_path;
    candidate.level_id = quest_level_id_from_path(candidate.level_path);
    candidate.entity_name = marker.name;
    candidate.entity_hash = marker.entity_hash;
    candidate.x = marker.x;
    candidate.y = marker.y;
    candidate.z = marker.z;
    float position_delta[3] = {};
    float rotation_delta[3] = {};
    if (LevelEdit::EditFor(0x70000000u | uint32_t(marker_index),
                           position_delta, rotation_delta)) {
        candidate.x += position_delta[0];
        candidate.y += position_delta[1];
        candidate.z += position_delta[2];
    }
    candidate.model_hashes = marker.model_hashes;
    candidate.authored_instance = marker.pending_addition_index >= 0 &&
        LevelEdit::AdditionIsNamedEntity(
            marker.pending_addition_index);
    if (candidate.entity_name.empty()) {
        const auto contents =
            g_level_entity_contents.find(candidate.entity_hash);
        if (contents != g_level_entity_contents.end()) {
            candidate.entity_name = contents->second.entity_name;
        }
    }
    return !candidate.level_id.empty() &&
           !candidate.entity_name.empty() &&
           (candidate.entity_hash != 0 || candidate.authored_instance);
}

static std::string unique_static_prop_instance_name(
    const Gdb::CreatureCatalogEntry& entity)
{
    std::string base = "F2AB_Static_" + entity.name;
    for (char& c : base) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            c = '_';
        }
    }
    auto used = [&](const std::string& candidate) {
        for (const LevelSpawnMarker& marker : g_level_spawn_markers) {
            if (marker.name == candidate) return true;
        }
        std::vector<LevelEdit::Addition> additions;
        LevelEdit::GetAdditions(additions);
        return std::any_of(
            additions.begin(), additions.end(),
            [&](const LevelEdit::Addition& addition) {
                return !addition.removed &&
                       addition.entity_name == candidate;
            });
    };
    if (!used(base)) return base;
    for (unsigned int suffix = 2; suffix < 100000; ++suffix) {
        const std::string candidate =
            base + '_' + std::to_string(suffix);
        if (!used(candidate)) return candidate;
    }
    return base + "_New";
}

static int selected_level_spawn_marker_index()
{
    if (g_sel_spawn_marker >= 0 &&
        g_sel_spawn_marker < int(g_level_spawn_markers.size())) {
        return g_sel_spawn_marker;
    }
    if ((::g_selected_level_pick_id & 0xF0000000u) == 0x70000000u) {
        const size_t marker_index =
            size_t(::g_selected_level_pick_id & 0x0FFFFFFFu);
        if (marker_index < g_level_spawn_markers.size()) {
            return int(marker_index);
        }
    }
    if (::g_selected_level_mesh_idx < 0 ||
        ::g_selected_level_mesh_idx >= int(g_mp.meshes.size()) ||
        ::g_selected_level_pick_id == 0) {
        return -1;
    }
    const MPPerMesh& mesh =
        g_mp.meshes[size_t(::g_selected_level_mesh_idx)];
    uint32_t entity_hash = 0;
    for (const auto& range : mesh.pick_ranges) {
        if (range.selection_id != ::g_selected_level_pick_id) continue;
        entity_hash = range.gdb_entity_hash;
        break;
    }
    if (entity_hash == 0) return -1;
    for (size_t marker_index = 0;
         marker_index < g_level_spawn_markers.size(); ++marker_index) {
        if (g_level_spawn_markers[marker_index].entity_hash ==
            entity_hash) {
            return int(marker_index);
        }
    }
    return -1;
}

static void add_quest_item_to_container(uint32_t entity_hash,
                                        uint32_t item_hash)
{
    if (!entity_hash || !item_hash) return;
    std::vector<uint32_t> items;
    if (!LevelEdit::GetChestContents(entity_hash, items)) {
        const auto existing = g_level_entity_contents.find(entity_hash);
        if (existing != g_level_entity_contents.end()) {
            for (const Gdb::EntityContentsItem& item :
                 existing->second.initial_items) {
                items.push_back(item.record_hash);
            }
        }
    }
    if (std::find(items.begin(), items.end(), item_hash) == items.end()) {
        items.push_back(item_hash);
        LevelEdit::SetChestContents(entity_hash, items);
    }
}

static void draw_spawn_markers_overlay(const ImVec2& origin,
                                       const ImVec2& region,
                                       bool viewport_hovered)
{
    using namespace DirectX;
    const bool any_player_start =
        std::any_of(g_level_spawn_markers.begin(),
                    g_level_spawn_markers.end(), ::is_player_start_marker);
    if (!S.show_spawn_markers && !S.show_ent_npcs &&
        !S.show_dig_spots && !S.show_containers && !S.show_ent_text &&
        !any_player_start) {
        return;
    }
    if (!g_mp.no_tilt) return;
    if (g_sel_spawn_marker >= (int)g_level_spawn_markers.size()) {
        g_sel_spawn_marker = -1;
    }
    std::unordered_set<uint32_t> generator_spawn_points_pending_removal;
    for (const auto& marker : g_level_spawn_markers) {
        if (marker.kind == 1 &&
            LevelEdit::EntityRemovalPending(marker.entity_hash)) {
            generator_spawn_points_pending_removal.insert(
                marker.spawn_point_entities.begin(),
                marker.spawn_point_entities.end());
        }
    }
    if (g_sel_spawn_marker >= 0 &&
        (LevelEdit::EntityRemovalPending(
             g_level_spawn_markers[(size_t)g_sel_spawn_marker]
                 .entity_hash) ||
         generator_spawn_points_pending_removal.count(
             g_level_spawn_markers[(size_t)g_sel_spawn_marker]
                 .entity_hash))) {
        g_sel_spawn_marker = -1;
    }

    float cy = cosf(g_flycam.yaw);
    float sy = sinf(g_flycam.yaw);
    float cp = cosf(g_flycam.pitch);
    float sp = sinf(g_flycam.pitch);
    XMVECTOR eye = XMVectorSet(g_flycam.pos[0], g_flycam.pos[1],
                               g_flycam.pos[2], 1);
    XMVECTOR at = XMVectorSet(g_flycam.pos[0] + sy * cp,
                              g_flycam.pos[1] + sp,
                              g_flycam.pos[2] + cy * cp, 1);
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);
    XMMATRIX V = XMMatrixLookAtLH(eye, at, up);
    float fov = XMConvertToRadians(60.0f);
    float aspect = region.x / std::max(1.0f, region.y);
    float far_plane = std::max(g_mp.radius * 100.0f, 1000.0f);
    XMMATRIX P = XMMatrixPerspectiveFovLH(fov, aspect, 0.05f, far_plane);
    XMMATRIX VP = V * P;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 kCol[7] = {
        IM_COL32(255, 255, 255, 220),
        IM_COL32(255, 90, 90, 235),
        IM_COL32(255, 200, 80, 235),
        IM_COL32(120, 255, 140, 235),
        IM_COL32(85, 210, 255, 235),
        IM_COL32(220, 125, 255, 235),
        IM_COL32(90, 225, 225, 235),
    };

    size_t text_drawn = 0;
    for (const auto& kv : g_level_entity_text) {
        if (!S.show_ent_text) break;
        if (!kv.second.has_pos) continue;
        XMVECTOR clip = XMVector4Transform(
            XMVectorSet(kv.second.x, kv.second.z, kv.second.y, 1.0f),
            VP);
        const float w = XMVectorGetW(clip);
        if (w <= 0.05f) continue;
        const float ndcx = XMVectorGetX(clip) / w;
        const float ndcy = XMVectorGetY(clip) / w;
        if (ndcx < -1.1f || ndcx > 1.1f) continue;
        if (ndcy < -1.1f || ndcy > 1.1f) continue;
        ImVec2 pt;
        pt.x = origin.x + (ndcx * 0.5f + 0.5f) * region.x;
        pt.y = origin.y + (1.0f - (ndcy * 0.5f + 0.5f)) * region.y;
        const float r = 4.5f;
        dl->AddQuadFilled(ImVec2(pt.x, pt.y - r),
                          ImVec2(pt.x + r, pt.y),
                          ImVec2(pt.x, pt.y + r),
                          ImVec2(pt.x - r, pt.y),
                          IM_COL32(90, 170, 255, 235));
        if (w < 30.0f) {
            dl->AddText(ImVec2(pt.x + r + 3.0f, pt.y - 7.0f),
                        IM_COL32(160, 210, 255, 235), "text");
        }
        ++text_drawn;
    }
    (void)text_drawn;

    size_t drawn = 0;
    const bool can_pick = viewport_hovered && !LevelEdit::Saving() &&
                          !LevelGizmo::WantsMouse();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool context_clicked =
        ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    int click_hit = -1;
    bool overlay_click_hit = false;
    float click_best = 12.0f * 12.0f;
    uint32_t selected_model_entity_hash = 0;
    if (::g_selected_level_pick_id != 0) {
        for (const MPPerMesh& mesh : g_mp.meshes) {
            if (!mesh.is_entity_model) continue;
            for (const auto& range : mesh.pick_ranges) {
                if (range.selection_id == ::g_selected_level_pick_id) {
                    selected_model_entity_hash = range.gdb_entity_hash;
                    break;
                }
            }
            if (selected_model_entity_hash != 0) break;
        }
    }
    for (size_t mi = 0; mi < g_level_spawn_markers.size(); ++mi) {
        const auto& m = g_level_spawn_markers[mi];
        if (LevelEdit::EntityRemovalPending(m.entity_hash) ||
            generator_spawn_points_pending_removal.count(m.entity_hash)) {
            continue;
        }
        if (m.kind == 2 &&
            LevelEdit::SpawnPointRemovalPending(m.entity_hash)) {
            continue;
        }
        if (!level_marker_visible(m)) continue;
        float ex = m.x, ey = m.y, ez = m.z;
        {
            float d_pos[3], d_rot[3];
            if (LevelEdit::EditFor(0x70000000u | uint32_t(mi), d_pos,
                                   d_rot)) {
                ex += d_pos[0];
                ey += d_pos[1];
                ez += d_pos[2];
            }
        }
        XMVECTOR clip = XMVector4Transform(
            XMVectorSet(ex, ez, ey, 1.0f), VP);
        const float w = XMVectorGetW(clip);
        if (w <= 0.05f) continue;
        const float ndcx = XMVectorGetX(clip) / w;
        const float ndcy = XMVectorGetY(clip) / w;
        if (ndcx < -1.1f || ndcx > 1.1f) continue;
        if (ndcy < -1.1f || ndcy > 1.1f) continue;
        ImVec2 pt;
        pt.x = origin.x + (ndcx * 0.5f + 0.5f) * region.x;
        pt.y = origin.y + (1.0f - (ndcy * 0.5f + 0.5f)) * region.y;
        const bool player_start = ::is_player_start_marker(m);
        const ImU32 col = m.is_container && m.kind != 4
            ? kCol[5]
            : kCol[m.kind < 7 ? m.kind : 0];
        const float r = player_start ? 7.0f
                        : m.kind == 1 ? 6.0f
                                      : (m.is_container ? 5.5f : 4.5f);
        const bool model_owns_selection =
            (m.kind == 2 || m.kind == 3 || m.kind == 6) &&
            !m.model_hashes.empty();
        const bool selected = model_owns_selection
            ? (::g_selected_level_pick_id ==
                   (0x70000000u | uint32_t(mi)) ||
               (selected_model_entity_hash != 0 &&
                selected_model_entity_hash == m.entity_hash))
            : (int(mi) == g_sel_spawn_marker);
        if (player_start) {
            
            const ImU32 kFlag = IM_COL32(70, 230, 110, 245);
            ImVec2 top = pt;
            {
                XMVECTOR tclip = XMVector4Transform(
                    XMVectorSet(ex, ez + 2.2f, ey, 1.0f), VP);
                const float tw = XMVectorGetW(tclip);
                if (tw > 0.05f) {
                    const float tx = XMVectorGetX(tclip) / tw;
                    const float ty = XMVectorGetY(tclip) / tw;
                    top.x = origin.x + (tx * 0.5f + 0.5f) * region.x;
                    top.y = origin.y +
                            (1.0f - (ty * 0.5f + 0.5f)) * region.y;
                }
            }
            dl->AddCircleFilled(pt, 4.5f, kFlag);
            dl->AddCircle(pt, 5.5f,
                          selected ? IM_COL32(255, 255, 255, 255)
                                   : IM_COL32(0, 0, 0, 200),
                          0, selected ? 2.0f : 1.0f);
            dl->AddLine(pt, top, kFlag, 2.0f);
            const float fw = std::max(10.0f, (pt.y - top.y) * 0.35f);
            dl->AddTriangleFilled(
                top, ImVec2(top.x + fw, top.y + fw * 0.4f),
                ImVec2(top.x, top.y + fw * 0.8f), kFlag);
            dl->AddText(ImVec2(top.x + fw + 4.0f, top.y - 3.0f),
                        kFlag, "Player Start");
        } else {
            dl->AddQuadFilled(ImVec2(pt.x, pt.y - r),
                              ImVec2(pt.x + r, pt.y),
                              ImVec2(pt.x, pt.y + r),
                              ImVec2(pt.x - r, pt.y), col);
            dl->AddQuad(ImVec2(pt.x, pt.y - r - 1),
                        ImVec2(pt.x + r + 1, pt.y),
                        ImVec2(pt.x, pt.y + r + 1),
                        ImVec2(pt.x - r - 1, pt.y),
                        selected ? IM_COL32(255, 255, 255, 255)
                                 : IM_COL32(0, 0, 0, 200),
                        selected ? 2.0f : 1.0f);
            if ((w < 45.0f || selected) && !m.name.empty()) {
                dl->AddText(ImVec2(pt.x + r + 3.0f, pt.y - 7.0f),
                            IM_COL32(235, 235, 235, 235),
                            m.name.c_str());
            }
        }


        if (!model_owns_selection && can_pick &&
            (clicked || context_clicked)) {
            const float dx = mouse.x - pt.x;
            const float dy = mouse.y - pt.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < click_best) {
                click_best = d2;
                click_hit = int(mi);
            }
        }
        ++drawn;
    }
    if (click_hit >= 0) {
        overlay_click_hit = true;
        g_sel_spawn_marker = click_hit;
        g_marker_clear_selection = true;
    }

    if (S.show_spawn_markers) {
        std::vector<LevelEdit::GeneratorAddition> pending;
        LevelEdit::GetGenerators(pending);
        int gen_click = -1;
        float gen_best = 12.0f * 12.0f;
        for (size_t gi = 0; gi < pending.size(); ++gi) {
            const auto& pg = pending[gi];
            if (pg.removed) continue;
            XMVECTOR clip = XMVector4Transform(
                XMVectorSet(pg.pos[0], pg.pos[2], pg.pos[1], 1.0f),
                VP);
            const float w = XMVectorGetW(clip);
            if (w <= 0.05f) continue;
            const float ndcx = XMVectorGetX(clip) / w;
            const float ndcy = XMVectorGetY(clip) / w;
            if (ndcx < -1.1f || ndcx > 1.1f) continue;
            if (ndcy < -1.1f || ndcy > 1.1f) continue;
            ImVec2 pt;
            pt.x = origin.x + (ndcx * 0.5f + 0.5f) * region.x;
            pt.y = origin.y +
                   (1.0f - (ndcy * 0.5f + 0.5f)) * region.y;
            const float r = 6.0f;
            dl->AddQuadFilled(ImVec2(pt.x, pt.y - r),
                              ImVec2(pt.x + r, pt.y),
                              ImVec2(pt.x, pt.y + r),
                              ImVec2(pt.x - r, pt.y),
                              IM_COL32(200, 120, 255, 235));
            const bool gsel = (int(gi) == g_sel_pending_gen);
            dl->AddQuad(ImVec2(pt.x, pt.y - r - 1),
                        ImVec2(pt.x + r + 1, pt.y),
                        ImVec2(pt.x, pt.y + r + 1),
                        ImVec2(pt.x - r - 1, pt.y),
                        gsel ? IM_COL32(255, 255, 255, 255)
                             : IM_COL32(0, 0, 0, 200),
                        gsel ? 2.0f : 1.0f);
            const std::string lbl = "new: " + pg.creature_name;
            dl->AddText(ImVec2(pt.x + r + 3.0f, pt.y - 7.0f),
                        IM_COL32(225, 190, 255, 235), lbl.c_str());
            if (can_pick && clicked) {
                const float dx = mouse.x - pt.x;
                const float dy = mouse.y - pt.y;
                const float d2 = dx * dx + dy * dy;
                if (d2 < gen_best) {
                    gen_best = d2;
                    gen_click = int(gi);
                }
            }
        }
        if (gen_click >= 0) {
            overlay_click_hit = true;
            g_sel_pending_gen = gen_click;
            g_sel_pending_sp = -1;
            g_sel_spawn_marker = -1;
            g_marker_clear_selection = true;
        }

        std::vector<LevelEdit::PendingSpawnPoint> psps;
        LevelEdit::GetPendingSpawnPoints(psps);
        int sp_click = -1;
        float sp_best = 12.0f * 12.0f;
        for (const auto& sp : psps) {
            XMVECTOR clip = XMVector4Transform(
                XMVectorSet(sp.pos[0], sp.pos[2], sp.pos[1], 1.0f),
                VP);
            const float w = XMVectorGetW(clip);
            if (w <= 0.05f) continue;
            const float ndcx = XMVectorGetX(clip) / w;
            const float ndcy = XMVectorGetY(clip) / w;
            if (ndcx < -1.1f || ndcx > 1.1f) continue;
            if (ndcy < -1.1f || ndcy > 1.1f) continue;
            ImVec2 pt;
            pt.x = origin.x + (ndcx * 0.5f + 0.5f) * region.x;
            pt.y = origin.y +
                   (1.0f - (ndcy * 0.5f + 0.5f)) * region.y;
            const float r = 4.5f;
            dl->AddQuadFilled(ImVec2(pt.x, pt.y - r),
                              ImVec2(pt.x + r, pt.y),
                              ImVec2(pt.x, pt.y + r),
                              ImVec2(pt.x - r, pt.y),
                              IM_COL32(225, 160, 255, 235));
            const bool spsel = (sp.id == g_sel_pending_sp);
            dl->AddQuad(ImVec2(pt.x, pt.y - r - 1),
                        ImVec2(pt.x + r + 1, pt.y),
                        ImVec2(pt.x, pt.y + r + 1),
                        ImVec2(pt.x - r - 1, pt.y),
                        spsel ? IM_COL32(255, 255, 255, 255)
                              : IM_COL32(0, 0, 0, 200),
                        spsel ? 2.0f : 1.0f);
            if (w < 30.0f || spsel) {
                dl->AddText(ImVec2(pt.x + r + 3.0f, pt.y - 7.0f),
                            IM_COL32(235, 205, 255, 235),
                            sp.label.c_str());
            }
            if (can_pick && clicked) {
                const float dx = mouse.x - pt.x;
                const float dy = mouse.y - pt.y;
                const float d2 = dx * dx + dy * dy;
                if (d2 < sp_best) {
                    sp_best = d2;
                    sp_click = sp.id;
                }
            }
        }
        if (sp_click >= 0) {
            overlay_click_hit = true;
            g_sel_pending_sp = sp_click;
            g_sel_pending_gen = -1;
            g_sel_spawn_marker = -1;
            g_marker_clear_selection = true;
        } else if (click_hit >= 0) {
            g_sel_pending_sp = -1;
            g_sel_pending_gen = -1;
        }
    }
    if (can_pick && (clicked || context_clicked) && !overlay_click_hit) {
        g_sel_spawn_marker = -1;
        g_sel_pending_sp = -1;
        g_sel_pending_gen = -1;
    }
    if (drawn) {
        char buf[96];
        std::snprintf(buf, sizeof(buf),
                      "entity markers: %zu shown / %zu total", drawn,
                      g_level_spawn_markers.size());
        dl->AddText(ImVec2(origin.x + 14, origin.y + region.y - 38),
                    IM_COL32(220, 220, 220, 200), buf);
    }
}

#endif

void draw_skeleton_overlay(const ImVec2& origin, const ImVec2& region) {
    if (g_mp.bone_count == 0) return;

    std::vector<float> world_pose;
    MP_ComputeWorldPose(g_mp, S.bone_rot_deltas, world_pose);

    std::vector<ImVec2>  screen;
    std::vector<uint8_t> visible;
    project_bones_to_screen(world_pose, g_mp.bone_count, origin, region,
                            screen, visible);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 line_col   = IM_COL32(255, 220,  80, 220);
    const ImU32 dot_col    = IM_COL32(255, 240, 120, 255);
    const ImU32 sel_line   = IM_COL32(120, 220, 255, 255);
    const ImU32 sel_dot    = IM_COL32( 90, 240, 255, 255);

    const uint32_t n = g_mp.bone_count;

    for (uint32_t i = 0; i < n; ++i) {
        if (!visible[i]) continue;
        int pid = (i < g_mp.bone_parents.size()) ? g_mp.bone_parents[i] : -1;
        if (pid < 0 || pid >= (int)n) continue;
        if (!visible[(uint32_t)pid]) continue;
        bool sel = (S.selected_bone == (int)i || S.selected_bone == pid);
        dl->AddLine(screen[(uint32_t)pid], screen[i],
                    sel ? sel_line : line_col,
                    sel ? 2.0f : 1.5f);
    }

    for (uint32_t i = 0; i < n; ++i) {
        if (!visible[i]) continue;
        if ((int)i == S.selected_bone) {
            dl->AddCircleFilled(screen[i], 5.0f, sel_dot);
            dl->AddCircle      (screen[i], 7.5f, IM_COL32(0, 0, 0, 220), 0, 2.0f);
        } else {
            dl->AddCircleFilled(screen[i], 2.5f, dot_col);
        }
    }

    if (S.selected_bone >= 0 && S.selected_bone < (int)S.mdl_info.Bones.size()) {
        const std::string& bn = S.mdl_info.Bones[(size_t)S.selected_bone].Name;
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s  [%s]",
                      S.bone_rotate_mode ? "ROTATE" : "selected",
                      bn.c_str());
        ImVec2 ts = ImGui::CalcTextSize(buf);
        ImVec2 tp(origin.x + region.x - ts.x - 12.0f, origin.y + 8.0f);
        dl->AddRectFilled(ImVec2(tp.x - 6, tp.y - 4),
                          ImVec2(tp.x + ts.x + 6, tp.y + ts.y + 4),
                          IM_COL32(20, 22, 28, 200), 4.0f);
        dl->AddText(tp,
                    S.bone_rotate_mode ? IM_COL32(120, 220, 255, 255)
                                       : IM_COL32(220, 230, 240, 240),
                    buf);
    }
}

static int pick_bone_at(const ImVec2& mouse, const ImVec2& origin,
                        const ImVec2& region, float radius_px) {
    if (g_mp.bone_count == 0) return -1;
    std::vector<float> world_pose;
    MP_ComputeWorldPose(g_mp, S.bone_rot_deltas, world_pose);
    std::vector<ImVec2>  screen;
    std::vector<uint8_t> visible;
    project_bones_to_screen(world_pose, g_mp.bone_count, origin, region,
                            screen, visible);

    int   best = -1;
    float best_d2 = radius_px * radius_px;
    for (uint32_t i = 0; i < g_mp.bone_count; ++i) {
        if (!visible[i]) continue;
        float dx = screen[i].x - mouse.x;
        float dy = screen[i].y - mouse.y;
        float d2 = dx * dx + dy * dy;
        if (d2 < best_d2) { best_d2 = d2; best = (int)i; }
    }
    return best;
}

static void rp_quat_rot(const float q[4], const float v[3], float o[3]) {
    const float tx = 2.0f * (q[1]*v[2] - q[2]*v[1]);
    const float ty = 2.0f * (q[2]*v[0] - q[0]*v[2]);
    const float tz = 2.0f * (q[0]*v[1] - q[1]*v[0]);
    o[0] = v[0] + q[3]*tx + (q[1]*tz - q[2]*ty);
    o[1] = v[1] + q[3]*ty + (q[2]*tx - q[0]*tz);
    o[2] = v[2] + q[3]*tz + (q[0]*ty - q[1]*tx);
}

static void rp_quat_rot_inv(const float q[4], const float v[3], float o[3]) {
    const float qc[4] = { -q[0], -q[1], -q[2], q[3] };
    rp_quat_rot(qc, v, o);
}

static bool level_view_ray(const ImVec2& mouse,
                           const ImVec2& origin,
                           const ImVec2& region,
                           float out_origin[3],
                           float out_direction[3]) {
    const float fw = std::max(1.0f, region.x);
    const float fh = std::max(1.0f, region.y);
    const float mx = mouse.x - origin.x;
    const float my = mouse.y - origin.y;
    if (mx < 0.0f || my < 0.0f || mx > fw || my > fh) return false;

    const float fov      = 60.0f * 3.14159265f / 180.0f;
    const float aspect   = fw / fh;
    const float tan_half = std::tan(0.5f * fov);
    const float u_view   = (2.0f * mx / fw - 1.0f) * aspect * tan_half;
    const float v_view   = (1.0f - 2.0f * my / fh) * tan_half;

    const float cy = std::cos(g_flycam.yaw);
    const float sy = std::sin(g_flycam.yaw);
    const float cp = std::cos(g_flycam.pitch);
    const float sp = std::sin(g_flycam.pitch);
    const float fx = sy * cp,  fy = sp,  fz = cy * cp;
    const float rx = cy,       ry = 0.0f, rz = -sy;
    const float ux = -sp * sy, uy = cp,   uz = -sp * cy;

    float dx = rx * u_view + ux * v_view + fx;
    float dy = ry * u_view + uy * v_view + fy;
    float dz = rz * u_view + uz * v_view + fz;
    const float dlen = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dlen <= 1e-6f) return false;

    out_origin[0] = g_flycam.pos[0];
    out_origin[1] = g_flycam.pos[1];
    out_origin[2] = g_flycam.pos[2];
    out_direction[0] = dx / dlen;
    out_direction[1] = dy / dlen;
    out_direction[2] = dz / dlen;
    return true;
}

int pick_level_mesh_at(const ImVec2& mouse,
                       const ImVec2& origin,
                       const ImVec2& region,
                       uint32_t* out_pick_id = nullptr,
                       uint64_t* out_pick_hash = nullptr,
                       float* out_surface_distance = nullptr) {
    if (out_pick_id) *out_pick_id = 0;
    if (out_pick_hash) *out_pick_hash = 0;
    if (out_surface_distance) {
        *out_surface_distance = std::numeric_limits<float>::infinity();
    }
    if (!g_mp.has_model || !g_mp.no_tilt || g_mp.meshes.empty()) return -1;
    float ray_origin[3] = {};
    float ray_direction[3] = {};
    if (!level_view_ray(mouse, origin, region,
                        ray_origin, ray_direction)) return -1;
    const float ox = ray_origin[0];
    const float oy = ray_origin[1];
    const float oz = ray_origin[2];
    const float dx = ray_direction[0];
    const float dy = ray_direction[1];
    const float dz = ray_direction[2];

    auto hit_sphere = [&](const float center[3], float radius,
                          const float o[3], const float dv[3],
                          float& out_t) {
        const float lx = o[0] - center[0];
        const float ly = o[1] - center[1];
        const float lz = o[2] - center[2];
        const float l_dot_d = lx*dv[0] + ly*dv[1] + lz*dv[2];
        const float l_len2  = lx*lx + ly*ly + lz*lz;
        const float r2      = radius * radius;
        const float c       = l_len2 - r2;
        const float disc    = l_dot_d * l_dot_d - c;
        if (disc < 0.0f) return false;
        const float sq = std::sqrt(disc);
        float t = -l_dot_d - sq;
        if (t < 0.0f) t = -l_dot_d + sq;
        if (t < 0.0f) return false;
        out_t = t;
        return true;
    };

    auto hit_triangle = [&](const float* a,
                            const float* b,
                            const float* c,
                            const float ro[3],
                            const float rd[3],
                            float& out_t) {
        const float e1x = b[0] - a[0];
        const float e1y = b[1] - a[1];
        const float e1z = b[2] - a[2];
        const float e2x = c[0] - a[0];
        const float e2y = c[1] - a[1];
        const float e2z = c[2] - a[2];
        const float px = rd[1] * e2z - rd[2] * e2y;
        const float py = rd[2] * e2x - rd[0] * e2z;
        const float pz = rd[0] * e2y - rd[1] * e2x;
        const float det = e1x * px + e1y * py + e1z * pz;
        if (std::fabs(det) < 1e-7f) return false;
        const float inv_det = 1.0f / det;
        const float tx = ro[0] - a[0];
        const float ty = ro[1] - a[1];
        const float tz = ro[2] - a[2];
        const float u = (tx * px + ty * py + tz * pz) * inv_det;
        if (u < 0.0f || u > 1.0f) return false;
        const float qx = ty * e1z - tz * e1y;
        const float qy = tz * e1x - tx * e1z;
        const float qz = tx * e1y - ty * e1x;
        const float v = (rd[0] * qx + rd[1] * qy + rd[2] * qz) * inv_det;
        if (v < 0.0f || u + v > 1.0f) return false;
        const float t = (e2x * qx + e2y * qy + e2z * qz) * inv_det;
        if (t <= 0.0f) return false;
        out_t = t;
        return true;
    };

    int      best      = -1;
    uint32_t best_id   = 0;
    uint64_t best_hash = 0;
    float    best_t    = std::numeric_limits<float>::infinity();
    int      sph_best  = -1;
    float    sph_t     = std::numeric_limits<float>::infinity();
    for (size_t i = 0; i < g_mp.meshes.size(); ++i) {
        const auto& m = g_mp.meshes[i];
        if (m.index_count == 0 || m.radius <= 0.0f) continue;
        if (m.is_entity_model && !S.show_entity_models) continue;
        if (g_mp.selected_lod >= 0 &&
            m.lod_index != (uint32_t)g_mp.selected_lod) continue;
        if (m.is_terrain) continue;
        if (m.is_water)   continue;
        if (!S.dev_mode && is_adjacent_terrain_mesh_name(m.name)) continue;
        if (!m.pick_ranges.empty()) {
            for (const auto& pr : m.pick_ranges) {
                if (pr.selection_id == 0 || pr.radius <= 0.0f) continue;

                float ro[3] = { ox, oy, oz };
                float rd[3] = { dx, dy, dz };
                float tscale = 1.0f;
                auto eo = g_mp.range_edit_xforms.find(pr.selection_id);
                if (eo != g_mp.range_edit_xforms.end() &&
                    eo->second.deleted) continue;
                if (eo != g_mp.range_edit_xforms.end()) {
                    const LevelEdit::EditXform& x = eo->second;
                    const float rel[3] = {
                        ox - x.pivot[0] - x.off[0],
                        oy - x.pivot[1] - x.off[1],
                        oz - x.pivot[2] - x.off[2],
                    };
                    float rrel[3];
                    rp_quat_rot_inv(x.quat, rel, rrel);
                    const float inv_s =
                        x.scale > 1e-6f ? 1.0f / x.scale : 1.0f;
                    ro[0] = rrel[0] * inv_s + x.pivot[0];
                    ro[1] = rrel[1] * inv_s + x.pivot[1];
                    ro[2] = rrel[2] * inv_s + x.pivot[2];
                    const float dv[3] = { dx, dy, dz };
                    rp_quat_rot_inv(x.quat, dv, rd);
                    tscale = x.scale;
                }

                float sphere_t = 0.0f;
                if (!hit_sphere(pr.center, pr.radius, ro, rd, sphere_t)) {
                    continue;
                }

                bool tri_hit = false;
                float tri_t = std::numeric_limits<float>::infinity();
                if (!m.pick_positions.empty() && !m.pick_indices.empty()) {
                    const uint32_t end = std::min<uint32_t>(
                        pr.index_start + pr.index_count,
                        (uint32_t)m.pick_indices.size());
                    for (uint32_t k = pr.index_start; k + 2 < end; k += 3) {
                        const uint32_t ia = m.pick_indices[k + 0];
                        const uint32_t ib = m.pick_indices[k + 1];
                        const uint32_t ic = m.pick_indices[k + 2];
                        const size_t pa = (size_t)ia * 3;
                        const size_t pb = (size_t)ib * 3;
                        const size_t pc = (size_t)ic * 3;
                        if (pa + 2 >= m.pick_positions.size() ||
                            pb + 2 >= m.pick_positions.size() ||
                            pc + 2 >= m.pick_positions.size()) {
                            continue;
                        }
                        float t = 0.0f;
                        if (!hit_triangle(&m.pick_positions[pa],
                                          &m.pick_positions[pb],
                                          &m.pick_positions[pc],
                                          ro, rd, t)) {
                            continue;
                        }
                        if (t < tri_t) {
                            tri_t = t;
                            tri_hit = true;
                        }
                    }
                }

                if (tri_hit && tri_t * tscale < best_t) {
                    best_t = tri_t * tscale;
                    best = (int)i;
                    best_id = pr.selection_id;
                    best_hash = pr.inst_hash;
                }
            }
            continue;
        }
        if (m.name.rfind("engine_level:", 0) == 0) continue;
        if (m.edit_xform.deleted) continue;

        float t = 0.0f;
        float ctr[3] = { m.center[0], m.center[1], m.center[2] };
        float rr = m.radius;
        if (m.edit_xform.active()) {
            const LevelEdit::EditXform& x = m.edit_xform;
            const float rel[3] = {
                (m.center[0] - x.pivot[0]) * x.scale,
                (m.center[1] - x.pivot[1]) * x.scale,
                (m.center[2] - x.pivot[2]) * x.scale,
            };
            float rrel[3];
            rp_quat_rot(x.quat, rel, rrel);
            ctr[0] = rrel[0] + x.pivot[0] + x.off[0];
            ctr[1] = rrel[1] + x.pivot[1] + x.off[1];
            ctr[2] = rrel[2] + x.pivot[2] + x.off[2];
            rr = m.radius * x.scale;
        }
        const float o0[3] = { ox, oy, oz };
        const float d0[3] = { dx, dy, dz };
        if (!hit_sphere(ctr, rr, o0, d0, t)) continue;
        if (t < sph_t) {
            sph_t = t;
            sph_best = (int)i;
        }
    }
    if (best < 0 && sph_best >= 0) {
        best = sph_best;
        best_id = 0;
        best_hash = 0;
    }
    if (out_surface_distance && std::isfinite(best_t)) {
        *out_surface_distance = best_t;
    }
    if (out_pick_id) *out_pick_id = best_id;
    if (out_pick_hash) *out_pick_hash = best_hash;
    return best;
}

static bool level_placement_surface_at(const ImVec2& mouse,
                                       const ImVec2& origin,
                                       const ImVec2& region,
                                       bool allow_forward_fallback,
                                       float out_engine_pos[3]) {
    float ray_origin[3] = {};
    float ray_direction[3] = {};
    if (!level_view_ray(mouse, origin, region,
                        ray_origin, ray_direction)) return false;



    float object_t = std::numeric_limits<float>::infinity();
    pick_level_mesh_at(mouse, origin, region, nullptr, nullptr, &object_t);

    float terrain_hit[3] = {};
    float terrain_t = std::numeric_limits<float>::infinity();
    if (TerrainEdit::Raycast(
            ray_origin[0], ray_origin[1], ray_origin[2],
            ray_direction[0], ray_direction[1], ray_direction[2],
            terrain_hit[0], terrain_hit[1], terrain_hit[2])) {
        const float tx = terrain_hit[0] - ray_origin[0];
        const float ty = terrain_hit[1] - ray_origin[1];
        const float tz = terrain_hit[2] - ray_origin[2];
        terrain_t = tx * ray_direction[0] +
                    ty * ray_direction[1] +
                    tz * ray_direction[2];
        if (terrain_t <= 0.0f) {
            terrain_t = std::numeric_limits<float>::infinity();
        }
    }

    float preview_pos[3] = {};
    const float surface_t = std::min(object_t, terrain_t);
    if (std::isfinite(surface_t)) {
        preview_pos[0] = ray_origin[0] + ray_direction[0] * surface_t;
        preview_pos[1] = ray_origin[1] + ray_direction[1] * surface_t;
        preview_pos[2] = ray_origin[2] + ray_direction[2] * surface_t;
    } else if (std::fabs(ray_direction[1]) > 1e-4f) {
        const float t = (g_mp.center[1] - ray_origin[1]) /
                        ray_direction[1];
        if (t <= 0.0f) return false;
        preview_pos[0] = ray_origin[0] + ray_direction[0] * t;
        preview_pos[1] = ray_origin[1] + ray_direction[1] * t;
        preview_pos[2] = ray_origin[2] + ray_direction[2] * t;
    } else if (allow_forward_fallback) {
        preview_pos[0] = ray_origin[0] + ray_direction[0] * 10.0f;
        preview_pos[1] = ray_origin[1] + ray_direction[1] * 10.0f;
        preview_pos[2] = ray_origin[2] + ray_direction[2] * 10.0f;
    } else {
        return false;
    }


    out_engine_pos[0] = preview_pos[0];
    out_engine_pos[1] = preview_pos[2];
    out_engine_pos[2] = preview_pos[1];
    return true;
}



static bool level_water_surface_at(const ImVec2& mouse,
                                   const ImVec2& origin,
                                   const ImVec2& region,
                                   float out_engine_pos[3]) {
    float ray_origin[3] = {};
    float ray_direction[3] = {};
    if (!level_view_ray(mouse, origin, region, ray_origin,
                        ray_direction)) {
        return false;
    }
    float best_t = std::numeric_limits<float>::infinity();
    for (const auto& mesh : g_mp.meshes) {
        if (!mesh.is_water) continue;
        const float y = mesh.water_params[0];
        if (std::fabs(ray_direction[1]) < 1e-5f) continue;
        const float t = (y - ray_origin[1]) / ray_direction[1];
        if (t <= 0.0f || t >= best_t) continue;
        best_t = t;
    }
    if (!std::isfinite(best_t)) return false;
    const float px = ray_origin[0] + ray_direction[0] * best_t;
    const float py = ray_origin[1] + ray_direction[1] * best_t;
    const float pz = ray_origin[2] + ray_direction[2] * best_t;
    out_engine_pos[0] = px;
    out_engine_pos[1] = pz;
    out_engine_pos[2] = py;
    return true;
}

void draw_model_in_panel(ID3D11Device* device) {
    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    int w = std::max(1, (int)region.x);
    int h = std::max(1, (int)region.y);

    if (::g_selected_level_mesh_idx >= (int)g_mp.meshes.size() ||
        !g_mp.has_model || !g_mp.no_tilt) {
        ::g_selected_level_mesh_idx = -1;
        ::g_selected_level_pick_id = 0;
        ::g_selected_level_hash = 0;
    }
    if (::g_selected_level_mesh_idx >= 0 && !S.dev_mode &&
        is_adjacent_terrain_mesh_name(
            g_mp.meshes[(size_t)::g_selected_level_mesh_idx].name))
    {
        ::g_selected_level_mesh_idx = -1;
        ::g_selected_level_pick_id = 0;
        ::g_selected_level_hash = 0;
    }

    {
        LevelEdit::CollectPreviewXforms(g_mp.range_edit_xforms);
        for (size_t i = 0; i < g_mp.meshes.size(); ++i) {
            auto& mm = g_mp.meshes[i];
            mm.edit_xform = LevelEdit::EditXform{};
            auto it = g_mp.range_edit_xforms.find(
                0x80000000u | (uint32_t)i);
            if (it != g_mp.range_edit_xforms.end()) {
                mm.edit_xform = it->second;
            }
        }
    }

    if (!g_mp_initialized) {
        MP_Init(device, g_mp, w, h);
        g_mp_initialized = true;
    }
    MP_Resize(device, g_mp, w, h);

    ImGui::InvisibleButton("##model_render", region);
    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();

#ifdef _WIN32
    if (g_mp.no_tilt && LevelEdit::Enabled() &&
        ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pay =
                ImGui::AcceptDragDropPayload("F2_MODEL")) {
            const std::string drop_model(
                (const char*)pay->Data, (size_t)pay->DataSize);
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            float engine_pos[3] = {};
            if (level_placement_surface_at(mouse, origin, region, false,
                                           engine_pos)) {
                DebugTrace::log(
                    "drop: '%s' at (%.2f, %.2f, %.2f)",
                    drop_model.c_str(), engine_pos[0],
                    engine_pos[1], engine_pos[2]);
                spawn_level_model_at(device, drop_model, engine_pos);
            }
        }
        if (const ImGuiPayload* pay =
                ImGui::AcceptDragDropPayload("F2_ENTITY_NPC")) {
            int catalog_index = -1;
            if (pay->DataSize == (int)sizeof(int)) {
                std::memcpy(&catalog_index, pay->Data, sizeof(int));
            }
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            float engine_pos[3] = {};
            if (catalog_index >= 0 &&
                catalog_index < (int)g_global_entity_catalog.size() &&
                level_placement_surface_at(mouse, origin, region, false,
                                           engine_pos)) {
                const Gdb::CreatureCatalogEntry& entry =
                    g_global_entity_catalog[(size_t)catalog_index];
                LevelEdit::NpcPlacementInfo info;
                static int s_placed_serial = 0;
                info.instance_name =
                    entry.name + "_placed" +
                    std::to_string(++s_placed_serial);
                info.creature_name = entry.name;
                info.creature_entity = entry.entity_hash;
                info.transform_component_field =
                    entry.transform_component_field
                        ? entry.transform_component_field
                        : g_level_npc_donor.transform_field;
                info.transform_component_template =
                    entry.transform_component_template
                        ? entry.transform_component_template
                        : g_level_npc_donor.transform_parent;
                info.position_template =
                    entry.position_template
                        ? entry.position_template
                        : g_level_npc_donor.position_parent;
                info.rotation_template =
                    entry.rotation_template
                        ? entry.rotation_template
                        : g_level_npc_donor.rotation_parent;
                for (uint32_t model_hash : entry.model_hashes) {
                    const FlatAssetEntry* model =
                        FindGlobalModelAssetByPathHash(model_hash);
                    if (model) {
                        info.asset_models.push_back(model->full_path);
                    }
                }
                const int addition =
                    LevelEdit::AddNpcPlacement(engine_pos, info);
                if (addition >= 0) {
                    LevelSpawnMarker marker;
                    marker.x = engine_pos[0];
                    marker.y = engine_pos[1];
                    marker.z = engine_pos[2];
                    marker.kind = 3;
                    marker.pending_addition_index = addition;
                    marker.name = info.instance_name;
                    marker.creature_name = entry.name;
                    marker.creature_entity_hash = entry.entity_hash;
                    marker.model_hashes = entry.model_hashes;
                    g_level_spawn_markers.push_back(marker);
                    const size_t marker_index =
                        g_level_spawn_markers.size() - 1;
                    append_level_entity_model_at(
                        device, entry.model_hashes, marker_index,
                        engine_pos);
                    UI::select_level_marker(marker_index);
                    S.show_ent_npcs = true;
                    S.show_entity_models = true;
                    OutputLog::success("placed " + entry.name +
                                       " in the level");
                } else {
                    OutputLog::error(
                        "could not place " + entry.name +
                        " (is level editing enabled?)");
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    static bool  s_gen_popup = false;
    static bool  s_gen_click_pending = false;
    static bool  s_gen_on_water = false;
    static float s_gen_pos[3] = {0, 0, 0};
    static ImVec2 s_gen_click_mouse{};
    static char  s_add_filter[128] = {};
    if (g_add_menu_requested) {
        for (int i = 0; i < 3; ++i) {
            s_gen_pos[i] = g_add_menu_requested_pos[i];
        }
        g_add_menu_requested = false;
        s_gen_on_water = false;
        s_gen_popup = true;
    }
    if (g_mp.no_tilt && LevelEdit::Enabled() && !LevelEdit::Saving() &&
        hovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        s_gen_click_pending = false;
        const ImVec2 mouse2 = ImGui::GetIO().MousePos;
        s_gen_click_mouse = mouse2;
        float engine_pos[3] = {};
        const bool land_hit = level_placement_surface_at(
            mouse2, origin, region, true, engine_pos);
        float water_pos[3] = {};
        const bool water_hit =
            level_water_surface_at(mouse2, origin, region, water_pos);
        
        
        if (water_hit &&
            (!land_hit || water_pos[2] > engine_pos[2] + 0.01f)) {
            s_gen_pos[0] = water_pos[0];
            s_gen_pos[1] = water_pos[1];
            s_gen_pos[2] = water_pos[2];
            s_gen_on_water = true;
            s_gen_click_pending = true;
        } else if (land_hit) {
            s_gen_pos[0] = engine_pos[0];
            s_gen_pos[1] = engine_pos[1];
            s_gen_pos[2] = engine_pos[2];
            s_gen_on_water = false;
            s_gen_click_pending = true;
        }
    }
    if (s_gen_click_pending &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        const ImVec2 released_at = ImGui::GetIO().MousePos;
        const float release_dx = released_at.x - s_gen_click_mouse.x;
        const float release_dy = released_at.y - s_gen_click_mouse.y;
        const float release_distance = std::sqrt(
            release_dx * release_dx + release_dy * release_dy);
        if (std::max(g_flycam.right_drag_distance,
                     release_distance) < 4.0f) {
            uint32_t picked_id = 0;
            uint64_t picked_hash = 0;
            const int picked = pick_level_mesh_at(
                released_at, origin, region, &picked_id, &picked_hash);
            if (picked >= 0) {
                ::g_selected_level_mesh_idx = picked;
                ::g_selected_level_pick_id = picked_id;
                ::g_selected_level_hash = picked_hash;
                if (g_mp.meshes[size_t(picked)].is_entity_model) {
                    g_sel_spawn_marker = -1;
                    g_sel_pending_sp = -1;
                    g_sel_pending_gen = -1;
                }
            }
            s_gen_popup = true;
        }
        s_gen_click_pending = false;
    }
    if (s_gen_popup) {
        ImGui::OpenPopup("Add to level");
        s_gen_popup = false;
        s_add_filter[0] = 0;
    }
    const ImGuiIO& add_io = ImGui::GetIO();
    ImGui::SetNextWindowSize(
        ImVec2(std::min(500.0f, std::max(300.0f, add_io.DisplaySize.x - 32.0f)),
               std::min(620.0f, std::max(280.0f, add_io.DisplaySize.y - 32.0f))),
        ImGuiCond_Appearing);
    if (ImGui::BeginPopup("Add to level")) {
        ImGui::TextDisabled("(%.1f, %.1f, %.1f)", s_gen_pos[0],
                            s_gen_pos[1], s_gen_pos[2]);
        auto place_generator = [&](const std::string& creature) {
            std::vector<std::string> assets;
            uint32_t creature_entity = 0;
            for (const auto& ce : g_level_creature_catalog) {
                if (ce.name != creature) continue;
                creature_entity = ce.entity_hash;
                for (uint32_t mh : ce.model_hashes) {
                    for (const auto& mf : S.all_mdl_files) {
                        std::string lp = mf.full_path;
                        std::transform(lp.begin(), lp.end(), lp.begin(),
                                       ::tolower);
                        std::replace(lp.begin(), lp.end(), '/', '\\');
                        uint32_t h = 0x811C9DC5u;
                        for (unsigned char c : lp) {
                            h *= 0x01000193u;
                            h ^= uint32_t(c);
                        }
                        if (h == mh) {
                            assets.push_back(mf.full_path);
                            break;
                        }
                    }
                }
                break;
            }
            const int gi =
                LevelEdit::AddGenerator(s_gen_pos, creature,
                                        creature_entity, assets);
            if (gi >= 0) {
                OutputLog::success(
                    "level edit: generator for '" + creature +
                    "' queued (" + std::to_string(assets.size()) +
                    " creature model(s) will stream into this level "
                    "on Save)");
            }
        };
        auto place_static_entity = [&](size_t catalog_index) {
            if (catalog_index >= g_global_entity_catalog.size()) return;
            const Gdb::CreatureCatalogEntry& entity =
                g_global_entity_catalog[catalog_index];
            if (entity.kind != Gdb::EntityCatalogKind::StaticProp ||
                entity.model_hashes.empty()) {
                return;
            }
            const FlatAssetEntry* model =
                FindGlobalModelAssetByPathHash(entity.model_hashes.front());
            if (!model) {
                OutputLog::error(
                    "level edit: static prop model is not available");
                return;
            }
            const int addition =
                LevelEdit::AddPlacement(model->full_path, s_gen_pos);
            if (addition < 0) {
                OutputLog::error(
                    "level edit: static prop placement rejected");
                return;
            }
            LevelEdit::StaticPropPlacementInfo info;
            info.instance_name =
                unique_static_prop_instance_name(entity);
            info.entity_template = entity.entity_hash;
            info.transform_component_field =
                entity.transform_component_field;
            info.transform_component_template =
                entity.transform_component_template;
            info.position_template = entity.position_template;
            info.rotation_template = entity.rotation_template;
            LevelEdit::MarkAdditionAsStaticProp(addition, info);

            LevelSpawnMarker marker;
            marker.x = s_gen_pos[0];
            marker.y = s_gen_pos[1];
            marker.z = s_gen_pos[2];
            marker.kind = 6;
            marker.pending_addition_index = addition;
            marker.name = info.instance_name;
            marker.model_hashes = entity.model_hashes;
            g_level_spawn_markers.push_back(std::move(marker));
            const size_t marker_index =
                g_level_spawn_markers.size() - 1;
            if (!append_level_entity_model_at(
                    device, entity.model_hashes, marker_index,
                    s_gen_pos)) {
                OutputLog::warn(
                    "level edit: static prop was queued, but its preview "
                    "model could not be drawn");
            }
            UI::select_level_marker(marker_index);
            S.show_entity_models = true;
            OutputLog::success(
                "level edit: static prop '" + info.instance_name +
                "' queued as a named, behaviour-free entity");
        };

        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##add_to_level_search",
                                 "Search actors, objects, containers...",
                                 s_add_filter, sizeof(s_add_filter));

        std::string filter = s_add_filter;
        std::transform(filter.begin(), filter.end(), filter.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        auto matches_filter = [&](const std::string& value) {
            if (filter.empty()) return true;
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) {
                               return (char)std::tolower(c);
                           });
            return lower.find(filter) != std::string::npos;
        };

        std::vector<std::string> actor_names;
        std::unordered_set<std::string> seen_actor_names;
        auto add_actor = [&](const std::string& name) {
            if (name.empty() || !seen_actor_names.insert(name).second ||
                !matches_filter(name)) return;
            actor_names.push_back(name);
        };
        for (const auto& creature : g_level_creature_catalog) {
            add_actor(creature.name);
        }
        for (const auto& marker : g_level_spawn_markers) {
            if (marker.kind != 1 && marker.kind != 3) continue;
            add_actor(marker.kind == 1 ? marker.creature_name : marker.name);
        }

        std::vector<size_t> static_entity_indices;
        for (size_t i = 0; i < g_global_entity_catalog.size(); ++i) {
            const Gdb::CreatureCatalogEntry& entity =
                g_global_entity_catalog[i];
            if (entity.kind != Gdb::EntityCatalogKind::StaticProp) continue;
            std::string searchable = entity.display_name + " " +
                                     entity.name;
            if (!entity.model_hashes.empty()) {
                const FlatAssetEntry* model =
                    FindGlobalModelAssetByPathHash(
                        entity.model_hashes.front());
                if (model) searchable += " " + model->full_path;
            }
            if (matches_filter(searchable)) {
                static_entity_indices.push_back(i);
            }
        }

        std::vector<size_t> object_indices;
        object_indices.reserve(S.all_mdl_files.size());
        for (size_t i = 0; i < S.all_mdl_files.size(); ++i) {
            const auto& model = S.all_mdl_files[i];
            if (matches_filter(model.full_path) ||
                matches_filter(model.name)) {
                object_indices.push_back(i);
            }
        }

        const auto container_choices = build_container_spawn_choices();
        std::vector<size_t> container_indices;
        std::vector<size_t> dig_indices;
        std::vector<size_t> dive_indices;
        for (size_t i = 0; i < container_choices.size(); ++i) {
            if (!matches_filter(container_choices[i].label)) continue;
            if (container_choices[i].is_dive) {
                dive_indices.push_back(i);
            } else if (container_choices[i].info.is_dig_spot) {
                dig_indices.push_back(i);
            } else {
                container_indices.push_back(i);
            }
        }
        
        
        if (s_gen_on_water) {
            actor_names.clear();
            object_indices.clear();
            container_indices.clear();
            dig_indices.clear();
        } else {
            dive_indices.clear();
        }

        ImGui::Separator();
        ImGui::BeginChild("##add_to_level_flat_list", ImVec2(0.0f, 0.0f),
                          false, ImGuiWindowFlags_HorizontalScrollbar);
        bool any_result = false;
        const ImVec4 heading_colour(0.55f, 0.75f, 1.0f, 1.0f);

        auto draw_heading = [&](const char* title, size_t count) {
            ImGui::TextColored(heading_colour, "%s  (%zu)", title, count);
        };
        QuestUI::NpcCreationRequest npc_request;
        if (!s_gen_on_water &&
            QuestUI::GetPendingNpcCreation(npc_request)) {
            any_result = true;
            draw_heading("QUEST NPC", 1);
            const bool can_author_npc = g_level_npc_donor.valid();
            ImGui::BeginDisabled(!can_author_npc);
            const std::string action =
                "Create " + npc_request.display_name + " here";
            if (ImGui::Selectable(action.c_str())) {
                LevelEdit::NpcPlacementInfo info;
                info.instance_name = npc_request.instance_name;
                info.creature_name = npc_request.creature_name;
                info.creature_entity = npc_request.creature_entity;
                info.transform_component_field =
                    g_level_npc_donor.transform_field;
                info.transform_component_template =
                    g_level_npc_donor.transform_parent;
                info.position_template =
                    g_level_npc_donor.position_parent;
                info.rotation_template =
                    g_level_npc_donor.rotation_parent;
                for (uint32_t model_hash : npc_request.model_hashes) {
                    const FlatAssetEntry* model =
                        FindGlobalModelAssetByPathHash(model_hash);
                    if (model) info.asset_models.push_back(model->full_path);
                }
                const int addition =
                    LevelEdit::AddNpcPlacement(s_gen_pos, info);
                if (addition >= 0) {
                    LevelSpawnMarker marker;
                    marker.x = s_gen_pos[0];
                    marker.y = s_gen_pos[1];
                    marker.z = s_gen_pos[2];
                    marker.kind = 3;
                    marker.pending_addition_index = addition;
                    marker.name = npc_request.instance_name;
                    marker.creature_name = npc_request.creature_name;
                    marker.creature_entity_hash =
                        npc_request.creature_entity;
                    marker.model_hashes = npc_request.model_hashes;
                    g_level_spawn_markers.push_back(marker);
                    const size_t marker_index =
                        g_level_spawn_markers.size() - 1;
                    append_level_entity_model_at(
                        device, npc_request.model_hashes, marker_index,
                        s_gen_pos);
                    UI::select_level_marker(marker_index);
                    S.show_ent_npcs = true;
                    S.show_entity_models = true;

                    QuestUI::LevelReferenceCandidate candidate;
                    candidate.is_npc = true;
                    candidate.authored_instance = true;
                    candidate.level_path =
                        g_pending_terrain_level_entry.full_path;
                    candidate.level_id = quest_level_id_from_path(
                        candidate.level_path);
                    candidate.entity_name = npc_request.instance_name;
                    candidate.x = s_gen_pos[0];
                    candidate.y = s_gen_pos[1];
                    candidate.z = s_gen_pos[2];
                    candidate.model_hashes = npc_request.model_hashes;
                    std::string error;
                    if (QuestUI::BindCreatedNpcInstance(candidate, error)) {
                        OutputLog::success(
                            "quest NPC: created '" +
                            npc_request.instance_name + "' in " +
                            candidate.level_id +
                            "; Save Level will inject it");
                        ImGui::CloseCurrentPopup();
                    } else {
                        OutputLog::error("quest NPC: " + error);
                    }
                } else {
                    OutputLog::error(
                        "quest NPC: level rejected the NPC placement");
                }
            }
            ImGui::EndDisabled();
            if (!can_author_npc) {
                ImGui::TextDisabled(
                    "No placed-NPC transform schema is available yet; "
                    "load a level containing an NPC first.");
            }
            ImGui::Separator();
        }
        const int quest_reference_marker =
            selected_level_spawn_marker_index();
        if (QuestUI::IsAuthoredQuestActive() &&
            quest_reference_marker >= 0) {
            const LevelSpawnMarker& marker =
                g_level_spawn_markers[static_cast<std::size_t>(
                    quest_reference_marker)];
            const QuestUI::LevelReferenceTarget target =
                QuestUI::PendingLevelReferenceTarget();
            
            const bool compatible =
                BlueprintUI::PendingPickPin() != 0 ||
                (target == QuestUI::LevelReferenceTarget::QuestGiver &&
                 marker.kind == 3) ||
                (target != QuestUI::LevelReferenceTarget::QuestGiver &&
                 marker.is_container);
            const std::string action =
                "Assign selected " + QuestUI::PendingLevelReferenceLabel();
            if (compatible && matches_filter(action)) {
                any_result = true;
                draw_heading("QUEST REFERENCES", 1);
                const bool existing_entity = marker.entity_hash != 0 &&
                                             marker.pending_addition_index < 0;
                const bool authored_entity =
                    marker.pending_addition_index >= 0 &&
                    LevelEdit::AdditionIsNamedEntity(
                        marker.pending_addition_index);
                ImGui::BeginDisabled(
                    !existing_entity && !authored_entity);
                if (ImGui::Selectable(action.c_str())) {
                    QuestUI::LevelReferenceCandidate candidate;
                    std::string error;
                    if (!build_quest_level_reference(
                            marker, size_t(quest_reference_marker),
                            candidate)) {
                        error = "The selected entity has no usable GDB name.";
                    } else if (QuestUI::BindActiveLevelReference(candidate,
                                                                  error)) {
                        if (target !=
                            QuestUI::LevelReferenceTarget::QuestGiver) {
                            add_quest_item_to_container(
                                marker.entity_hash,
                                QuestUI::PendingLevelReferenceItemHash());
                        }
                        OutputLog::success(
                            "quest reference: " + action + " in " +
                            candidate.level_id);
                        ImGui::CloseCurrentPopup();
                    }
                    if (!error.empty()) {
                        OutputLog::error("quest reference: " + error);
                    }
                }
                ImGui::EndDisabled();
                if (!existing_entity && !authored_entity) {
                    ImGui::TextDisabled(
                        "This newly placed object is not a named entity.");
                }
            }
        }
        if (!static_entity_indices.empty()) {
            if (any_result) ImGui::Separator();
            any_result = true;
            draw_heading("STATIC ENTITIES",
                         static_entity_indices.size());
            ImGui::PushID("static_entities");
            for (size_t row = 0; row < static_entity_indices.size(); ++row) {
                const size_t catalog_index =
                    static_entity_indices[row];
                const Gdb::CreatureCatalogEntry& entity =
                    g_global_entity_catalog[catalog_index];
                const std::string& label = entity.display_name.empty()
                    ? entity.name : entity.display_name;
                ImGui::PushID(static_cast<int>(catalog_index));
                if (ImGui::Selectable(label.c_str())) {
                    place_static_entity(catalog_index);
                    ImGui::CloseCurrentPopup();
                }
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(entity.name.c_str());
                    if (!entity.model_hashes.empty()) {
                        const FlatAssetEntry* model =
                            FindGlobalModelAssetByPathHash(
                                entity.model_hashes.front());
                        if (model) {
                            ImGui::TextDisabled(
                                "%s", model->full_path.c_str());
                        }
                    }
                    ImGui::TextDisabled(
                        "Named static prop; no behaviours");
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        if (!actor_names.empty()) {
            any_result = true;
            draw_heading("ACTORS", actor_names.size());
            ImGui::PushID("actors");
            ImGuiListClipper clipper;
            clipper.Begin((int)actor_names.size());
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart;
                     i < clipper.DisplayEnd; ++i) {
                    ImGui::PushID(i);
                    if (ImGui::Selectable(actor_names[(size_t)i].c_str())) {
                        place_generator(actor_names[(size_t)i]);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopID();
                }
            }
            clipper.End();
            ImGui::PopID();
        }

        if (!object_indices.empty()) {
            if (any_result) ImGui::Separator();
            any_result = true;
            draw_heading("OBJECTS", object_indices.size());
            ImGui::PushID("objects");
            ImGuiListClipper clipper;
            clipper.Begin((int)object_indices.size());
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart;
                     row < clipper.DisplayEnd; ++row) {
                    const size_t model_index = object_indices[(size_t)row];
                    const auto& model = S.all_mdl_files[model_index];
                    const std::string label = clean_level_model_name(
                        model.name.empty() ? model.full_path : model.name);
                    ImGui::PushID((int)model_index);
                    if (ImGui::Selectable(label.c_str())) {
                        spawn_level_model_at(device, model.full_path,
                                             s_gen_pos);
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
            ImGui::PopID();
        }

        auto place_container_choice = [&](size_t choice_index) {
            const auto& choice = container_choices[choice_index];
            const int add_idx = spawn_level_container_at(
                device, choice.model_path, s_gen_pos, choice.info);
            if (add_idx >= 0) {
                if (choice.info.is_dig_spot) {
                    S.show_dig_spots = true;
                } else {
                    S.show_containers = true;
                }
                if (choice.model_path.empty()) {
                    LevelSpawnMarker marker;
                    marker.x = s_gen_pos[0];
                    marker.y = s_gen_pos[1];
                    marker.z = s_gen_pos[2];
                    marker.kind = choice.info.is_dig_spot ? 4 : 5;
                    marker.is_container = true;
                    marker.pending_addition_index = add_idx;
                    marker.name = choice.label;
                    g_level_spawn_markers.push_back(std::move(marker));
                    UI::select_level_marker(g_level_spawn_markers.size() - 1);
                }
            }
            ImGui::CloseCurrentPopup();
        };
        auto draw_container_section = [&](const char* title,
                                          const std::vector<size_t>& rows) {
            if (rows.empty()) return;
            if (any_result) ImGui::Separator();
            any_result = true;
            draw_heading(title, rows.size());
            ImGui::PushID(title);
            for (size_t row = 0; row < rows.size(); ++row) {
                const size_t choice_index = rows[row];
                ImGui::PushID((int)choice_index);
                if (ImGui::Selectable(
                        container_choices[choice_index].label.c_str())) {
                    place_container_choice(choice_index);
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        };
        draw_container_section("CONTAINERS", container_indices);
        draw_container_section("DIG SPOTS", dig_indices);
        draw_container_section("DIVE SPOTS", dive_indices);

        if (!any_result) {
            ImGui::TextDisabled("No matching placeable items.");
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
#endif

    bool skel_visible = ::g_skel_overlay_show && (g_mp.bone_count > 0);

    static float s_rot_snapshot[4]    = {0, 0, 0, 1};
    static int   s_rot_snapshot_bone  = -1;
    static bool  s_rot_snapshot_valid = false;

    auto cancel_rotate = [&]() {
        if (s_rot_snapshot_valid &&
            s_rot_snapshot_bone >= 0 &&
            s_rot_snapshot_bone < (int)g_mp.bone_count &&
            (size_t)s_rot_snapshot_bone * 4 + 4 <= S.bone_rot_deltas.size()) {
            for (int k = 0; k < 4; ++k) {
                S.bone_rot_deltas[(size_t)s_rot_snapshot_bone * 4 + (size_t)k]
                    = s_rot_snapshot[k];
            }
        }
        s_rot_snapshot_valid = false;
        S.bone_rotate_mode   = false;
    };
    auto confirm_rotate = [&]() {
        s_rot_snapshot_valid = false;
        S.bone_rotate_mode   = false;
    };

    bool rotate_active = (skel_visible && S.bone_rotate_mode &&
                          S.selected_bone >= 0 &&
                          S.selected_bone < (int)g_mp.bone_count);

    if (rotate_active) {

        if (hovered) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            if (d.x != 0.0f || d.y != 0.0f) {
                const float kRotSensitivity = 0.01f;
                float a_y = d.x * kRotSensitivity;
                float a_x = d.y * kRotSensitivity;

                using namespace DirectX;
                XMVECTOR qx = XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), a_x);
                XMVECTOR qy = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), a_y);
                XMVECTOR delta = XMQuaternionMultiply(qx, qy);

                int b = S.selected_bone;
                XMVECTOR cur = XMVectorSet(
                    S.bone_rot_deltas[(size_t)b * 4 + 0],
                    S.bone_rot_deltas[(size_t)b * 4 + 1],
                    S.bone_rot_deltas[(size_t)b * 4 + 2],
                    S.bone_rot_deltas[(size_t)b * 4 + 3]);

                XMVECTOR nxt = XMQuaternionNormalize(XMQuaternionMultiply(cur, delta));
                XMFLOAT4 nf;
                XMStoreFloat4(&nf, nxt);
                S.bone_rot_deltas[(size_t)b * 4 + 0] = nf.x;
                S.bone_rot_deltas[(size_t)b * 4 + 1] = nf.y;
                S.bone_rot_deltas[(size_t)b * 4 + 2] = nf.z;
                S.bone_rot_deltas[(size_t)b * 4 + 3] = nf.w;
            }
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            cancel_rotate();
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            confirm_rotate();
        }
    }

    if (skel_visible && !rotate_active && hovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImVec2 mp = ImGui::GetIO().MousePos;
        int picked = pick_bone_at(mp, origin, region, 12.0f);
        S.selected_bone = picked;
    }

    
    
    const bool sculpt_click_owns_mouse =
        details_panel_docked() &&
        ((LandscapePanel::InSculptMode() && TerrainEdit::IsLoaded()) ||
         (LandscapePanel::InPaintMode() && TerrainPaint::Active()));
    if (g_mp.no_tilt && hovered && !rotate_active &&
        !sculpt_click_owns_mouse &&
        !LevelGizmo::WantsMouse() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f))
    {
        uint32_t picked_id = 0;
        uint64_t picked_hash = 0;
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        DebugTrace::log("pick: begin mouse=(%.0f,%.0f) meshes=%zu edit=%d",
                        mouse.x, mouse.y, g_mp.meshes.size(),
                        LevelEdit::Enabled() ? 1 : 0);
        const int picked = pick_level_mesh_at(mouse, origin, region,
                                              &picked_id, &picked_hash);
        DebugTrace::log("pick: done mesh=%d id=%u hash=%llu name='%s'",
                        picked, picked_id,
                        (unsigned long long)picked_hash,
                        picked >= 0
                            ? g_mp.meshes[(size_t)picked].name.c_str()
                            : "");
        ::g_selected_level_mesh_idx = picked;
        ::g_selected_level_pick_id = picked >= 0 ? picked_id : 0;
        ::g_selected_level_hash = picked >= 0 ? picked_hash : 0;
        if (picked >= 0 &&
            g_mp.meshes[static_cast<size_t>(picked)].is_entity_model) {


            g_sel_spawn_marker = -1;
            g_sel_pending_sp = -1;
            g_sel_pending_gen = -1;
        }
        if (picked < 0) LevelGizmo::CancelDrag();
    }

    if (S.terrain_mode) {
        const float dt = ImGui::GetIO().DeltaTime;
        if (hovered || g_flycam.is_looking ||
            g_flycam.right_press_pending) {
            ::render_panel_handle_flycam(dt);
        }
    } else {
        if (!rotate_active && active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            const float kOrbitSensitivity = 0.008f;
            S.cam_yaw   += d.x * kOrbitSensitivity;
            S.cam_pitch += d.y * kOrbitSensitivity;

            const float kPitchLimit = 1.5f;
            if (S.cam_pitch >  kPitchLimit) S.cam_pitch =  kPitchLimit;
            if (S.cam_pitch < -kPitchLimit) S.cam_pitch = -kPitchLimit;
        }

        if (hovered) {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {
                S.cam_dist *= (wheel > 0.0f) ? 0.9f : 1.111f;
                if (S.cam_dist < 0.3f)  S.cam_dist = 0.3f;
                if (S.cam_dist > 50.0f) S.cam_dist = 50.0f;
            }
        }
    }

    if (skel_visible && hovered && ImGui::IsKeyPressed(S.key_rotate_mode)) {
        if (S.selected_bone >= 0 && S.selected_bone < (int)g_mp.bone_count &&
            (size_t)S.selected_bone * 4 + 4 <= S.bone_rot_deltas.size()) {
            if (!S.bone_rotate_mode) {
                int b = S.selected_bone;
                for (int k = 0; k < 4; ++k) {
                    s_rot_snapshot[k] =
                        S.bone_rot_deltas[(size_t)b * 4 + (size_t)k];
                }
                s_rot_snapshot_bone  = b;
                s_rot_snapshot_valid = true;
                S.bone_rotate_mode   = true;
            } else {
                confirm_rotate();
            }
        }
    }

    for (size_t i = 0; i < g_mp.meshes.size(); ++i) {
        g_mp.meshes[i].highlight =
            ((int)i == ::g_highlight_mesh_idx) ||
            (::g_selected_level_pick_id == 0 &&
             (int)i == ::g_selected_level_mesh_idx);
        g_mp.meshes[i].isolated  = ((int)i == ::g_isolate_mesh_idx);
    }
    g_mp.selected_pick_id = ::g_selected_level_pick_id;
    g_mp.selected_pick_hash = ::g_selected_level_hash;

    if (!S.terrain_mode) apply_orbit_to_flycam();
    MP_Render(device, g_mp, g_flycam);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (g_mp.srv) {
        dl->AddImage((ImTextureID)g_mp.srv,
                     origin,
                     ImVec2(origin.x + region.x, origin.y + region.y));
    }

#ifdef _WIN32
    if (S.terrain_mode) {
        draw_gdb_placements_overlay(origin, region);
    }
    draw_spawn_markers_overlay(origin, region, hovered);
    if (g_marker_clear_selection) {
        g_marker_clear_selection = false;
        ::g_selected_level_pick_id = 0;
        ::g_selected_level_mesh_idx = -1;
        ::g_selected_level_hash = 0;
    }

    if (g_sel_spawn_marker >= 0 &&
        g_sel_spawn_marker < (int)g_level_spawn_markers.size() &&
        level_marker_visible(
            g_level_spawn_markers[(size_t)g_sel_spawn_marker])) {
        const auto& mk =
            g_level_spawn_markers[(size_t)g_sel_spawn_marker];
        const Gdb::EntityGameplayDetails* marker_gameplay = nullptr;
        const uint32_t gameplay_hash = mk.creature_entity_hash != 0
            ? mk.creature_entity_hash : mk.entity_hash;
        auto gameplay_it = g_level_entity_gameplay.find(gameplay_hash);
        if (gameplay_it != g_level_entity_gameplay.end()) {
            marker_gameplay = &gameplay_it->second;
        }
        uint32_t spawn_owner_entity = 0;
        uint32_t spawn_owner_list = 0;
        if (mk.kind == 2) {
            for (const auto& candidate : g_level_spawn_markers) {
                if (candidate.kind != 1 ||
                    !candidate.spawn_points_record) {
                    continue;
                }
                if (std::find(candidate.spawn_point_entities.begin(),
                              candidate.spawn_point_entities.end(),
                              mk.entity_hash) !=
                    candidate.spawn_point_entities.end()) {
                    spawn_owner_entity = candidate.entity_hash;
                    spawn_owner_list = candidate.spawn_points_record;
                    break;
                }
            }
        }
        const uint32_t edit_id = 0x70000000u |
                                 uint32_t(g_sel_spawn_marker);
        const bool pending_addition = mk.pending_addition_index >= 0;
        const bool editable = LevelEdit::Enabled() &&
                              !LevelEdit::Saving() &&
                              (pending_addition || mk.pos_off[0] ||
                               mk.pos_off[1] ||
                               mk.pos_off[2]);
        const bool spawn_deletable =
            LevelEdit::Enabled() && !LevelEdit::Saving() &&
            mk.kind == 2 && spawn_owner_list != 0;
        const bool entity_deletable =
            LevelEdit::Enabled() && !LevelEdit::Saving() &&
            mk.kind != 2 && (mk.entity_hash != 0 || pending_addition);
        auto queue_entity_delete = [&]() {
            LevelEdit::InstInfo info;
            const float orig[3] = {mk.x, mk.y, mk.z};
            info.orig_pos = orig;
            info.gdb_off = mk.pos_off;
            info.gdb_rot_off = mk.rot_off;
            info.gdb_entity_hash = mk.entity_hash;
            if (pending_addition) {
                info.lev_off = uint32_t(mk.pending_addition_index + 1);
                info.lev_kind = 5;
            }
            LevelEdit::PushUndoSnapshot({edit_id});
            LevelEdit::SetDeleted(edit_id, info);
            OutputLog::info("level edit: entity deletion queued");
            if (pending_addition) {
                g_level_spawn_markers[size_t(g_sel_spawn_marker)].kind = 0;
            }
            g_sel_spawn_marker = -1;
        };
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            g_sel_spawn_marker = -1;
        } else if (!ImGui::GetIO().WantTextInput &&
                   ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            if (spawn_deletable) {
                LevelEdit::RemoveSpawnPointFromExisting(
                    spawn_owner_entity, spawn_owner_list,
                    mk.entity_hash);
                OutputLog::info(
                    "level edit: spawn point deletion queued");
                g_sel_spawn_marker = -1;
            } else if (entity_deletable) {
                queue_entity_delete();
            }
        }
        const float kMarkerW =
            (mk.is_container || marker_gameplay) ? 320.0f : 220.0f;
        ImGui::SetNextWindowPos(
            ImVec2(origin.x + region.x - kMarkerW - 8.0f,
                   origin.y + 6.0f));
        ImGui::SetNextWindowSize(ImVec2(kMarkerW, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.78f);
        ImGuiWindowFlags mfl = ImGuiWindowFlags_NoTitleBar |
                               ImGuiWindowFlags_NoResize |
                               ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoCollapse |
                               ImGuiWindowFlags_NoSavedSettings |
                               ImGuiWindowFlags_AlwaysAutoResize;
        if (g_sel_spawn_marker >= 0 &&
            ImGui::Begin("##spawn_marker_editor", nullptr, mfl)) {
            float mpos[3] = {mk.x, mk.y, mk.z};
            {
                float d_pos[3], d_rot[3];
                if (LevelEdit::EditFor(edit_id, d_pos, d_rot)) {
                    mpos[0] += d_pos[0];
                    mpos[1] += d_pos[1];
                    mpos[2] += d_pos[2];
                }
            }
            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                               "Position:");
            static const char* kMAxis[3] = {"X##mpos", "Y##mpos",
                                            "Z##mpos"};
            float edit_pos[3] = {mpos[0], mpos[1], mpos[2]};
            bool commit = false;
            if (editable) {
                for (int a = 0; a < 3; ++a) {
                    ImGui::SetNextItemWidth(120.0f);
                    ImGui::InputFloat(kMAxis[a], &edit_pos[a], 0.0f,
                                      0.0f, "%.3f");
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        commit = true;
                    }
                }
            } else {
                ImGui::Text("X: %.3f", mpos[0]);
                ImGui::Text("Y: %.3f", mpos[1]);
                ImGui::Text("Z: %.3f", mpos[2]);
            }
            if (commit) {
                const float step[3] = {edit_pos[0] - mpos[0],
                                       edit_pos[1] - mpos[1],
                                       edit_pos[2] - mpos[2]};
                if (step[0] != 0 || step[1] != 0 || step[2] != 0) {
                    LevelEdit::InstInfo info;
                    const float orig[3] = {mk.x, mk.y, mk.z};
                    info.orig_pos = orig;
                    info.gdb_off = mk.pos_off;
                    info.gdb_rot_off = mk.rot_off;
                    info.gdb_entity_hash = mk.entity_hash;
                    if (pending_addition) {
                        info.lev_off =
                            uint32_t(mk.pending_addition_index + 1);
                        info.lev_kind = 5;
                    }
                    LevelEdit::AddMove(edit_id, step, info);
                }
            }

            ImGui::Spacing();
            const char* kind_name =
                mk.kind == 1 ? "Creature generator"
                : mk.kind == 2 ? "Spawn point"
                : mk.kind == 4 ? "Dig spot"
                : mk.kind == 5 ? "Container"
                : mk.kind == 6 ? "Static prop"
                               : "NPC / creature";
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.55f, 1.0f), "%s",
                               kind_name);
            if (!mk.name.empty()) {
                ImGui::TextUnformatted(mk.name.c_str());
            }
            if (mk.kind == 1 && !mk.creature_name.empty()) {
                ImGui::TextDisabled("spawns: %s",
                                    mk.creature_name.c_str());
            }
            if (marker_gameplay) {
                draw_entity_gameplay_details(*marker_gameplay);
            }
            if (pending_addition) {
                draw_addition_container_details(
                    mk.pending_addition_index);
            } else if (mk.kind == 4 || mk.is_container) {
                draw_level_container_details(mk.entity_hash);
            }
            if (mk.kind == 2 && spawn_deletable &&
                ImGui::Button("Delete spawn point")) {
                LevelEdit::RemoveSpawnPointFromExisting(
                    spawn_owner_entity, spawn_owner_list,
                    mk.entity_hash);
                OutputLog::info(
                    "level edit: spawn point deletion queued");
                g_sel_spawn_marker = -1;
            }
            if (mk.kind != 2 && entity_deletable &&
                ImGui::Button(mk.kind == 1 ? "Delete generator"
                                           : pending_addition
                                               ? (mk.kind == 4
                                                      ? "Delete dig spot"
                                                      : mk.kind == 3
                                                            ? "Delete NPC"
                                                            : mk.kind == 6
                                                                  ? "Delete static prop"
                                                            : "Delete container")
                                               : "Delete entity")) {
                queue_entity_delete();
            }
            if (mk.kind == 1) {
                const bool can_add_sp =
                    LevelEdit::Enabled() && !LevelEdit::Saving() &&
                    mk.spawn_points_record != 0;
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Spawn points (%zu):",
                                   mk.spawn_point_entities.size());
                for (size_t si = 0;
                     si < mk.spawn_point_entities.size(); ++si) {
                    const uint32_t sph = mk.spawn_point_entities[si];
                    if (LevelEdit::SpawnPointRemovalPending(sph)) {
                        continue;
                    }
                    int target = -1;
                    for (size_t mj = 0;
                         mj < g_level_spawn_markers.size(); ++mj) {
                        if (g_level_spawn_markers[mj].entity_hash ==
                            sph) {
                            target = int(mj);
                            break;
                        }
                    }
                    ImGui::PushID(int(si) + 0x3000);
                    char lbl[64];
                    std::snprintf(lbl, sizeof(lbl), "0x%08X%s", sph,
                                  target < 0 ? " (no marker)" : "");
                    if (ImGui::Selectable(
                            lbl, target == g_sel_spawn_marker) &&
                        target >= 0) {
                        g_sel_spawn_marker = target;
                    }
                    if (can_add_sp) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Delete")) {
                            LevelEdit::RemoveSpawnPointFromExisting(
                                mk.entity_hash, mk.spawn_points_record,
                                sph);
                            if (target == g_sel_spawn_marker) {
                                g_sel_spawn_marker = -1;
                            }
                            OutputLog::info(
                                "level edit: spawn point deletion "
                                "queued");
                        }
                    }
                    ImGui::PopID();
                }
                if (can_add_sp &&
                    ImGui::SmallButton("+ Add spawn point")) {
                    const float n =
                        float(mk.spawn_point_entities.size() +
                              LevelEdit::PendingSpawnPointCount());
                    const float ang = n * 1.0471976f;
                    const float rad = 1.5f + 0.3f * n;
                    const float sp_pos[3] = {
                        mpos[0] + std::cos(ang) * rad,
                        mpos[1] + std::sin(ang) * rad,
                        mpos[2]};
                    LevelEdit::AddSpawnPointToExisting(
                        mk.entity_hash, mk.spawn_points_record,
                        sp_pos);
                    OutputLog::success(
                        "level edit: spawn point queued next to the "
                        "generator (authored on Save; move it after "
                        "reload)");
                }
                if (LevelEdit::PendingSpawnPointCount() > 0) {
                    ImGui::TextDisabled(
                        "%zu pending spawn point(s)",
                        LevelEdit::PendingSpawnPointCount());
                }
            }
            ImGui::End();
        } else if (g_sel_spawn_marker >= 0) {
            ImGui::End();
        }

        if (g_sel_spawn_marker >= 0 && editable) {
            float gpos[3] = {mk.x, mk.y, mk.z};
            {
                float d_pos[3], d_rot[3];
                if (LevelEdit::EditFor(edit_id, d_pos, d_rot)) {
                    gpos[0] += d_pos[0];
                    gpos[1] += d_pos[1];
                    gpos[2] += d_pos[2];
                }
            }
            LevelGizmo::Result gz = LevelGizmo::DrawAndHandle(
                g_flycam, origin, region, gpos, true);
            static bool s_mk_dragging = false;
            if (gz.dragging && !s_mk_dragging) {
                LevelEdit::PushUndoSnapshot({edit_id});
            }
            s_mk_dragging = gz.dragging;
            if (gz.moved &&
                (gz.step[0] != 0.0f || gz.step[1] != 0.0f ||
                 gz.step[2] != 0.0f)) {
                LevelEdit::InstInfo info;
                const float orig[3] = {mk.x, mk.y, mk.z};
                info.orig_pos = orig;
                info.gdb_off = mk.pos_off;
                info.gdb_rot_off = mk.rot_off;
                info.gdb_entity_hash = mk.entity_hash;
                LevelEdit::AddMove(edit_id, gz.step, info);
            }
        }
    }

    if (g_sel_pending_sp >= 0 && S.show_spawn_markers) {
        std::vector<LevelEdit::PendingSpawnPoint> psps;
        LevelEdit::GetPendingSpawnPoints(psps);
        const LevelEdit::PendingSpawnPoint* sel_sp = nullptr;
        for (const auto& sp : psps) {
            if (sp.id == g_sel_pending_sp) {
                sel_sp = &sp;
                break;
            }
        }
        if (!sel_sp) {
            g_sel_pending_sp = -1;
        } else {
            const bool sp_editable =
                LevelEdit::Enabled() && !LevelEdit::Saving();
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                g_sel_pending_sp = -1;
            } else if (sp_editable && !ImGui::GetIO().WantTextInput &&
                       ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                LevelEdit::RemovePendingSpawnPoint(sel_sp->id);
                OutputLog::info(
                    "level edit: pending spawn point removed");
                g_sel_pending_sp = -1;
                sel_sp = nullptr;
            }
        }
        if (sel_sp) {
            const bool sp_editable =
                LevelEdit::Enabled() && !LevelEdit::Saving();
            if (!details_panel_docked()) {
            const float kSpW = 220.0f;
            ImGui::SetNextWindowPos(
                ImVec2(origin.x + region.x - kSpW - 8.0f,
                       origin.y + 6.0f));
            ImGui::SetNextWindowSize(ImVec2(kSpW, 0), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.78f);
            ImGuiWindowFlags sfl = ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_AlwaysAutoResize;
            if (ImGui::Begin("##pending_sp_editor", nullptr, sfl)) {
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Position:");
                float ep[3] = {sel_sp->pos[0], sel_sp->pos[1],
                               sel_sp->pos[2]};
                bool commit = false;
                if (sp_editable) {
                    static const char* kAx[3] = {"X##psp", "Y##psp",
                                                 "Z##psp"};
                    for (int a = 0; a < 3; ++a) {
                        ImGui::SetNextItemWidth(120.0f);
                        ImGui::InputFloat(kAx[a], &ep[a], 0.0f, 0.0f,
                                          "%.3f");
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            commit = true;
                        }
                    }
                } else {
                    ImGui::Text("X: %.3f", ep[0]);
                    ImGui::Text("Y: %.3f", ep[1]);
                    ImGui::Text("Z: %.3f", ep[2]);
                }
                if (commit) {
                    LevelEdit::MovePendingSpawnPoint(sel_sp->id, ep);
                }
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.9f, 0.75f, 1.0f, 1.0f),
                                   "%s", sel_sp->label.c_str());
                ImGui::TextDisabled("unsaved - authored on Save");
                if (sp_editable &&
                    ImGui::Button("Delete spawn point")) {
                    LevelEdit::RemovePendingSpawnPoint(sel_sp->id);
                    OutputLog::info(
                        "level edit: pending spawn point removed");
                    g_sel_pending_sp = -1;
                }
            }
            ImGui::End();
            }   

            if (sp_editable && g_sel_pending_sp >= 0) {
                float gpos[3] = {sel_sp->pos[0], sel_sp->pos[1],
                                 sel_sp->pos[2]};
                LevelGizmo::Result gz = LevelGizmo::DrawAndHandle(
                    g_flycam, origin, region, gpos, true);
                if (gz.moved &&
                    (gz.step[0] != 0.0f || gz.step[1] != 0.0f ||
                     gz.step[2] != 0.0f)) {
                    const float np[3] = {gpos[0] + gz.step[0],
                                         gpos[1] + gz.step[1],
                                         gpos[2] + gz.step[2]};
                    LevelEdit::MovePendingSpawnPoint(sel_sp->id, np);
                }
            }
        }
    }

    if (g_sel_pending_gen >= 0 && S.show_spawn_markers) {
        std::vector<LevelEdit::GeneratorAddition> pgens;
        LevelEdit::GetGenerators(pgens);
        const bool gen_valid =
            g_sel_pending_gen < (int)pgens.size() &&
            !pgens[(size_t)g_sel_pending_gen].removed;
        if (!gen_valid) {
            g_sel_pending_gen = -1;
        } else {
            const auto& pg = pgens[(size_t)g_sel_pending_gen];
            const bool gen_editable =
                LevelEdit::Enabled() && !LevelEdit::Saving();
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                g_sel_pending_gen = -1;
            } else if (gen_editable && !ImGui::GetIO().WantTextInput &&
                       ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                LevelEdit::RemoveGenerator(g_sel_pending_gen);
                g_sel_pending_gen = -1;
            }
            if (g_sel_pending_gen >= 0) {
                if (!details_panel_docked()) {
                const float kGenW = 220.0f;
                ImGui::SetNextWindowPos(
                    ImVec2(origin.x + region.x - kGenW - 8.0f,
                           origin.y + 6.0f));
                ImGui::SetNextWindowSize(ImVec2(kGenW, 0),
                                         ImGuiCond_Always);
                ImGui::SetNextWindowBgAlpha(0.78f);
                ImGuiWindowFlags gfl =
                    ImGuiWindowFlags_NoTitleBar |
                    ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_AlwaysAutoResize;
                if (ImGui::Begin("##pending_gen_editor", nullptr,
                                 gfl)) {
                    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                       "Position:");
                    float ep[3] = {pg.pos[0], pg.pos[1], pg.pos[2]};
                    bool commit = false;
                    if (gen_editable) {
                        static const char* kAx[3] = {
                            "X##pgen", "Y##pgen", "Z##pgen"};
                        for (int a = 0; a < 3; ++a) {
                            ImGui::SetNextItemWidth(120.0f);
                            ImGui::InputFloat(kAx[a], &ep[a], 0.0f,
                                              0.0f, "%.3f");
                            if (ImGui::IsItemDeactivatedAfterEdit()) {
                                commit = true;
                            }
                        }
                    } else {
                        ImGui::Text("X: %.3f", ep[0]);
                        ImGui::Text("Y: %.3f", ep[1]);
                        ImGui::Text("Z: %.3f", ep[2]);
                    }
                    if (commit) {
                        LevelEdit::MovePendingGenerator(
                            g_sel_pending_gen, ep);
                    }
                    ImGui::Spacing();
                    ImGui::TextColored(
                        ImVec4(1.0f, 0.75f, 0.55f, 1.0f),
                        "Creature generator (unsaved)");
                    ImGui::TextDisabled("spawns: %s",
                                        pg.creature_name.c_str());
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                       "Spawn points (%zu):",
                                       pg.spawn_points.size());
                    for (size_t si = 0; si < pg.spawn_points.size();
                         ++si) {
                        ImGui::PushID(int(si) + 0x4000);
                        char lbl[48];
                        std::snprintf(lbl, sizeof(lbl),
                                      "#%zu (%.1f, %.1f, %.1f)",
                                      si + 1, pg.spawn_points[si][0],
                                      pg.spawn_points[si][1],
                                      pg.spawn_points[si][2]);
                        const int sp_id =
                            (g_sel_pending_gen << 8) | int(si);
                        if (ImGui::Selectable(
                                lbl, g_sel_pending_sp == sp_id)) {
                            g_sel_pending_sp = sp_id;
                            g_sel_pending_gen = -1;
                        }
                        ImGui::PopID();
                    }
                    if (gen_editable &&
                        ImGui::SmallButton("+ Add spawn point")) {
                        const float n = float(pg.spawn_points.size());
                        const float ang = n * 1.0471976f;
                        const float rad = 1.5f + 0.3f * n;
                        const float sp_pos[3] = {
                            pg.pos[0] + std::cos(ang) * rad,
                            pg.pos[1] + std::sin(ang) * rad,
                            pg.pos[2]};
                        LevelEdit::AddGeneratorSpawnPoint(
                            g_sel_pending_gen, sp_pos);
                    }
                    ImGui::TextDisabled("unsaved - authored on Save");
                    if (gen_editable &&
                        ImGui::Button("Delete generator")) {
                        LevelEdit::RemoveGenerator(g_sel_pending_gen);
                        OutputLog::info(
                            "level edit: unsaved generator removed");
                        g_sel_pending_gen = -1;
                    }
                }
                ImGui::End();
                }   

                if (gen_editable && g_sel_pending_gen >= 0) {
                    float gpos[3] = {pg.pos[0], pg.pos[1], pg.pos[2]};
                    LevelGizmo::Result gz = LevelGizmo::DrawAndHandle(
                        g_flycam, origin, region, gpos, true);
                    if (gz.moved &&
                        (gz.step[0] != 0.0f || gz.step[1] != 0.0f ||
                         gz.step[2] != 0.0f)) {
                        const float np[3] = {gpos[0] + gz.step[0],
                                             gpos[1] + gz.step[1],
                                             gpos[2] + gz.step[2]};
                        LevelEdit::MovePendingGenerator(
                            g_sel_pending_gen, np);
                    }
                }
            }
        }
    }
#endif

    if (g_mp.no_tilt && ::g_selected_level_mesh_idx >= 0 &&
        ::g_selected_level_mesh_idx < (int)g_mp.meshes.size())
    {
        const auto& sel_mesh =
            g_mp.meshes[(size_t)::g_selected_level_mesh_idx];
        float sel_pos[3] = {0.0f, 0.0f, 0.0f};
        float sel_rot[3] = {0.0f, 0.0f, 0.0f};
        bool  sel_has_rot = false;
        bool  sel_found = false;
        uint32_t sel_gdb_entity_hash = 0;
        if (::g_selected_level_pick_id != 0) {
            for (const auto& pr : sel_mesh.pick_ranges) {
                if (pr.selection_id != ::g_selected_level_pick_id) continue;
                sel_gdb_entity_hash = pr.gdb_entity_hash;
                if (pr.has_transform) {
                    sel_pos[0] = pr.inst_pos[0];
                    sel_pos[1] = pr.inst_pos[1];
                    sel_pos[2] = pr.inst_pos[2];
                    sel_rot[0] = pr.inst_rot_deg[0];
                    sel_rot[1] = pr.inst_rot_deg[1];
                    sel_rot[2] = pr.inst_rot_deg[2];
                    sel_has_rot = true;
                } else {
                    sel_pos[0] = pr.center[0];
                    sel_pos[1] = pr.center[2];
                    sel_pos[2] = pr.center[1];
                }
                sel_found = true;
                break;
            }
        }
        if (!sel_found) {
            sel_pos[0] = sel_mesh.center[0];
            sel_pos[1] = sel_mesh.center[2];
            sel_pos[2] = sel_mesh.center[1];
        }
        const bool whole_mesh_sel = (::g_selected_level_pick_id == 0);
        const uint32_t edit_key = whole_mesh_sel
            ? (0x80000000u | (uint32_t)::g_selected_level_mesh_idx)
            : ::g_selected_level_pick_id;
        {
            float d_pos[3], d_rot[3];
            if (LevelEdit::EditFor(edit_key, d_pos, d_rot)) {
                sel_pos[0] += d_pos[0];
                sel_pos[1] += d_pos[1];
                sel_pos[2] += d_pos[2];
                sel_rot[0] += d_rot[0];
                sel_rot[1] += d_rot[1];
                sel_rot[2] += d_rot[2];
            }
        }

        auto range_in_group = [](const MPPerMesh::PickRange& pr) {
            if (pr.selection_id == ::g_selected_level_pick_id) return true;
            return ::g_selected_level_hash != 0 &&
                   pr.inst_hash == ::g_selected_level_hash;
        };
        auto collect_group_ids = [&]() {
            std::vector<uint32_t> ids;
            if (whole_mesh_sel) {
                ids.push_back(edit_key);
                return ids;
            }
            std::unordered_set<uint32_t> seen;
            for (const auto& m2 : g_mp.meshes) {
                for (const auto& pr : m2.pick_ranges) {
                    if (!range_in_group(pr)) continue;
                    if (seen.insert(pr.selection_id).second) {
                        ids.push_back(pr.selection_id);
                    }
                }
            }
            return ids;
        };
        enum { kEditMove, kEditRotate, kEditDelete };
        auto apply_group_edit = [&](int what, const float v[3]) {
            if (whole_mesh_sel) {
                const float orig[3] = { sel_mesh.center[0],
                                        sel_mesh.center[2],
                                        sel_mesh.center[1] };
                LevelEdit::InstInfo info;
                info.orig_pos = orig;
                if (what == kEditMove) {
                    LevelEdit::AddMove(edit_key, v, info);
                } else if (what == kEditRotate) {
                    LevelEdit::AddRotate(edit_key, v, info);
                } else {
                    LevelEdit::SetDeleted(edit_key, info);
                }
                return;
            }
            std::unordered_set<uint32_t> done;
            for (const auto& m2 : g_mp.meshes) {
                for (const auto& pr : m2.pick_ranges) {
                    if (!range_in_group(pr)) continue;
                    if (!done.insert(pr.selection_id).second) continue;
                    LevelEdit::InstInfo info;
                    info.orig_pos = pr.inst_pos;
                    info.orig_rot_deg[0] = pr.inst_rot_deg[0];
                    info.orig_rot_deg[1] = pr.inst_rot_deg[1];
                    info.orig_rot_deg[2] = pr.inst_rot_deg[2];
                    info.lev_off = pr.pos_file_offset;
                    info.lev_kind = pr.lev_rec_kind;
                    info.gdb_off = pr.gdb_pos_off;
                    info.gdb_rot_off = pr.gdb_rot_off;
                    info.gdb_entity_hash = pr.gdb_entity_hash;
                    if (what == kEditMove) {
                        LevelEdit::AddMove(pr.selection_id, v, info);
                    } else if (what == kEditRotate) {
                        LevelEdit::AddRotate(pr.selection_id, v, info);
                    } else {
                        LevelEdit::SetDeleted(pr.selection_id, info);
                    }
                }
            }
        };
        const bool sel_finite = std::isfinite(sel_pos[0]) &&
                                std::isfinite(sel_pos[1]) &&
                                std::isfinite(sel_pos[2]);
        const bool edit_active = LevelEdit::Enabled() &&
                                 !LevelEdit::Saving() &&
                                 (whole_mesh_sel || sel_found) &&
                                 sel_finite;

        static int      s_dbg_idx = -2;
        static uint32_t s_dbg_id  = 0xFFFFFFFFu;
        const bool dbg_sel_changed =
            s_dbg_idx != ::g_selected_level_mesh_idx ||
            s_dbg_id  != ::g_selected_level_pick_id;
        if (dbg_sel_changed) {
            s_dbg_idx = ::g_selected_level_mesh_idx;
            s_dbg_id  = ::g_selected_level_pick_id;
            DebugTrace::log(
                "sel: idx=%d id=%u hash=%llu ranges=%zu found=%d whole=%d "
                "finite=%d pos=(%.2f,%.2f,%.2f) edit_active=%d",
                ::g_selected_level_mesh_idx, ::g_selected_level_pick_id,
                (unsigned long long)::g_selected_level_hash,
                sel_mesh.pick_ranges.size(), sel_found ? 1 : 0,
                whole_mesh_sel ? 1 : 0, sel_finite ? 1 : 0,
                sel_pos[0], sel_pos[1], sel_pos[2], edit_active ? 1 : 0);
        }

        const Gdb::EntityContents* sel_contents = nullptr;

        if (::g_selected_level_hash != 0 &&
            ::g_selected_level_hash <= 0xFFFFFFFFull) {
            auto cit = g_level_entity_contents.find(
                uint32_t(::g_selected_level_hash));
            if (cit != g_level_entity_contents.end()) {
                sel_contents = &cit->second;
            }
        }
        if (!sel_contents && sel_gdb_entity_hash != 0) {
            auto cit = g_level_entity_contents.find(sel_gdb_entity_hash);
            if (cit != g_level_entity_contents.end()) {
                sel_contents = &cit->second;
            }
        }
        const Gdb::EntityGameplayDetails* sel_gameplay = nullptr;
        if (::g_selected_level_hash != 0 &&
            ::g_selected_level_hash <= 0xFFFFFFFFull) {
            auto git = g_level_entity_gameplay.find(
                uint32_t(::g_selected_level_hash));
            if (git != g_level_entity_gameplay.end()) {
                sel_gameplay = &git->second;
            }
        }
        const Gdb::PropertyDetails* sel_property = nullptr;
        if (::g_selected_level_hash != 0 &&
            ::g_selected_level_hash <= 0xFFFFFFFFull) {
            auto pit = g_level_property_details.find(
                uint32_t(::g_selected_level_hash));
            if (pit != g_level_property_details.end()) {
                sel_property = &pit->second;
            }
        }
        if (!sel_property && sel_gdb_entity_hash != 0) {
            auto pit = g_level_property_details.find(sel_gdb_entity_hash);
            if (pit != g_level_property_details.end()) {
                sel_property = &pit->second;
            }
        }
        if (!sel_gameplay && sel_gdb_entity_hash != 0) {
            auto git = g_level_entity_gameplay.find(sel_gdb_entity_hash);
            if (git != g_level_entity_gameplay.end()) {
                sel_gameplay = &git->second;
            }
        }
        if (!sel_gameplay) {
            uint32_t creature_hash = 0;
            const int selected_marker =
                selected_level_spawn_marker_index();
            if (selected_marker >= 0) {
                creature_hash = g_level_spawn_markers[
                    size_t(selected_marker)].creature_entity_hash;
            }
            if (creature_hash == 0 && sel_gdb_entity_hash != 0) {
                for (const auto& marker : g_level_spawn_markers) {
                    if (marker.entity_hash == sel_gdb_entity_hash) {
                        creature_hash = marker.creature_entity_hash;
                        if (creature_hash != 0) break;
                    }
                }
            }
            auto git = g_level_entity_gameplay.find(creature_hash);
            if (git != g_level_entity_gameplay.end()) {
                sel_gameplay = &git->second;
            }
        }
        constexpr uint64_t kAdditionHashBase = 0xADD0000000000000ull;
        int sel_chest_addition = -1;
        int sel_readable_addition = -1;
        if (::g_selected_level_hash >= kAdditionHashBase) {
            const int add_idx =
                int(::g_selected_level_hash - kAdditionHashBase);
            if (LevelEdit::AdditionIsChest(add_idx)) {
                sel_chest_addition = add_idx;
            }
            if (LevelEdit::AdditionIsReadable(add_idx)) {
                sel_readable_addition = add_idx;
            }
        }
        const Gdb::EntityTextTags* sel_text = nullptr;
        if (::g_selected_level_hash != 0 &&
            ::g_selected_level_hash <= 0xFFFFFFFFull) {
            auto tit = g_level_entity_text.find(
                uint32_t(::g_selected_level_hash));
            if (tit != g_level_entity_text.end()) {
                sel_text = &tit->second;
            }
        }
        if (!sel_text && sel_gdb_entity_hash != 0) {
            auto tit = g_level_entity_text.find(sel_gdb_entity_hash);
            if (tit != g_level_entity_text.end()) {
                sel_text = &tit->second;
            }
        }
        if (!sel_text && sel_found) {
            float best = 3.0f * 3.0f;
            for (const auto& kv : g_level_entity_text) {
                if (!kv.second.has_pos) continue;
                const float dx = kv.second.x - sel_pos[0];
                const float dy = kv.second.y - sel_pos[1];
                const float dz = kv.second.z - sel_pos[2];
                const float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 < best) {
                    best = d2;
                    sel_text = &kv.second;
                }
            }
        }
        if (!details_panel_docked()) {
        const float kOverlayW = (sel_contents || sel_gameplay ||
                                 sel_property ||
                                 sel_chest_addition >= 0 || sel_text ||
                                 sel_readable_addition >= 0)
            ? 290.0f
            : (LevelEdit::Enabled() ? 190.0f : 150.0f);
        ImGui::SetNextWindowPos(ImVec2(origin.x + region.x - kOverlayW - 8.0f,
                                       origin.y + 6.0f));
        ImGui::SetNextWindowSize(ImVec2(kOverlayW, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.78f);
        ImGuiWindowFlags ofl = ImGuiWindowFlags_NoTitleBar
                             | ImGuiWindowFlags_NoResize
                             | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoCollapse
                             | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_AlwaysAutoResize
                             | ImGuiWindowFlags_NoFocusOnAppearing;
        if (ImGui::Begin("##sel_transform_overlay", nullptr, ofl)) {
            if (dbg_sel_changed) DebugTrace::log("ov: begin");
            if (edit_active) {
                const char* mode_name =
                    LevelGizmo::GetMode() == LevelGizmo::Mode::Rotate
                        ? "Rotate (E)"
                        : "Move (W)";
                ImGui::TextDisabled("%s", mode_name);
            }
            if (S.dev_mode) {
                ImGui::TextDisabled(
                    "sel 0x%016llX link 0x%08X text %s(%zu)",
                    (unsigned long long)::g_selected_level_hash,
                    sel_gdb_entity_hash, sel_text ? "HIT" : "miss",
                    g_level_entity_text.size());
            }
            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Position:");
            if (edit_active) {
                static const char* kAxis[3] = { "X##selpos", "Y##selpos",
                                                "Z##selpos" };
                float edit_pos[3] = { sel_pos[0], sel_pos[1], sel_pos[2] };
                bool commit = false;
                for (int a = 0; a < 3; ++a) {
                    ImGui::SetNextItemWidth(120.0f);
                    ImGui::InputFloat(kAxis[a], &edit_pos[a], 0.0f, 0.0f,
                                      "%.3f");
                    if (ImGui::IsItemDeactivatedAfterEdit()) commit = true;
                }
                if (commit) {
                    const float step[3] = { edit_pos[0] - sel_pos[0],
                                            edit_pos[1] - sel_pos[1],
                                            edit_pos[2] - sel_pos[2] };
                    if (step[0] != 0.0f || step[1] != 0.0f ||
                        step[2] != 0.0f) {
                        LevelEdit::PushUndoSnapshot(collect_group_ids());
                        apply_group_edit(kEditMove, step);
                    }
                }
                if (dbg_sel_changed) DebugTrace::log("ov: pos done");
            } else {
                ImGui::Text("X: %.3f", sel_pos[0]);
                ImGui::Text("Y: %.3f", sel_pos[1]);
                ImGui::Text("Z: %.3f", sel_pos[2]);
            }
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Rotation:");
            if (edit_active) {
                static const char* kRAxis[3] = { "X##selrot", "Y##selrot",
                                                 "Z##selrot" };
                float edit_rot[3] = { sel_rot[0], sel_rot[1], sel_rot[2] };
                bool commit = false;
                for (int a = 0; a < 3; ++a) {
                    ImGui::SetNextItemWidth(120.0f);
                    ImGui::InputFloat(kRAxis[a], &edit_rot[a], 0.0f, 0.0f,
                                      "%.1f");
                    if (ImGui::IsItemDeactivatedAfterEdit()) commit = true;
                }
                if (commit) {
                    const float step[3] = { edit_rot[0] - sel_rot[0],
                                            edit_rot[1] - sel_rot[1],
                                            edit_rot[2] - sel_rot[2] };
                    if (step[0] != 0.0f || step[1] != 0.0f ||
                        step[2] != 0.0f) {
                        LevelEdit::PushUndoSnapshot(collect_group_ids());
                        apply_group_edit(kEditRotate, step);
                    }
                }
            } else if (sel_has_rot) {
                ImGui::Text("X: %.1f", sel_rot[0]);
                ImGui::Text("Y: %.1f", sel_rot[1]);
                ImGui::Text("Z: %.1f", sel_rot[2]);
            } else {
                ImGui::TextDisabled("n/a");
            }
            if (sel_text) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Text:");
                static uint32_t s_text_sel = 0;
                static std::vector<std::string> s_text_bufs;
                const uint32_t text_key = sel_text->tag_hashes.front();
                if (s_text_sel != text_key ||
                    s_text_bufs.size() != sel_text->tag_hashes.size()) {
                    s_text_sel = text_key;
                    s_text_bufs.clear();
                    for (uint32_t th : sel_text->tag_hashes) {
                        std::string t;
                        if (!LevelEdit::GetEntityTextEdit(th, t)) {
                            TextBank::Lookup(th, t);
                        }
                        s_text_bufs.push_back(std::move(t));
                    }
                }
                const bool text_editable =
                    LevelEdit::Enabled() && !LevelEdit::Saving();
                for (size_t pi = 0; pi < s_text_bufs.size(); ++pi) {
                    ImGui::PushID(int(pi) + 0x2000);
                    if (s_text_bufs.size() > 1) {
                        if (pi == 0) {
                            ImGui::TextDisabled("Item Name");
                        } else if (pi == 1) {
                            ImGui::TextDisabled("Description");
                        } else {
                            ImGui::TextDisabled("Page %d", int(pi) + 1);
                        }
                    }
                    if (text_editable) {
                        ImGui::InputTextMultiline(
                            "##entity_text", &s_text_bufs[pi],
                            ImVec2(268.0f, 110.0f));
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            LevelEdit::SetEntityTextEdit(
                                sel_text->tag_hashes[pi],
                                s_text_bufs[pi]);
                        }
                    } else {
                        std::string shown = s_text_bufs[pi];
                        if (shown.size() > 1200) {
                            shown.resize(1200);
                            shown += " [...]";
                        }
                        ImGui::TextWrapped("%s", shown.c_str());
                    }
                    ImGui::PopID();
                }
                if (text_editable) {
                    ImGui::TextDisabled(
                        "Text is written to book.babel on Save");
                }
            }
            if (sel_readable_addition >= 0) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
                                   "New readable (unsaved)");
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Text:");
                static int s_addtext_sel = -1;
                static std::string s_addtext_buf;
                if (s_addtext_sel != sel_readable_addition) {
                    s_addtext_sel = sel_readable_addition;
                    s_addtext_buf.clear();
                    LevelEdit::GetAdditionReadableText(
                        sel_readable_addition, s_addtext_buf);
                }
                const bool add_text_editable =
                    LevelEdit::Enabled() && !LevelEdit::Saving();
                ImGui::BeginDisabled(!add_text_editable);
                ImGui::InputTextMultiline("##add_readable_text",
                                          &s_addtext_buf,
                                          ImVec2(268.0f, 110.0f));
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    LevelEdit::SetAdditionReadableText(
                        sel_readable_addition, s_addtext_buf);
                }
                ImGui::EndDisabled();
            }
            if (sel_gameplay) {
                draw_entity_gameplay_details(*sel_gameplay);
            }
            if (sel_property) {
                draw_property_details(*sel_property);
            }
            if (sel_contents) {
                auto pretty_tag = [](std::string tag, int money) {

                    for (const char* pfx : { "INV_ITEM_", "OBJECT_",
                                             "TEXT_" }) {
                        const size_t n = std::strlen(pfx);
                        if (tag.size() > n && tag.compare(0, n, pfx) == 0) {
                            tag = tag.substr(n);
                            break;
                        }
                    }
                    constexpr const char* kNameSuffix = "_NAME";
                    constexpr size_t kNameSuffixLen = 5;
                    if (tag.size() > kNameSuffixLen &&
                        tag.compare(tag.size() - kNameSuffixLen,
                                    kNameSuffixLen, kNameSuffix) == 0) {
                        tag.resize(tag.size() - kNameSuffixLen);
                    }
                    if (tag.find('_') != std::string::npos ||
                        std::none_of(tag.begin(), tag.end(),
                                     [](unsigned char c) {
                                         return std::islower(c);
                                     })) {
                        bool word_start = true;
                        for (auto& c : tag) {
                            if (c == '_') {
                                c = ' ';
                                word_start = true;
                            } else {
                                c = word_start
                                    ? char(std::toupper((unsigned char)c))
                                    : char(std::tolower((unsigned char)c));
                                word_start = false;
                            }
                        }
                    }
                    if (money >= 0) {
                        tag += " (" + std::to_string(money) + " gold)";
                    }
                    return tag;
                };
                auto catalog_label = [&](uint32_t record_hash) {
                    for (const auto& c : g_item_details) {
                        if (c.record_hash == record_hash &&
                            !c.display_name.empty()) {
                            return c.display_name;
                        }
                    }
                    for (const auto& c : g_level_item_catalog) {
                        if (c.record_hash == record_hash) {
                            return pretty_tag(c.label, c.money);
                        }
                    }
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "0x%08X", record_hash);
                    return std::string(buf);
                };
                auto item_label = [&](const Gdb::EntityContentsItem& it) {
                    if (!it.display_name.empty()) return it.display_name;
                    std::string tag = !it.name_tag.empty() ? it.name_tag
                                                           : it.entry_label;
                    if (tag.empty()) return catalog_label(it.record_hash);
                    return pretty_tag(std::move(tag), it.money);
                };
                ImGui::Spacing();
                ImGui::Separator();
                if (!sel_gameplay && !sel_contents->entity_name.empty()) {
                    ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
                                       "%s",
                                       sel_contents->entity_name.c_str());
                }
                draw_container_authored_rules(*sel_contents);
                const uint32_t sel_entity =
                    sel_gdb_entity_hash != 0
                        ? sel_gdb_entity_hash
                        : uint32_t(::g_selected_level_hash);
                std::vector<uint32_t> shown_items;
                bool staged = LevelEdit::GetChestContents(sel_entity,
                                                          shown_items);
                if (!staged) {
                    for (const auto& it : sel_contents->initial_items) {
                        shown_items.push_back(it.record_hash);
                    }
                }
                const bool contents_editable =
                    LevelEdit::Enabled() && !LevelEdit::Saving();

                static int s_picker_slot = -1;
                static uint32_t s_picker_entity = 0;
                static char s_picker_filter[64] = {};

                if (staged || !shown_items.empty() ||
                    sel_contents->has_inventory_component ||
                    contents_editable) {
                    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                       staged ? "Initial items (edited):"
                                              : "Initial items:");
                    int remove_idx = -1;
                    bool open_picker = false;
                    for (size_t ii = 0; ii < shown_items.size(); ++ii) {
                        ImGui::PushID(int(ii));
                        if (contents_editable) {
                            if (ImGui::SmallButton("x")) remove_idx = int(ii);
                            ImGui::SameLine();
                            std::string label;
                            if (!staged &&
                                ii < sel_contents->initial_items.size()) {
                                label = item_label(
                                    sel_contents->initial_items[ii]);
                            } else {
                                label = catalog_label(shown_items[ii]);
                            }
                            if (ImGui::Selectable(label.c_str(), false,
                                    ImGuiSelectableFlags_DontClosePopups)) {
                                s_picker_slot = int(ii);
                                s_picker_entity = sel_entity;
                                s_picker_filter[0] = 0;
                                open_picker = true;
                            }
                        } else {
                            std::string label;
                            if (!staged &&
                                ii < sel_contents->initial_items.size()) {
                                label = item_label(
                                    sel_contents->initial_items[ii]);
                            } else {
                                label = catalog_label(shown_items[ii]);
                            }
                            ImGui::BulletText("%s", label.c_str());
                        }
                        ImGui::PopID();
                    }
                    if (shown_items.empty()) {
                        ImGui::TextDisabled(staged ? "  (emptied)"
                                                   : "  (empty)");
                    }
                    if (contents_editable) {
                        if (ImGui::SmallButton("+ Add item")) {
                            s_picker_slot = int(shown_items.size());
                            s_picker_entity = sel_entity;
                            s_picker_filter[0] = 0;
                            open_picker = true;
                        }
                        if (staged) {
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Revert")) {
                                LevelEdit::ClearChestContents(sel_entity);
                            }
                        }
                        if (staged) {
                            ImGui::TextDisabled("staged - Save to bake");
                        }
                    }
                    if (remove_idx >= 0 &&
                        size_t(remove_idx) < shown_items.size()) {
                        std::vector<uint32_t> next = shown_items;
                        next.erase(next.begin() + remove_idx);
                        LevelEdit::SetChestContents(sel_entity, next);
                    }
                    if (open_picker) {
                        ImGui::OpenPopup("##chest_item_picker");
                    }
                    if (ImGui::BeginPopup("##chest_item_picker")) {
                        if (s_picker_entity != sel_entity) {
                            ImGui::CloseCurrentPopup();
                        } else {
                            ImGui::SetNextItemWidth(260.0f);
                            ImGui::InputTextWithHint("##item_filter",
                                                     "search items...",
                                                     s_picker_filter,
                                                     sizeof(s_picker_filter));
                            std::string filter = s_picker_filter;
                            std::transform(filter.begin(), filter.end(),
                                           filter.begin(), ::tolower);
                            ImGui::BeginChild("##item_list",
                                              ImVec2(320.0f, 300.0f), true);
                            std::vector<int> rows;
                            rows.reserve(g_item_details.size());
                            for (int ci = 0;
                                 ci < int(g_item_details.size());
                                 ++ci) {
                                if (filter.empty()) {
                                    rows.push_back(ci);
                                    continue;
                                }
                                std::string low =
                                    g_item_details[size_t(ci)]
                                        .display_name;
                                std::transform(low.begin(), low.end(),
                                               low.begin(), ::tolower);
                                if (low.find(filter) !=
                                    std::string::npos) {
                                    rows.push_back(ci);
                                }
                            }
                            ImGuiListClipper clipper;
                            clipper.Begin(int(rows.size()));
                            while (clipper.Step()) {
                                for (int ri = clipper.DisplayStart;
                                     ri < clipper.DisplayEnd; ++ri) {
                                    const auto& c = g_item_details
                                        [size_t(rows[size_t(ri)])];
                                    const std::string& pl =
                                        c.display_name.empty()
                                            ? c.label
                                            : c.display_name;
                                    ImGui::PushID(int(c.record_hash));
                                    if (ImGui::Selectable(pl.c_str())) {
                                        std::vector<uint32_t> next =
                                            shown_items;
                                        if (size_t(s_picker_slot) <
                                            next.size()) {
                                            next[size_t(s_picker_slot)] =
                                                c.record_hash;
                                        } else {
                                            next.push_back(c.record_hash);
                                        }
                                        LevelEdit::SetChestContents(
                                            sel_entity, next);
                                        ImGui::CloseCurrentPopup();
                                    }
                                    ImGui::PopID();
                                }
                            }
                            ImGui::EndChild();
                        }
                        ImGui::EndPopup();
                    }
                }

                draw_container_loot_table_editor(sel_entity,
                                                 *sel_contents);
            } else if (sel_chest_addition >= 0) {
                auto pretty_tag2 = [](std::string tag, int money) {
                    for (const char* pfx : { "INV_ITEM_", "OBJECT_",
                                             "TEXT_" }) {
                        const size_t n = std::strlen(pfx);
                        if (tag.size() > n && tag.compare(0, n, pfx) == 0) {
                            tag = tag.substr(n);
                            break;
                        }
                    }
                    if (tag.size() > 5 &&
                        tag.compare(tag.size() - 5, 5, "_NAME") == 0) {
                        tag.resize(tag.size() - 5);
                    }
                    if (tag.find('_') != std::string::npos ||
                        std::none_of(tag.begin(), tag.end(),
                                     [](unsigned char c) {
                                         return std::islower(c);
                                     })) {
                        bool ws = true;
                        for (auto& c : tag) {
                            if (c == '_') {
                                c = ' ';
                                ws = true;
                            } else {
                                c = ws ? char(std::toupper((unsigned char)c))
                                       : char(std::tolower((unsigned char)c));
                                ws = false;
                            }
                        }
                    }
                    if (money >= 0) {
                        tag += " (" + std::to_string(money) + " gold)";
                    }
                    return tag;
                };
                auto catalog_label2 = [&](uint32_t record_hash) {
                    for (const auto& c : g_item_details) {
                        if (c.record_hash == record_hash &&
                            !c.display_name.empty()) {
                            return c.display_name;
                        }
                    }
                    for (const auto& c : g_level_item_catalog) {
                        if (c.record_hash == record_hash) {
                            return pretty_tag2(c.label, c.money);
                        }
                    }
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "0x%08X", record_hash);
                    return std::string(buf);
                };
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
                                   LevelEdit::AdditionIsDigSpot(
                                       sel_chest_addition)
                                       ? "New dig spot (unsaved)"
                                       : "New container (unsaved)");
                std::vector<uint32_t> add_items;
                LevelEdit::GetAdditionChestItems(sel_chest_addition,
                                                 add_items);
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Contents:");
                const bool add_editable =
                    LevelEdit::Enabled() && !LevelEdit::Saving();
                static char s_add_filter[64] = {};
                bool open_add_picker = false;
                static int s_add_slot = -1;
                int remove_idx = -1;
                for (size_t ii = 0; ii < add_items.size(); ++ii) {
                    ImGui::PushID(int(ii) + 0x1000);
                    if (add_editable) {
                        if (ImGui::SmallButton("x")) remove_idx = int(ii);
                        ImGui::SameLine();
                        if (ImGui::Selectable(
                                catalog_label2(add_items[ii]).c_str(),
                                false,
                                ImGuiSelectableFlags_DontClosePopups)) {
                            s_add_slot = int(ii);
                            s_add_filter[0] = 0;
                            open_add_picker = true;
                        }
                    } else {
                        ImGui::BulletText(
                            "%s", catalog_label2(add_items[ii]).c_str());
                    }
                    ImGui::PopID();
                }
                if (add_editable) {
                    if (ImGui::SmallButton("+ Add item")) {
                        s_add_slot = int(add_items.size());
                        s_add_filter[0] = 0;
                        open_add_picker = true;
                    }

                    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                       "Random loot:");
                    const uint32_t cur_loot =
                        LevelEdit::GetAdditionLootTable(
                            sel_chest_addition);
                    std::string cur_label = "None";
                    if (cur_loot) {
                        char lb[32];
                        std::snprintf(lb, sizeof(lb), "table 0x%08X",
                                      cur_loot);
                        cur_label = lb;
                        for (const auto& kv : g_level_entity_contents) {
                            if (kv.second.potential_items_record ==
                                    cur_loot &&
                                !kv.second.entity_name.empty()) {
                                cur_label =
                                    "like " + kv.second.entity_name;
                                break;
                            }
                        }
                    }
                    ImGui::SetNextItemWidth(240.0f);
                    if (ImGui::BeginCombo("##rand_loot",
                                          cur_label.c_str())) {
                        if (ImGui::Selectable("None", cur_loot == 0)) {
                            LevelEdit::SetAdditionLootTable(
                                sel_chest_addition, 0);
                        }
                        std::unordered_set<uint32_t> seen_tables;
                        for (const auto& kv : g_level_entity_contents) {
                            const auto& ec = kv.second;
                            if (!ec.potential_items_record ||
                                ec.potential_items.empty()) {
                                continue;
                            }
                            if (!seen_tables
                                     .insert(ec.potential_items_record)
                                     .second) {
                                continue;
                            }
                            char lbl[128];
                            std::snprintf(
                                lbl, sizeof(lbl),
                                "%s (%zu entr%s)##%08X",
                                ec.entity_name.empty()
                                    ? "<unnamed>"
                                    : ec.entity_name.c_str(),
                                ec.potential_items.size(),
                                ec.potential_items.size() == 1 ? "y"
                                                               : "ies",
                                ec.potential_items_record);
                            if (ImGui::Selectable(
                                    lbl,
                                    cur_loot ==
                                        ec.potential_items_record)) {
                                LevelEdit::SetAdditionLootTable(
                                    sel_chest_addition,
                                    ec.potential_items_record);
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Give this container a weighted random loot "
                            "table borrowed from an existing container "
                            "in this level.");
                    }
                }
                if (remove_idx >= 0 &&
                    size_t(remove_idx) < add_items.size()) {
                    add_items.erase(add_items.begin() + remove_idx);
                    LevelEdit::SetAdditionChestItems(sel_chest_addition,
                                                     add_items);
                }
                if (open_add_picker) {
                    ImGui::OpenPopup("##add_chest_item_picker");
                }
                if (ImGui::BeginPopup("##add_chest_item_picker")) {
                    ImGui::SetNextItemWidth(260.0f);
                    ImGui::InputTextWithHint("##add_item_filter",
                                             "search items...",
                                             s_add_filter,
                                             sizeof(s_add_filter));
                    std::string filter = s_add_filter;
                    std::transform(filter.begin(), filter.end(),
                                   filter.begin(), ::tolower);
                    ImGui::BeginChild("##add_item_list",
                                      ImVec2(320.0f, 300.0f), true);
                    std::vector<int> rows;
                    rows.reserve(g_item_details.size());
                    for (int ci = 0;
                         ci < int(g_item_details.size()); ++ci) {
                        if (filter.empty()) {
                            rows.push_back(ci);
                            continue;
                        }
                        std::string low =
                            g_item_details[size_t(ci)].display_name;
                        std::transform(low.begin(), low.end(),
                                       low.begin(), ::tolower);
                        if (low.find(filter) != std::string::npos) {
                            rows.push_back(ci);
                        }
                    }
                    ImGuiListClipper clipper;
                    clipper.Begin(int(rows.size()));
                    while (clipper.Step()) {
                        for (int ri = clipper.DisplayStart;
                             ri < clipper.DisplayEnd; ++ri) {
                            const auto& c = g_item_details
                                [size_t(rows[size_t(ri)])];
                            const std::string& pl =
                                c.display_name.empty() ? c.label
                                                       : c.display_name;
                            ImGui::PushID(int(c.record_hash));
                            if (ImGui::Selectable(pl.c_str())) {
                                if (size_t(s_add_slot) <
                                    add_items.size()) {
                                    add_items[size_t(s_add_slot)] =
                                        c.record_hash;
                                } else {
                                    add_items.push_back(c.record_hash);
                                }
                                LevelEdit::SetAdditionChestItems(
                                    sel_chest_addition, add_items);
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndChild();
                    ImGui::EndPopup();
                }
            }
        }
        ImGui::End();
        }   
        if (dbg_sel_changed) DebugTrace::log("sel: overlay done");

        if (edit_active) {
            if (dbg_sel_changed) DebugTrace::log("gz: call");
            LevelGizmo::Result gz = LevelGizmo::DrawAndHandle(
                g_flycam, origin, region, sel_pos, true);
            static bool s_was_dragging = false;
            if (gz.dragging && !s_was_dragging) {
                LevelEdit::PushUndoSnapshot(collect_group_ids());
                DebugTrace::log("gizmo: drag begin");
            }
            s_was_dragging = gz.dragging;
            if (gz.moved) {
                if (gz.step[0] != 0.0f || gz.step[1] != 0.0f ||
                    gz.step[2] != 0.0f) {
                    apply_group_edit(kEditMove, gz.step);
                }
                if (gz.rot_step_deg[0] != 0.0f ||
                    gz.rot_step_deg[1] != 0.0f ||
                    gz.rot_step_deg[2] != 0.0f) {
                    apply_group_edit(kEditRotate, gz.rot_step_deg);
                }
            }
        } else {
            LevelGizmo::CancelDrag();
        }
        if (dbg_sel_changed) DebugTrace::log("sel: gizmo done");

        if (edit_active && !ImGui::GetIO().WantTextInput &&
            ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            DebugTrace::log("del: id=%u hash=%llu",
                            ::g_selected_level_pick_id,
                            (unsigned long long)::g_selected_level_hash);
            bool removed_spawn_point = false;
            const int marker_index = selected_level_spawn_marker_index();
            if (marker_index >= 0) {
                const LevelSpawnMarker& marker =
                    g_level_spawn_markers[size_t(marker_index)];
                if (marker.kind == 2) {
                    for (const LevelSpawnMarker& owner :
                         g_level_spawn_markers) {
                        if (owner.kind != 1 ||
                            owner.spawn_points_record == 0 ||
                            std::find(owner.spawn_point_entities.begin(),
                                      owner.spawn_point_entities.end(),
                                      marker.entity_hash) ==
                                owner.spawn_point_entities.end()) {
                            continue;
                        }
                        LevelEdit::RemoveSpawnPointFromExisting(
                            owner.entity_hash, owner.spawn_points_record,
                            marker.entity_hash);
                        OutputLog::info(
                            "level edit: spawn point deletion queued");
                        removed_spawn_point = true;
                        break;
                    }
                }
            }
            if (!removed_spawn_point) {
                LevelEdit::PushUndoSnapshot(collect_group_ids());
                apply_group_edit(kEditDelete, nullptr);
            }
            ::g_selected_level_mesh_idx = -1;
            ::g_selected_level_pick_id = 0;
            ::g_selected_level_hash = 0;
            LevelGizmo::CancelDrag();
        }
    }

    if (g_mp.no_tilt && LevelEdit::Enabled() && hovered &&
        !g_flycam.is_looking && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
            LevelGizmo::SetMode(LevelGizmo::Mode::Translate);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
            LevelGizmo::SetMode(LevelGizmo::Mode::Rotate);
        }
    }

    if (LevelEdit::Enabled() && !ImGui::GetIO().WantTextInput &&
        (ImGui::GetIO().KeyAlt || ImGui::GetIO().KeyCtrl) &&
        ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (!LevelEdit::Undo()) {
            OutputLog::info("level edit: nothing to undo");
        }
    }

    if (hovered && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (S.bone_rotate_mode) {
            cancel_rotate();
        } else if (g_mp.no_tilt && ::g_selected_level_mesh_idx >= 0) {
            ::g_selected_level_mesh_idx = -1;
            ::g_selected_level_pick_id = 0;
            ::g_selected_level_hash = 0;
            LevelGizmo::CancelDrag();
        } else if (g_mp.no_tilt) {

        } else {
            if (S.content_tabs_visible && ContentTabs::HasTabs()) {
                ContentTabs::CloseActive();
            } else {
                MP_Release(g_mp);
                g_mp.has_model = false;
                g_mp_initialized = false;
                S.show_model_preview = false;
                S.model_preview_open = false;
                S.selected_bone = -1;
            }
        }
    }

    dl->AddRectFilled(ImVec2(origin.x + 6, origin.y + 6),
                      ImVec2(origin.x + 196, origin.y + 70),
                      IM_COL32(20, 22, 28, 200), 4.0f);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 12));
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Controls");
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 30));
    ImGui::TextDisabled("L-Drag  rotate");
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 46));
    ImGui::TextDisabled("Wheel  zoom  /  ESC  close");

    float next_overlay_y = origin.y + 76.0f;

    
    
    const bool custom_level_clean_viewport = details_panel_docked();

    bool has_skeleton = g_mp.has_model && g_mp.bone_count > 0 &&
                        !custom_level_clean_viewport;
    if (has_skeleton) {

        static float s_skel_alpha    = 0.30f;

        const float kIdleAlpha   = 0.30f;
        const float kHoverAlpha  = 1.00f;

        const ImVec2 win_pos (origin.x + 6, origin.y + 76);
        const ImVec2 win_size(190, 0);
        ImGui::SetNextWindowPos(win_pos);
        ImGui::SetNextWindowSize(win_size, ImGuiCond_Always);

        ImGui::SetNextWindowBgAlpha(s_skel_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_skel_alpha);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##skeleton_overlay", nullptr, fl)) {

            bool hovering = ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                ImGuiHoveredFlags_ChildWindows);
            float target = hovering ? kHoverAlpha : kIdleAlpha;

            s_skel_alpha += (target - s_skel_alpha) * 0.18f;
            if (std::fabs(s_skel_alpha - target) < 0.005f) s_skel_alpha = target;

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Skeleton");
            ImGui::Checkbox("Show", &::g_skel_overlay_show);
            if (S.selected_bone >= 0 && S.selected_bone < (int)S.mdl_info.Bones.size()) {

                ImGui::TextDisabled(S.bone_rotate_mode
                                        ? "RMB cancel  /  LMB confirm"
                                        : "R: rotate selected");
            }

            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            next_overlay_y = wp.y + ws.y + 6.0f;
        }
        ImGui::End();
        ImGui::PopStyleVar();

        if (!::g_skel_overlay_show) {
            S.selected_bone     = -1;
            S.bone_rotate_mode  = false;
        }

        if (::g_skel_overlay_show) {
            draw_skeleton_overlay(origin, region);
        }
    } else {

        ::g_skel_overlay_show = false;
        S.selected_bone       = -1;
        S.bone_rotate_mode    = false;
    }

    if (g_mp.has_model && !custom_level_clean_viewport) {
        static float s_wire_alpha = 0.30f;
        const float kIdleAlpha   = 0.30f;
        const float kHoverAlpha  = 1.00f;

        const ImVec2 wire_pos (origin.x + 6, next_overlay_y);
        const ImVec2 wire_size(190, 0);
        ImGui::SetNextWindowPos(wire_pos);
        ImGui::SetNextWindowSize(wire_size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(s_wire_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_wire_alpha);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##wireframe_overlay", nullptr, fl)) {
            bool hovering = ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                ImGuiHoveredFlags_ChildWindows);
            float target = hovering ? kHoverAlpha : kIdleAlpha;
            s_wire_alpha += (target - s_wire_alpha) * 0.18f;
            if (std::fabs(s_wire_alpha - target) < 0.005f) s_wire_alpha = target;

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Wireframe");
            ImGui::Checkbox("Show", &g_mp.wireframe);
            if (g_mp.no_tilt && (!g_level_spawn_markers.empty() ||
                                 !g_level_entity_text.empty())) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Entities");
                ImGui::Checkbox("Generators / spawn points",
                                &S.show_spawn_markers);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Creature generators (red) and their spawn "
                        "points (orange). In edit mode: click to "
                        "select, Right-click ground to add a "
                        "new generator.");
                ImGui::Checkbox("NPC / creature markers",
                                &S.show_ent_npcs);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Show the labelled diamond at each authored NPC or "
                        "creature position. Markers are not selectable.");
                }
                if (ImGui::Checkbox("Entity models",
                                    &S.show_entity_models) &&
                    !S.show_entity_models &&
                    ::g_selected_level_mesh_idx >= 0 &&
                    ::g_selected_level_mesh_idx <
                        static_cast<int>(g_mp.meshes.size()) &&
                    g_mp.meshes[static_cast<size_t>(
                        ::g_selected_level_mesh_idx)].is_entity_model) {
                    ::g_selected_level_mesh_idx = -1;
                    ::g_selected_level_pick_id = 0;
                    ::g_selected_level_hash = 0;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Render the resolved full NPC and creature models. "
                        "Select and move entities by clicking their model.");
                }
                ImGui::Checkbox("Containers", &S.show_containers);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "All inventory-bearing objects (purple), including "
                        "chests, cupboards, registers, and barrels. "
                        "Dig spots remain under their own "
                        "checkbox.");
                }
                ImGui::Checkbox("Dig spots", &S.show_dig_spots);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Dig spots (blue). Click one to inspect its loot, "
                        "search radius, priority, and respawn chance when "
                        "those values are present.");
                }
                ImGui::Checkbox("Text objects", &S.show_ent_text);
            }
            if (S.dev_mode) {
                ImGui::Checkbox("Terrain: engine blend",
                                &S.terrain_landscape_blend);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Dev-only: engine-reconciled LANDSCAPEMATERIAL terrain "
                        "blend (per-material tiling, 16/dim). A/B vs the current "
                        "shared-scale shader.");
            }
            if (g_mp.has_sky_theme) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Time");

                const bool has_cycle =
                    g_mp.has_day_night_cycle &&
                    g_mp.day_night_keyframes.size() >= 2;
                bool auto_time =
                    has_cycle && !g_mp.time_of_day_override;
                ImGui::BeginDisabled(!has_cycle);
                if (ImGui::Checkbox("Auto", &auto_time)) {
                    if (auto_time) {
                        g_mp.time_of_day_override = false;
                    } else {
                        g_mp.time_of_day_override = true;
                        g_mp.time_of_day_override_value =
                            g_mp.current_time_of_day;
                    }
                }
                ImGui::EndDisabled();

                float hour =
                    (g_mp.time_of_day_override
                         ? g_mp.time_of_day_override_value
                         : g_mp.current_time_of_day) * 24.0f;
                hour = std::clamp(hour, 0.0f, 24.0f);
                ImGui::SetNextItemWidth(150.0f);
                if (ImGui::SliderFloat("##time_of_day", &hour,
                                       0.0f, 24.0f, "%.2f h",
                                       ImGuiSliderFlags_AlwaysClamp)) {
                    g_mp.time_of_day_override = true;
                    g_mp.time_of_day_override_value =
                        std::clamp(hour / 24.0f, 0.0f, 1.0f);
                }
            }
            if (S.terrain_mode || g_mp.has_sky_theme ||
                g_mp.has_weather_theme ||
                g_mp.has_fog_theme) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Environment");
                if (S.terrain_mode) {
                    ImGui::Checkbox("Adjacent terrain",
                                    &S.show_adjacent_terrain);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Show the neighbouring levels' heightfields "
                            "around this level (textured with their baked "
                            "ground).");
                    }
                }
                if (g_mp.has_sky_theme) {
                    ImGui::Checkbox("Sky", &g_mp.show_sky);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            "Procedural sky, sun/moon and cloud layers "
                            "from the level's environment theme.");
                }
                if (g_mp.has_weather_theme) {
                    const bool theme_has_rain =
                        g_mp.weather_precip[0] > 0.0001f &&
                        g_mp.weather_precip[1] > 0.0001f;
                    const bool theme_has_snow =
                        g_mp.weather_precip[2] > 0.0001f &&
                        g_mp.weather_precip[3] > 0.0001f;
                    ImGui::Checkbox("Weather", &g_mp.show_weather);
                    if (ImGui::IsItemHovered()) {
                        if (theme_has_rain || theme_has_snow) {
                            char buf[160];
                            std::snprintf(
                                buf, sizeof(buf),
                                "Theme precipitation:%s%s\n"
                                "rain density %.2f size %.2f\n"
                                "snow fallspeed %.2f size %.2f",
                                theme_has_rain ? " rain" : "",
                                theme_has_snow ? " snow" : "",
                                g_mp.weather_precip[0],
                                g_mp.weather_precip[1],
                                g_mp.weather_precip[2],
                                g_mp.weather_precip[3]);
                            ImGui::SetTooltip("%s", buf);
                        } else {
                            ImGui::SetTooltip(
                                "This level's theme has no rain or "
                                "snow at the current time of day.");
                        }
                    }
                }
                if (g_mp.has_weather_theme || g_mp.has_fog_theme) {
                    ImGui::Checkbox("Mist / fog", &g_mp.show_mist);
                    if (ImGui::IsItemHovered()) {
                        if (g_mp.weather_mist_strength > 0.0001f ||
                            g_mp.has_fog_theme) {
                            char buf[120];
                            std::snprintf(
                                buf, sizeof(buf),
                                "Theme fogging + ground mist "
                                "(GroundMist strength %.2f).",
                                g_mp.weather_mist_strength);
                            ImGui::SetTooltip("%s", buf);
                        } else {
                            ImGui::SetTooltip(
                                "This level's theme has no fogging or "
                                "ground mist parameters.");
                        }
                    }
                }
            }

            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            next_overlay_y = wp.y + ws.y + 6.0f;
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    if (g_mp.has_model && g_mp.lod_count > 1 &&
        !details_panel_docked()) {
        static float s_lod_alpha = 0.30f;
        const float kIdleAlpha   = 0.30f;
        const float kHoverAlpha  = 1.00f;

        const ImVec2 lod_pos (origin.x + 6, next_overlay_y);
        const ImVec2 lod_size(190, 0);
        ImGui::SetNextWindowPos(lod_pos);
        ImGui::SetNextWindowSize(lod_size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(s_lod_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_lod_alpha);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##lod_overlay", nullptr, fl)) {
            bool hovering = ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                ImGuiHoveredFlags_ChildWindows);
            float target = hovering ? kHoverAlpha : kIdleAlpha;
            s_lod_alpha += (target - s_lod_alpha) * 0.18f;
            if (std::fabs(s_lod_alpha - target) < 0.005f) s_lod_alpha = target;

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "LOD");

            const int lod_count = (int)g_mp.lod_count;
            int current = g_mp.selected_lod;
            if (current < -1 || current >= lod_count) current = 0;

            if (ImGui::RadioButton("All", current == -1)) {
                g_mp.selected_lod = -1;
            }
            for (int i = 0; i < lod_count; ++i) {
                ImGui::SameLine();
                char lbl[16];
                std::snprintf(lbl, sizeof(lbl), "%d", i);
                if (ImGui::RadioButton(lbl, current == i)) {
                    g_mp.selected_lod = i;
                }
            }

            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            next_overlay_y = wp.y + ws.y + 6.0f;
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    if (g_mp.has_model && !g_mp.meshes.empty() &&
        !custom_level_clean_viewport) {
        static float s_mat_alpha = 0.30f;
        const float kIdleAlpha   = 0.30f;
        const float kHoverAlpha  = 1.00f;

        const float kMatW = 296.0f;
        float max_h = std::max(160.0f,
                               region.y - (next_overlay_y - origin.y) - 20.0f);

        ImGui::SetNextWindowPos(ImVec2(origin.x + 6, next_overlay_y));
        ImGui::SetNextWindowSizeConstraints(ImVec2(kMatW, 0.0f),
                                            ImVec2(kMatW, max_h));
        ImGui::SetNextWindowBgAlpha(s_mat_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_mat_alpha);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##materials_overlay", nullptr, fl)) {
            bool hovering = ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                ImGuiHoveredFlags_ChildWindows);
            float target = hovering ? kHoverAlpha : kIdleAlpha;
            s_mat_alpha += (target - s_mat_alpha) * 0.18f;
            if (std::fabs(s_mat_alpha - target) < 0.005f) s_mat_alpha = target;

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Materials");
            ImGui::Separator();

            const ImVec2 thumb_size(48, 48);

            if (!g_mp.no_tilt) for (size_t mi = 0; mi < g_mp.meshes.size(); ++mi) {
                auto& mesh = g_mp.meshes[mi];

                if (g_mp.selected_lod >= 0 &&
                    mesh.lod_index != (uint32_t)g_mp.selected_lod) {
                    continue;
                }

                ImGui::PushID((int)mi);

                ImGui::TextUnformatted(mesh.name.c_str());

                bool h   = (::g_highlight_mesh_idx == (int)mi);
                bool iso = (::g_isolate_mesh_idx   == (int)mi);

                if (ImGui::Checkbox("Highlight", &h)) {
                    if (h) {
                        ::g_highlight_mesh_idx = (int)mi;
                        ::g_isolate_mesh_idx   = -1;
                    } else if (::g_highlight_mesh_idx == (int)mi) {
                        ::g_highlight_mesh_idx = -1;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Isolate", &iso)) {
                    if (iso) {
                        ::g_isolate_mesh_idx   = (int)mi;
                        ::g_highlight_mesh_idx = -1;
                    } else if (::g_isolate_mesh_idx == (int)mi) {
                        ::g_isolate_mesh_idx = -1;
                    }
                }

                struct ThumbSpec {
                    const char*               slot_id;
                    ID3D11ShaderResourceView* srv;
                    const std::string*        name;
                    bool*                     visible;
                };
                ThumbSpec thumbs[5] = {
                    {"diffuse",  mesh.srv_diffuse,  &mesh.diffuse_tex_name,  &mesh.diffuse_visible},
                    {"normal",   mesh.srv_normal,   &mesh.normal_tex_name,   &mesh.normal_visible},
                    {"specular", mesh.srv_specular, &mesh.specular_tex_name, &mesh.specular_visible},
                    {"metallic", mesh.srv_metallic, &mesh.metallic_tex_name, &mesh.metallic_visible},
                    {"extra",    mesh.srv_extra,    &mesh.extra_tex_name,    &mesh.extra_visible},
                };
                bool any_thumb = false;
                for (int ti = 0; ti < 5; ++ti) {
                    const ThumbSpec& t = thumbs[ti];
                    if (!t.srv || t.srv == g_mp.default_srv) continue;
                    if (t.name->empty()) continue;
                    if (any_thumb) ImGui::SameLine();
                    any_thumb = true;
                    ImGui::PushID(t.slot_id);

                    ImGui::BeginGroup();

                    ImVec4 tint = (*t.visible) ? ImVec4(1, 1, 1, 1)
                                               : ImVec4(0.45f, 0.45f, 0.45f, 1);
                    if (ImGui::ImageButton("##t",
                                           (ImTextureID)t.srv,
                                           thumb_size,
                                           ImVec2(0, 0), ImVec2(1, 1),
                                           ImVec4(0, 0, 0, 0), tint)) {
                        ::g_tex_popout_srv      = t.srv;
                        ::g_tex_popout_name     = *t.name;
                        ::g_tex_popout_open     = true;

                        ::g_tex_popout_mesh_idx = (int)mi;
                    }

                    if (ImGui::BeginPopupContextItem()) {
                        const auto* terrain_tex =
                            TerrainTextureRegistry::Find(*t.name);
                        if (terrain_tex) {
                            tex_export_menu_rgba(*t.name,
                                                 terrain_tex->rgba,
                                                 terrain_tex->width,
                                                 terrain_tex->height);
                        } else {
                            const std::string& preferred_bnk =
                                (S.selected_nested_index != -1 &&
                                 !S.selected_nested_temp_path.empty())
                                    ? S.selected_nested_temp_path
                                    : S.selected_bnk;
                            tex_export_menu_named(*t.name, *t.name,
                                                  preferred_bnk, 0);
                        }
                        ImGui::EndPopup();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s\n[%s]",
                                          t.name->c_str(), t.slot_id);
                    }

                    ImGui::Checkbox("##vis", t.visible);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Show %s in render", t.slot_id);
                    }
                    ImGui::EndGroup();
                    ImGui::PopID();
                }
                if (!any_thumb) {
                    ImGui::TextDisabled("(no textures)");
                }

                ImGui::Separator();
                ImGui::PopID();
            }

            if (g_mp.no_tilt && ::g_selected_level_mesh_idx >= 0 &&
                ::g_selected_level_mesh_idx < (int)g_mp.meshes.size())
            {
                const int mi   = ::g_selected_level_mesh_idx;
                auto&     mesh = g_mp.meshes[mi];
                const std::string selected_model_key =
                    level_model_key_from_mesh_name(mesh.name);
                const std::string selected_model_name =
                    clean_level_model_name(mesh.name);

                ImGui::TextWrapped("%s", selected_model_name.c_str());
                ImGui::Separator();

                ImGui::PushID(0x20000 + mi);

                struct ThumbSpec {
                    const char*               slot_id;
                    ID3D11ShaderResourceView* srv;
                    const std::string*        name;
                    int                       mesh_idx;
                    std::vector<bool*>        visible;
                };
                struct MaterialRow {
                    std::string key;
                    std::string label;
                    std::array<ThumbSpec, 5> thumbs;
                };

                auto make_row_key = [](const MPPerMesh& m,
                                       const std::string& label) {
                    return label + "|" +
                           m.diffuse_tex_name + "|" +
                           m.normal_tex_name + "|" +
                           m.specular_tex_name + "|" +
                           m.metallic_tex_name + "|" +
                           m.extra_tex_name;
                };

                std::vector<MaterialRow> rows;
                auto append_row_mesh =
                    [&](MPPerMesh& related, int related_idx) {
                        const std::string label =
                            clean_level_material_name(related.name);
                        const std::string key = make_row_key(related, label);
                        MaterialRow* row = nullptr;
                        for (auto& existing : rows) {
                            if (existing.key == key) {
                                row = &existing;
                                break;
                            }
                        }
                        if (!row) {
                            MaterialRow fresh;
                            fresh.key = key;
                            fresh.label = label;
                            fresh.thumbs = {{
                                {"diffuse",  related.srv_diffuse,
                                 &related.diffuse_tex_name, related_idx, {}},
                                {"normal",   related.srv_normal,
                                 &related.normal_tex_name, related_idx, {}},
                                {"specular", related.srv_specular,
                                 &related.specular_tex_name, related_idx, {}},
                                {"metallic", related.srv_metallic,
                                 &related.metallic_tex_name, related_idx, {}},
                                {"extra",    related.srv_extra,
                                 &related.extra_tex_name, related_idx, {}},
                            }};
                            rows.push_back(std::move(fresh));
                            row = &rows.back();
                        }

                        row->thumbs[0].visible.push_back(
                            &related.diffuse_visible);
                        row->thumbs[1].visible.push_back(
                            &related.normal_visible);
                        row->thumbs[2].visible.push_back(
                            &related.specular_visible);
                        row->thumbs[3].visible.push_back(
                            &related.metallic_visible);
                        row->thumbs[4].visible.push_back(
                            &related.extra_visible);
                    };

                for (size_t ri = 0; ri < g_mp.meshes.size(); ++ri) {
                    auto& related = g_mp.meshes[ri];
                    if (level_model_key_from_mesh_name(related.name) !=
                        selected_model_key)
                    {
                        continue;
                    }
                    append_row_mesh(related, (int)ri);
                }

                if (rows.empty()) {
                    ImGui::TextDisabled("(no materials)");
                }
                for (size_t row_i = 0; row_i < rows.size(); ++row_i) {
                    MaterialRow& row = rows[row_i];
                    ImGui::PushID((int)row_i);
                    ImGui::TextUnformatted(row.label.c_str());

                    bool any_thumb = false;
                    for (size_t ti = 0; ti < row.thumbs.size(); ++ti) {
                        ThumbSpec& t = row.thumbs[ti];
                        if (!t.srv || t.srv == g_mp.default_srv) continue;
                        if (!t.name || t.name->empty()) continue;
                        if (any_thumb) ImGui::SameLine();
                        any_thumb = true;
                        ImGui::PushID((int)ti);
                        ImGui::BeginGroup();

                        bool visible = true;
                        for (bool* v : t.visible) {
                            if (v && !*v) {
                                visible = false;
                                break;
                            }
                        }

                        ImVec4 tint = visible
                            ? ImVec4(1, 1, 1, 1)
                            : ImVec4(0.45f, 0.45f, 0.45f, 1);
                        if (ImGui::ImageButton("##t",
                                               (ImTextureID)t.srv,
                                               thumb_size,
                                               ImVec2(0, 0), ImVec2(1, 1),
                                               ImVec4(0, 0, 0, 0), tint)) {
                            ::g_tex_popout_srv      = t.srv;
                            ::g_tex_popout_name     = *t.name;
                            ::g_tex_popout_open     = true;
                            ::g_tex_popout_mesh_idx = t.mesh_idx;
                        }
                        if (ImGui::BeginPopupContextItem()) {
                            const auto* terrain_tex =
                                TerrainTextureRegistry::Find(*t.name);
                            if (terrain_tex) {
                                tex_export_menu_rgba(*t.name,
                                                     terrain_tex->rgba,
                                                     terrain_tex->width,
                                                     terrain_tex->height);
                            } else {
                                const std::string& preferred_bnk =
                                    (S.selected_nested_index != -1 &&
                                     !S.selected_nested_temp_path.empty())
                                        ? S.selected_nested_temp_path
                                        : S.selected_bnk;
                                tex_export_menu_named(*t.name, *t.name,
                                                      preferred_bnk, 0);
                            }
                            ImGui::EndPopup();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s\n[%s]",
                                              t.name->c_str(), t.slot_id);
                        }
                        bool checkbox_visible = visible;
                        if (ImGui::Checkbox("##vis", &checkbox_visible)) {
                            for (bool* v : t.visible) {
                                if (v) *v = checkbox_visible;
                            }
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Show %s in render", t.slot_id);
                        }
                        ImGui::EndGroup();
                        ImGui::PopID();
                    }
                    if (!any_thumb) {
                        ImGui::TextDisabled("(no textures)");
                    }
                    ImGui::Separator();
                    ImGui::PopID();
                }
                ImGui::PopID();
            } else if (g_mp.no_tilt) {
                const auto& lod = EhfLodThumbnails::Get();
                if (!lod.empty()) {
                    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f),
                        ".ehf LOD palette");
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%zu materials)", lod.size());
                    ImGui::Separator();

                    auto basename = [](const std::string& s) -> std::string {
                        if (s.empty()) return {};
                        size_t pos = s.find_last_of("/\\");
                        return (pos == std::string::npos)
                            ? s : s.substr(pos + 1);
                    };

                    auto thumb_or_placeholder =
                        [&](ID3D11ShaderResourceView* srv,
                            const std::string& path,
                            const char* slot_tag,
                            int slot_idx)
                    {
                        const ImVec2 sz(48, 48);
                        ImGui::PushID(slot_idx);
                        if (srv) {
                            if (ImGui::ImageButton("##t",
                                (ImTextureID)srv, sz,
                                ImVec2(0, 0), ImVec2(1, 1),
                                ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, 1)))
                            {
                                ::g_tex_popout_srv      = srv;
                                ::g_tex_popout_name     = path;
                                ::g_tex_popout_open     = true;
                                ::g_tex_popout_mesh_idx = -1;
                            }
                            if (ImGui::BeginPopupContextItem()) {
                                const std::string& preferred_bnk =
                                    (S.selected_nested_index != -1 &&
                                     !S.selected_nested_temp_path.empty())
                                        ? S.selected_nested_temp_path
                                        : S.selected_bnk;
                                tex_export_menu_named(path, path,
                                                      preferred_bnk, 0);
                                ImGui::EndPopup();
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s\n[%s]",
                                                  path.c_str(), slot_tag);
                            }
                        } else {
                            ImGui::Dummy(sz);
                            if (!path.empty() && ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("decode failed: %s\n[%s]",
                                                  path.c_str(), slot_tag);
                            }
                        }
                        ImGui::PopID();
                    };

                    for (size_t i = 0; i < lod.size(); ++i) {
                        const auto& e = lod[i];
                        ImGui::PushID(int(0x10000 + i));

                        const std::string title = "[" + std::to_string(i)
                            + "] " + basename(e.base_diffuse_path);
                        ImGui::TextUnformatted(title.c_str());

                        thumb_or_placeholder(e.srv_base_diffuse,
                                             e.base_diffuse_path,
                                             "base diffuse", 0);
                        ImGui::SameLine();
                        thumb_or_placeholder(e.srv_base_normal,
                                             e.base_normal_path,
                                             "base normal", 1);
                        ImGui::SameLine();
                        thumb_or_placeholder(e.srv_detail_diffuse,
                                             e.detail_diffuse_path,
                                             "detail diffuse", 2);
                        ImGui::SameLine();
                        thumb_or_placeholder(e.srv_detail_normal,
                                             e.detail_normal_path,
                                             "detail normal", 3);

                        ImGui::Separator();
                        ImGui::PopID();
                    }
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    } else {

        ::g_highlight_mesh_idx       = -1;
        ::g_isolate_mesh_idx         = -1;
        ::g_selected_level_mesh_idx  = -1;
        ::g_selected_level_pick_id   = 0;
        ::g_tex_popout_open          = false;
        ::g_tex_popout_srv           = nullptr;
        ::g_tex_popout_name.clear();
        ::g_tex_popout_mesh_idx      = -1;
    }

    const bool sculpt_mode_active =
        details_panel_docked() && g_mp.has_model && g_mp.no_tilt &&
        LandscapePanel::InSculptMode();
    const bool paint_mode_active =
        details_panel_docked() && g_mp.has_model && g_mp.no_tilt &&
        LandscapePanel::InPaintMode() && TerrainPaint::Active();
    if (sculpt_mode_active || paint_mode_active ||
        (S.dev_mode && g_mp.has_model && g_mp.no_tilt &&
         !details_panel_docked())) {
        enum TerrainTool {
            TT_NONE = 0,
            TT_RAISE,
            TT_LOWER,
            TT_SMOOTH,
            TT_FLATTEN,
            TT_NOISE,
        };
        int&   s_tool          = g_te_ui.tool;
        float& s_brush_size    = g_te_ui.brush_size;
        float& s_brush_strength= g_te_ui.brush_strength;
        bool&  s_has_changes   = g_te_ui.has_changes;
        bool&  s_open_save_confirm = g_te_ui.open_save_confirm;

        auto upload_after_edit = [&]() {
            if (!g_mp.meshes.empty()) {
                TerrainEdit::ApplyToGpu(device, &g_mp.meshes[0]);
            }
            s_has_changes = TerrainEdit::IsDirty();
        };

        if (!sculpt_mode_active) {
        const float kEditW    = 300.0f;
        const float kEditPad  = 6.0f;
        const ImVec2 edit_pos(origin.x + region.x - kEditW - kEditPad,
                              origin.y + kEditPad);

        ImGui::SetNextWindowPos(edit_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints(ImVec2(kEditW, 0.0f),
                                            ImVec2(kEditW, region.y - 2*kEditPad));
        ImGui::SetNextWindowBgAlpha(0.88f);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_AlwaysAutoResize;

        if (ImGui::Begin((std::string(ICON_FA_MOUNTAIN) +
                          "  Edit Terrain###edit_terrain").c_str(),
                         nullptr, fl))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f),
                "%s  Save", ICON_FA_FLOPPY_DISK);
            ImGui::Separator();
            if (s_has_changes) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                    "%s Unsaved changes", ICON_FA_TRIANGLE_EXCLAMATION);
            } else {
                ImGui::TextDisabled("(no pending changes)");
            }
            ImGui::BeginDisabled(!s_has_changes);
            if (ImGui::Button((std::string(ICON_FA_FLOPPY_DISK)
                              + "  Save to .iso").c_str(),
                              ImVec2(-1, 0)))
            {
                s_open_save_confirm = true;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered() && !s_has_changes) {
                ImGui::SetTooltip("No changes to save");
            }

            if (s_open_save_confirm) {
                ImGui::OpenPopup("Confirm Save");
                s_open_save_confirm = false;
            }
            if (ImGui::BeginPopupModal("Confirm Save", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                    "%s  Save modified terrain heights",
                    ICON_FA_TRIANGLE_EXCLAMATION);
                ImGui::TextWrapped(
                    "This will write the modified .ghf as a gzip "
                    "file under  <fable_root>/edited_heightfields/.\n"
                    "Your source ISO is NOT touched.");
                ImGui::Separator();
                ImGui::TextWrapped(
                    "Direct BNK / ISO injection is still on the "
                    "TODO list - for now you'll need to splice the "
                    "saved .ghf back into the ISO externally.");
                ImGui::Spacing();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button,
                    ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                    ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
                if (ImGui::Button((std::string(ICON_FA_FLOPPY_DISK)
                                  + "  Save Anyway").c_str(),
                                  ImVec2(160, 0)))
                {
                    std::string msg;
                    if (TerrainEdit::Save(msg)) {
                        OutputLog::success(
                            "Edit Terrain: saved modified .ghf -> "
                            + msg);
                        s_has_changes = false;
                    } else {
                        OutputLog::error(
                            "Edit Terrain: save failed: " + msg);
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor(2);
                ImGui::EndPopup();
            }

            ImGui::Spacing();
            ImGui::Spacing();

            ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f),
                "%s  Tools", ICON_FA_PAINTBRUSH);
            ImGui::Separator();

            const bool edit_ready = TerrainEdit::IsLoaded();
            ImGui::BeginDisabled(!edit_ready);

            auto tool_btn = [&](const char* label, int tool_id,
                                const ImVec2& sz)
            {
                const bool active = (s_tool == tool_id);
                if (active) {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                        ImVec4(0.30f, 0.55f, 0.30f, 1.f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(0.40f, 0.65f, 0.40f, 1.f));
                }
                if (ImGui::Button(label, sz)) {
                    s_tool = active ? TT_NONE : tool_id;
                }
                if (active) ImGui::PopStyleColor(2);
            };

            const ImVec2 btn_size(ImGui::GetContentRegionAvail().x * 0.49f, 0);
            tool_btn((std::string(ICON_FA_ARROW_UP) + "  Raise##tool").c_str(),
                     TT_RAISE, btn_size);
            ImGui::SameLine();
            tool_btn((std::string(ICON_FA_ARROW_DOWN) + "  Lower##tool").c_str(),
                     TT_LOWER, btn_size);
            tool_btn((std::string(ICON_FA_DROPLET) + "  Smooth##tool").c_str(),
                     TT_SMOOTH, btn_size);
            ImGui::SameLine();
            tool_btn((std::string(ICON_FA_GRIP_LINES) + "  Flatten##tool").c_str(),
                     TT_FLATTEN, btn_size);

            ImGui::Spacing();
            if (ImGui::Button((std::string(ICON_FA_ARROW_ROTATE_LEFT)
                + "  Reset all changes").c_str(),
                ImVec2(-1, 0)))
            {
                TerrainEdit::Reset();
                upload_after_edit();
                s_tool = TT_NONE;
            }

            ImGui::EndDisabled();
            if (!edit_ready && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Terrain edit state not initialized "
                                  "(no .ghf loaded?)");
            }

            ImGui::Spacing();
            ImGui::Spacing();

            ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f),
                "%s  Brush", ICON_FA_BRUSH);
            ImGui::Separator();
            ImGui::SliderFloat("Size##brush",     &s_brush_size,
                               1.0f, 256.0f, "%.0f wu");
            ImGui::SliderFloat("Strength##brush", &s_brush_strength,
                               0.05f, 10.0f, "%.2f wu");

            ImGui::Spacing();
            if (s_tool == TT_NONE) {
                ImGui::TextDisabled("Select a tool, then left-click + "
                                    "drag on the terrain to apply.");
            } else {
                ImGui::TextColored(ImVec4(0.7f, 1.f, 0.7f, 1.f),
                    "Click + drag on terrain to apply.");
            }

            ImGui::BeginDisabled(!edit_ready || s_tool == TT_NONE);
            if (ImGui::Button((std::string(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT)
                + "  Apply to ALL").c_str(),
                ImVec2(-1, 0)))
            {
                switch (s_tool) {
                    case TT_RAISE:   TerrainEdit::RaiseAll(s_brush_strength); break;
                    case TT_LOWER:   TerrainEdit::LowerAll(s_brush_strength); break;
                    case TT_SMOOTH:  TerrainEdit::SmoothAll(); break;
                    case TT_FLATTEN: {
                        const auto& st = TerrainEdit::Get();
                        float sum = 0.f;
                        for (float h : st.heights_current) sum += h;
                        const float avg = st.heights_current.empty() ? 0.f :
                            sum / float(st.heights_current.size());
                        TerrainEdit::FlattenAll(avg);
                        break;
                    }
                    default: break;
                }
                upload_after_edit();
            }
            ImGui::EndDisabled();
        }
        ImGui::End();
        }   

        
        
        int   eff_tool     = s_tool;
        float eff_size     = s_brush_size;
        float eff_strength = s_brush_strength;
        float eff_falloff  = 1.0f;
        if (sculpt_mode_active) {
            const bool lower_mod = ImGui::GetIO().KeyShift;
            switch (LandscapePanel::SculptTool()) {
                case 0: eff_tool = lower_mod ? TT_LOWER : TT_RAISE; break;
                case 1: eff_tool = TT_SMOOTH; break;
                case 2: eff_tool = TT_FLATTEN; break;
                case 3: eff_tool = TT_NOISE; break;
                default: eff_tool = TT_NONE; break;
            }
            eff_size = LandscapePanel::BrushSize();
            const float str01 = LandscapePanel::ToolStrength();
            eff_strength =
                (eff_tool == TT_SMOOTH || eff_tool == TT_FLATTEN)
                    ? str01
                    : str01 * 0.35f;
            eff_falloff = LandscapePanel::BrushFalloff();
        } else if (paint_mode_active) {
            eff_tool = TT_RAISE;   
            eff_size = LandscapePanel::BrushSize();
            eff_strength = LandscapePanel::ToolStrength();
            eff_falloff = LandscapePanel::BrushFalloff();
        }

        if (TerrainEdit::IsLoaded() && eff_tool != TT_NONE) {
            ImVec2 mp_pos  = ImGui::GetIO().MousePos;
            const bool over_view =
                mp_pos.x >= origin.x   && mp_pos.x < origin.x + region.x &&
                mp_pos.y >= origin.y   && mp_pos.y < origin.y + region.y;
            
            
            
            
            const bool imgui_captured =
                !hovered ||
                ImGui::IsPopupOpen(nullptr,
                                   ImGuiPopupFlags_AnyPopupId |
                                       ImGuiPopupFlags_AnyPopupLevel);

            g_te_ui.hover_valid = false;
            if (over_view && g_mp.width > 0 && g_mp.height > 0) {
                using namespace DirectX;

                const float cy = cosf(g_flycam.yaw);
                const float sy = sinf(g_flycam.yaw);
                const float cp = cosf(g_flycam.pitch);
                const float sp = sinf(g_flycam.pitch);
                const float forward[3] = { sy * cp, sp, cy * cp };
                XMVECTOR eye = XMVectorSet(g_flycam.pos[0],
                    g_flycam.pos[1], g_flycam.pos[2], 1);
                XMVECTOR at  = XMVectorSet(g_flycam.pos[0] + forward[0],
                    g_flycam.pos[1] + forward[1],
                    g_flycam.pos[2] + forward[2], 1);
                XMVECTOR up  = XMVectorSet(0, 1, 0, 0);
                XMMATRIX V = XMMatrixLookAtLH(eye, at, up);
                const float fov = XMConvertToRadians(60.0f);
                const float aspect = (float)g_mp.width / (float)g_mp.height;
                const float far_plane = g_mp.radius * 100.0f;
                XMMATRIX P = XMMatrixPerspectiveFovLH(fov, aspect,
                                                     0.05f, far_plane);
                XMMATRIX VP = V * P;
                XMVECTOR det;
                XMMATRIX inv_VP = XMMatrixInverse(&det, VP);

                const float u = (mp_pos.x - origin.x) / region.x;
                const float v = (mp_pos.y - origin.y) / region.y;
                const float ndc_x =  u * 2.f - 1.f;
                const float ndc_y =  1.f - v * 2.f;

                XMVECTOR near_pt = XMVector4Transform(
                    XMVectorSet(ndc_x, ndc_y, 0.f, 1.f), inv_VP);
                XMVECTOR far_pt  = XMVector4Transform(
                    XMVectorSet(ndc_x, ndc_y, 1.f, 1.f), inv_VP);
                near_pt = XMVectorScale(near_pt,
                    1.f / XMVectorGetW(near_pt));
                far_pt  = XMVectorScale(far_pt,
                    1.f / XMVectorGetW(far_pt));
                const float ox = XMVectorGetX(near_pt);
                const float oy = XMVectorGetY(near_pt);
                const float oz = XMVectorGetZ(near_pt);
                const float dx = XMVectorGetX(far_pt) - ox;
                const float dy = XMVectorGetY(far_pt) - oy;
                const float dz = XMVectorGetZ(far_pt) - oz;

                float hx, hy, hz;
                if (TerrainEdit::Raycast(ox, oy, oz, dx, dy, dz,
                                         hx, hy, hz))
                {
                    g_te_ui.hover_valid = true;
                    g_te_ui.hover_x = hx;
                    g_te_ui.hover_y = hy;
                    g_te_ui.hover_z = hz;

                    const int kSeg = 48;
                    ImDrawList* dlay = ImGui::GetForegroundDrawList();
                    auto draw_terrain_ring = [&](float ring_radius,
                                                 ImU32 col,
                                                 float thickness) {
                        if (ring_radius <= 0.01f) return;
                        ImVec2 last_screen{};
                        bool last_valid = false;
                        for (int i = 0; i <= kSeg; ++i) {
                            const float ang =
                                (float)i / (float)kSeg * 6.2831853f;
                            const float wx =
                                hx + cosf(ang) * ring_radius;
                            const float wz =
                                hz + sinf(ang) * ring_radius;
                            const float wy =
                                TerrainEdit::SampleHeightAtWorldXZ(wx, wz);
                            XMVECTOR wpt = XMVectorSet(wx, wy, wz, 1.f);
                            XMVECTOR cs  = XMVector4Transform(wpt, VP);
                            const float ws = XMVectorGetW(cs);
                            if (ws <= 0.f) { last_valid = false; continue; }
                            const float nx = XMVectorGetX(cs) / ws;
                            const float ny = XMVectorGetY(cs) / ws;
                            const float sx = origin.x +
                                (nx * 0.5f + 0.5f) * region.x;
                            const float sy = origin.y +
                                (1.f - (ny * 0.5f + 0.5f)) * region.y;
                            const ImVec2 sc(sx, sy);
                            if (last_valid) {
                                dlay->AddLine(last_screen, sc, col,
                                              thickness);
                            }
                            last_screen = sc;
                            last_valid = true;
                        }
                    };
                    const float radius = eff_size;
                    draw_terrain_ring(radius,
                                      IM_COL32(255, 215, 0, 220), 1.5f);
                    
                    
                    if (eff_falloff > 0.02f && eff_falloff < 0.98f) {
                        draw_terrain_ring(radius * (1.0f - eff_falloff),
                                          IM_COL32(255, 235, 130, 140),
                                          1.0f);
                    }
                    XMVECTOR cpt = XMVector4Transform(
                        XMVectorSet(hx, hy, hz, 1.f), VP);
                    const float cw = XMVectorGetW(cpt);
                    if (cw > 0.f) {
                        const float cnx = XMVectorGetX(cpt) / cw;
                        const float cny = XMVectorGetY(cpt) / cw;
                        const float csx = origin.x
                            + (cnx * 0.5f + 0.5f) * region.x;
                        const float csy = origin.y
                            + (1.f - (cny * 0.5f + 0.5f)) * region.y;
                        dlay->AddCircleFilled(ImVec2(csx, csy), 3.f,
                            IM_COL32(255, 215, 0, 255));
                    }

                    if (paint_mode_active && !imgui_captured &&
                        ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    {
                        
                        TerrainPaint::ApplyBrush(
                            hx, hz, eff_size, eff_strength, eff_falloff,
                            ImGui::GetIO().KeyShift);
                        g_paint_composite_pending = true;
                    }
                    else if (!imgui_captured &&
                        ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    {
                        TerrainEdit::BrushTool bt =
                            TerrainEdit::BrushTool::None;
                        switch (eff_tool) {
                            case TT_RAISE:   bt = TerrainEdit::BrushTool::Raise; break;
                            case TT_LOWER:   bt = TerrainEdit::BrushTool::Lower; break;
                            case TT_SMOOTH:  bt = TerrainEdit::BrushTool::Smooth; break;
                            case TT_FLATTEN: bt = TerrainEdit::BrushTool::Flatten; break;
                            case TT_NOISE:   bt = TerrainEdit::BrushTool::Noise; break;
                            default: break;
                        }
                        
                        
                        
                        static float s_flatten_target = 0.0f;
                        if (ImGui::IsMouseClicked(
                                ImGuiMouseButton_Left)) {
                            s_flatten_target =
                                TerrainEdit::SampleHeightAtWorldXZ(hx,
                                                                   hz);
                        }
                        const float target_h =
                            (eff_tool == TT_FLATTEN) ? s_flatten_target
                                                     : 0.f;
                        TerrainEdit::ApplyBrush(bt, hx, hz,
                            eff_size, eff_strength, target_h,
                            eff_falloff);
                        upload_after_edit();
                    }
                }
            }
        }
    }

    
    
    if (g_paint_composite_pending && paint_mode_active) {
        static double s_last_composite = 0.0;
        const bool held = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (!held || ImGui::GetTime() - s_last_composite > 0.25) {
            s_last_composite = ImGui::GetTime();
            if (!held) g_paint_composite_pending = false;
            std::vector<uint8_t> rgba;
            int cw = 0, ch = 0;
            if (TerrainPaint::BuildComposite(rgba, cw, ch) &&
                !g_mp.meshes.empty()) {
                ID3D11ShaderResourceView* srv =
                    create_srv_from_rgba(device, cw, ch, rgba);
                if (srv) {
                    MPPerMesh& m = g_mp.meshes[0];
                    if (m.srv_diffuse &&
                        m.srv_diffuse != g_mp.default_srv) {
                        m.srv_diffuse->Release();
                    }
                    m.srv_diffuse = srv;
                    m.diffuse_visible = true;
                    m.diffuse_tex_name = "painted_layers";
                }
            }
        }
    }

    if (g_mp.has_model && g_mp.bone_count > 0 && !S.anim_clips.empty() &&
        !S.item_model_active && !S.entity_model_active) {
        static float s_anim_alpha = 0.30f;
        const float kIdleAlpha   = 0.30f;
        const float kHoverAlpha  = 1.00f;

        const float kAnimW   = 280.0f;
        const float kAnimPad = 6.0f;

        const float anim_h = std::max(160.0f, region.y - 2 * kAnimPad);
        const ImVec2 anim_pos(origin.x + region.x - kAnimW - kAnimPad,
                              origin.y + kAnimPad);
        const ImVec2 anim_size(kAnimW, anim_h);

        ImGui::SetNextWindowPos(anim_pos);
        ImGui::SetNextWindowSize(anim_size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(s_anim_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_anim_alpha);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin("##anims_overlay", nullptr, fl)) {

            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            ImVec2 mp = ImGui::GetIO().MousePos;
            bool in_rect = mp.x >= wp.x && mp.x < wp.x + ws.x &&
                           mp.y >= wp.y && mp.y < wp.y + ws.y;
            static bool s_was_hovering = false;
            bool hovering = in_rect;

            if (!hovering && s_was_hovering &&
                ImGui::GetIO().MouseDown[0]) {
                hovering = true;
            }
            s_was_hovering = hovering;
            float target = hovering ? kHoverAlpha : kIdleAlpha;
            s_anim_alpha += (target - s_anim_alpha) * 0.18f;
            if (std::fabs(s_anim_alpha - target) < 0.005f) s_anim_alpha = target;

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Animations");
            ImGui::Separator();

            {
                auto& pl = Anim::global_player();
                const auto* cur = pl.clip();
                if (cur) {
                    const float dur_s = Anim::clip_duration_seconds(*cur);
                    const bool playing =
                        (pl.state() == Anim::AnimPlayer::State::Playing);
                    const bool paused  =
                        (pl.state() == Anim::AnimPlayer::State::Paused);

                    const float btn_lg = 36.0f;
                    const float btn_sm = 26.0f;
                    const float gap    = 10.0f;
                    const float row_w  = ImGui::GetContentRegionAvail().x;
                    const float group_w = btn_sm + gap + btn_lg + gap + btn_sm;
                    const float group_x = (row_w - group_w) * 0.5f;
                    const float row_y   = ImGui::GetCursorPosY();
                    const float sm_y    = row_y + (btn_lg - btn_sm) * 0.5f;

                    ImGui::SetCursorPos(ImVec2(group_x, sm_y));
                    if (UI::icon_button("##anim_stop", ICON_FA_STOP,
                                        btn_sm, false)) {
                        pl.stop();
                    }

                    ImGui::SetCursorPos(ImVec2(group_x + btn_sm + gap, row_y));
                    const char* play_glyph = playing ? ICON_FA_PAUSE : ICON_FA_PLAY;

                    float play_dx = playing ? 0.0f : 0.17f;
                    if (UI::icon_button("##anim_playpause", play_glyph,
                                        btn_lg, true, false, play_dx)) {
                        if (playing) pl.pause();
                        else if (paused) pl.resume();
                        else pl.play(cur, pl.is_loop());
                    }

                    ImGui::SetCursorPos(ImVec2(
                        group_x + btn_sm + gap + btn_lg + gap, sm_y));
                    bool loop = pl.is_loop();
                    if (UI::icon_button("##anim_loop", ICON_FA_REPEAT,
                                        btn_sm, false, loop)) {
                        pl.set_loop(!loop);
                    }

                    ImGui::Dummy(ImVec2(0, btn_lg + 4.0f));

                    ImGui::Text("%.2fs / %.2fs", pl.time(), dur_s);

                    {
                        const float scrub_h = 18.0f;
                        ImGui::InvisibleButton("##anim_scrub",
                                               ImVec2(-1, scrub_h));
                        ImVec2 r0 = ImGui::GetItemRectMin();
                        ImVec2 r1 = ImGui::GetItemRectMax();
                        bool active = ImGui::IsItemActive();
                        ImDrawList* dl = ImGui::GetWindowDrawList();

                        dl->AddRectFilled(r0, r1,
                                          IM_COL32(20, 22, 28, 255), 4.0f);

                        const float w = r1.x - r0.x;
                        const float cy = (r0.y + r1.y) * 0.5f;
                        const float prog = (dur_s > 0.0f)
                            ? (pl.time() / dur_s) : 0.0f;
                        const float playhead_x = r0.x + w * prog;

                        dl->AddRectFilled(r0,
                                          ImVec2(playhead_x, r1.y),
                                          IM_COL32(120, 200, 255, 200),
                                          4.0f);

                        bool hovered_event = false;
                        std::string ev_tip;
                        const ImVec2 mp = ImGui::GetIO().MousePos;
                        for (const auto& ev : cur->events) {
                            if (dur_s <= 0.0f) break;
                            float t = ev.time / dur_s;
                            if (t < 0.0f || t > 1.0f) continue;
                            float ex = r0.x + w * t;
                            dl->AddLine(ImVec2(ex, r0.y + 2),
                                        ImVec2(ex, r1.y - 2),
                                        IM_COL32(255, 200, 90, 230),
                                        1.5f);

                            if (ImGui::IsItemHovered() &&
                                std::fabs(mp.x - ex) <= 4.0f &&
                                !hovered_event) {
                                hovered_event = true;
                                ev_tip = ev.name;
                                if (!ev.param.empty())
                                    ev_tip += " - " + ev.param;
                                char tbuf[16];
                                std::snprintf(tbuf, sizeof(tbuf),
                                              "  @ %.2fs", ev.time);
                                ev_tip += tbuf;
                            }
                        }

                        dl->AddLine(ImVec2(playhead_x, r0.y + 1),
                                    ImVec2(playhead_x, r1.y - 1),
                                    IM_COL32(240, 245, 250, 255),
                                    2.0f);

                        if (active && dur_s > 0.0f) {
                            float t = (mp.x - r0.x) / w;
                            if (t < 0.0f) t = 0.0f;
                            if (t > 1.0f) t = 1.0f;
                            pl.seek(t * dur_s);
                        }

                        if (hovered_event) {
                            ImGui::SetTooltip("%s", ev_tip.c_str());
                        }
                    }

                    ImGui::Separator();
                }
            }

            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##anims_overlay_filter", "Filter",
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
            } else if (S.dev_mode &&
                       g_mp.has_model && S.current_mdl_path_hash != 0) {
                ImGui::TextDisabled("No authored animation set");
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
            } else if (S.dev_mode) {
                ImGui::TextDisabled("Track-count filter unavailable");
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
                    std::transform(nlow.begin(), nlow.end(),
                                   nlow.begin(), ::tolower);
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
            }

            if (S.dev_mode &&
                S.anim_selected_clip >= 0 &&
                S.anim_selected_clip < (int)S.anim_clips.size())
            {
                const auto& c = S.anim_clips[(size_t)S.anim_selected_clip];
                ImGui::Separator();
                if (Anim::global_data_file().is_open()) {
                    auto h = Anim::global_data_file().parse_clip_header(c);
                    if (h.ok) {
                        ImGui::TextDisabled(
                            "tracks=%u frames=%u fmt=%u",
                            h.bone_count, h.frame_count, h.bone_idx_bits);
                        if (ImGui::TreeNodeEx("##anim_bone_view",
                                              ImGuiTreeNodeFlags_None,
                                              "Per-bone bodies")) {
                            auto sp = Anim::global_data_file().clip_bytes(c);
                            const size_t total = sp.size;
                            ImGui::BeginChild("##anim_bone_list",
                                              ImVec2(0, 120), false,
                                              ImGuiWindowFlags_HorizontalScrollbar);
                            for (uint32_t bi = 0; bi < h.bone_count; ++bi) {
                                uint32_t bo_bits = h.bone_offsets[bi];
                                uint32_t be_bits = (bi + 1 < h.bone_count)
                                    ? h.bone_offsets[bi + 1]
                                    : (uint32_t)((total > h.packed_body_offset)
                                        ? (total - h.packed_body_offset) * 8
                                        : 0);
                                if (be_bits < bo_bits) continue;
                                uint32_t bo = (uint32_t)h.packed_body_offset
                                            + bo_bits / 8;
                                uint32_t be = (uint32_t)h.packed_body_offset
                                            + (be_bits + 7) / 8;
                                if (be < bo || be > total) continue;
                                uint32_t blen = be - bo;
                                char hexbuf[3 * 4 + 1] = "??";
                                if (bo + 4 <= total) {
                                    std::snprintf(hexbuf, sizeof(hexbuf),
                                                  "%02X %02X %02X %02X",
                                                  sp.data[bo + 0],
                                                  sp.data[bo + 1],
                                                  sp.data[bo + 2],
                                                  sp.data[bo + 3]);
                                }
                                ImGui::TextDisabled(
                                    "track %3u  bits=%6u  bytes=%5u  first4: %s",
                                    bi, be_bits - bo_bits, blen, hexbuf);
                            }
                            ImGui::EndChild();
                            ImGui::TreePop();
                        }
                    } else {
                        ImGui::TextDisabled(
                            "(unrecognised clip header: m=0x%08X v=%u)",
                            h.magic, h.version);
                    }
                } else {
                    ImGui::TextDisabled("(data file not loaded)");
                }
                ImGui::Separator();
            }

            ImGui::BeginChild("##anims_overlay_list", ImVec2(0, 0), false);
            ImGuiListClipper clipper;
            clipper.Begin((int)vis.size());
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart;
                     row < clipper.DisplayEnd; ++row) {
                    const int clip_idx = vis[(size_t)row];
                    const auto& c =
                        S.anim_clips[(size_t)clip_idx];
                    ImGui::PushID(row);
                    bool selected =
                        (S.anim_selected_clip == clip_idx);
                    char label[80];
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
                        }
                        ImGui::EndTooltip();
                    }
                    ImGui::PopID();
                }
            }
            clipper.End();
            ImGui::EndChild();
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    if (S.show_item_details && S.selected_item >= 0 &&
        S.selected_item < (int)g_item_details.size() &&
        !LevelEdit::Enabled()) {
        const auto& it = g_item_details[(size_t)S.selected_item];

        static ID3D11ShaderResourceView* s_icon_srv = nullptr;
        static uint32_t s_icon_for = 0xFFFFFFFFu;
        static int s_icon_w = 0, s_icon_h = 0;
        if (g_item_icon_dirty.exchange(false) ||
            s_icon_for != it.record_hash) {
            s_icon_for = it.record_hash;
            if (s_icon_srv) { s_icon_srv->Release(); s_icon_srv = nullptr; }
            s_icon_w = s_icon_h = 0;
            if (!it.icon_tex.empty()) {
                std::vector<unsigned char> tex_buf;
                if (build_any_tex_buffer_for_name(it.icon_tex, tex_buf,
                                                  std::string())) {
                    std::vector<uint8_t> rgba;
                    int w = 0, h = 0;
                    bool has_a = false;
                    if (decode_tex_to_rgba(tex_buf, rgba, w, h, &has_a,
                                           -1) &&
                        w > 0 && h > 0) {
                        s_icon_srv = create_srv_from_rgba(device, w, h,
                                                          rgba);
                        s_icon_w = w;
                        s_icon_h = h;
                    }
                }
            }
        }

        static float s_item_alpha = 0.30f;
        const float kIdleAlpha  = 0.30f;
        const float kHoverAlpha = 1.00f;
        const float kItemW  = 300.0f;
        const float kItemPad = 6.0f;
        ImGui::SetNextWindowPos(
            ImVec2(origin.x + region.x - kItemW - kItemPad,
                   origin.y + kItemPad));


        ImGui::SetNextWindowSizeConstraints(
            ImVec2(kItemW, 0.0f),
            ImVec2(kItemW, std::max(200.0f, region.y - 2 * kItemPad)));
        ImGui::SetNextWindowBgAlpha(s_item_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_item_alpha);
        ImGuiWindowFlags ifl = ImGuiWindowFlags_NoTitleBar |
                               ImGuiWindowFlags_NoResize |
                               ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoCollapse |
                               ImGuiWindowFlags_NoSavedSettings |
                               ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##item_details_overlay", nullptr, ifl)) {
            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            ImVec2 mp = ImGui::GetIO().MousePos;
            bool hovering = mp.x >= wp.x && mp.x < wp.x + ws.x &&
                            mp.y >= wp.y && mp.y < wp.y + ws.y;
            static bool s_was_hovering = false;
            if (!hovering && s_was_hovering &&
                ImGui::GetIO().MouseDown[0]) {
                hovering = true;
            }
            s_was_hovering = hovering;
            const float target = hovering ? kHoverAlpha : kIdleAlpha;
            s_item_alpha += (target - s_item_alpha) * 0.18f;
            if (std::fabs(s_item_alpha - target) < 0.005f) {
                s_item_alpha = target;
            }

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                               "Item Details");
            ImGui::Separator();

            std::string disp_name;
            if (it.name_tag) TextBank::Lookup(it.name_tag, disp_name);
            if (disp_name.empty()) disp_name = it.label;
            ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "%s",
                               disp_name.c_str());
            if (s_icon_srv) {
                float iw = float(s_icon_w), ih = float(s_icon_h);
                const float maxdim = 80.0f;
                if (iw > maxdim || ih > maxdim) {
                    const float s = maxdim / std::max(iw, ih);
                    iw *= s; ih *= s;
                }
                ImGui::Image((ImTextureID)s_icon_srv, ImVec2(iw, ih));
            }
            if (it.money >= 0) {
                ImGui::Text("Value: %d gold", it.money);
            }

            std::string desc;
            if (it.desc_tag) TextBank::Lookup(it.desc_tag, desc);
            if (!desc.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
                                   "Description");
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(desc.c_str());
                ImGui::PopTextWrapPos();
            }

            if (!it.stats.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
                                   "Stats");
                if (ImGui::BeginTable("##item_stats", 2,
                                      ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Field");
                    ImGui::TableSetupColumn(
                        "Value", ImGuiTableColumnFlags_WidthFixed,
                        84.0f);
                    for (const auto& kv : it.stats) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(kv.first.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(kv.second.c_str());
                    }
                    ImGui::EndTable();
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    if (S.show_entity_details && S.selected_entity >= 0 &&
        S.selected_entity < static_cast<int>(g_global_entity_catalog.size()) &&
        !LevelEdit::Enabled() &&
        ContentTabs::ActiveKind() == ContentTabs::Kind::Entity) {
        const auto& entity = g_global_entity_catalog[
            static_cast<std::size_t>(S.selected_entity)];
        static float s_entity_alpha = 0.30f;
        constexpr float kIdleAlpha = 0.30f;
        constexpr float kHoverAlpha = 1.00f;
        constexpr float kEntityW = 350.0f;
        constexpr float kEntityPad = 6.0f;
        const float entity_h = (std::min)(
            620.0f, (std::max)(180.0f, region.y - 2.0f * kEntityPad));
        ImGui::SetNextWindowPos(
            ImVec2(origin.x + region.x - kEntityW - kEntityPad,
                   origin.y + kEntityPad));
        ImGui::SetNextWindowSize(ImVec2(kEntityW, entity_h),
                                 ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(s_entity_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_entity_alpha);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar;
        if (ImGui::Begin("##entity_details_overlay", nullptr, flags)) {
            const ImVec2 window_pos = ImGui::GetWindowPos();
            const ImVec2 window_size = ImGui::GetWindowSize();
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            bool hovering = mouse.x >= window_pos.x &&
                            mouse.x < window_pos.x + window_size.x &&
                            mouse.y >= window_pos.y &&
                            mouse.y < window_pos.y + window_size.y;
            static bool s_was_hovering = false;
            if (!hovering && s_was_hovering &&
                ImGui::GetIO().MouseDown[0]) {
                hovering = true;
            }
            s_was_hovering = hovering;
            const float target = hovering ? kHoverAlpha : kIdleAlpha;
            s_entity_alpha += (target - s_entity_alpha) * 0.18f;
            if (std::fabs(s_entity_alpha - target) < 0.005f) {
                s_entity_alpha = target;
            }

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                               "Entity Details");
            ImGui::Separator();
            ImGui::BeginChild("##entity_details_scroll", ImVec2(0.0f, 0.0f),
                              false);
            ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "%s",
                               (entity.display_name.empty()
                                    ? entity.name : entity.display_name)
                                   .c_str());
            if (S.dev_mode) {
                if (!entity.display_name.empty() &&
                    entity.display_name != entity.name) {
                    ImGui::TextDisabled("Internal: %s", entity.name.c_str());
                }
                ImGui::TextDisabled("Entity 0x%08X", entity.entity_hash);
            }

            static std::uint32_t cached_animation_entity = 0;
            static std::uint64_t cached_animation_binding_revision = 0;
            static std::uint64_t cached_animation_catalog_revision = 0;
            static std::size_t cached_animation_clip_count = 0;
            static std::string entity_animation_filter;
            static std::vector<std::pair<std::size_t, std::string>> animations;
            const std::uint64_t binding_revision =
                Anim::model_animation_binding_revision();
            if (cached_animation_entity != entity.entity_hash ||
                cached_animation_binding_revision != binding_revision ||
                cached_animation_catalog_revision !=
                    g_global_entity_catalog_revision ||
                cached_animation_clip_count != S.anim_clips.size()) {
                animations.clear();
                entity_animation_filter.clear();
                std::unordered_set<std::size_t> seen_animations;
                const std::unordered_set<std::uint32_t> model_hashes(
                    entity.model_hashes.begin(), entity.model_hashes.end());
                for (const Anim::ModelAnimationBinding& binding :
                     Anim::model_animation_bindings()) {
                    if (model_hashes.find(binding.model_path_hash) ==
                        model_hashes.end()) {
                        continue;
                    }
                    if (binding.clip_index >= S.anim_clips.size() ||
                        !seen_animations.insert(binding.clip_index).second) {
                        continue;
                    }
                    std::string name = binding.animation_name.empty()
                        ? binding.source_name : binding.animation_name;
                    if (name.empty()) {
                        name = S.anim_clips[binding.clip_index].name;
                    }
                    animations.emplace_back(binding.clip_index,
                                             std::move(name));
                }
                cached_animation_entity = entity.entity_hash;
                cached_animation_binding_revision = binding_revision;
                cached_animation_catalog_revision =
                    g_global_entity_catalog_revision;
                cached_animation_clip_count = S.anim_clips.size();
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f),
                               "Animations (%zu)", animations.size());
            if (animations.empty()) {
                ImGui::TextDisabled("None indexed");
            } else {
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##entity_animation_filter",
                                         "Filter animations...",
                                         &entity_animation_filter);
                std::string filter_lower = entity_animation_filter;
                std::transform(filter_lower.begin(), filter_lower.end(),
                               filter_lower.begin(), ::tolower);
                std::vector<std::size_t> visible_animations;
                visible_animations.reserve(animations.size());
                for (std::size_t i = 0; i < animations.size(); ++i) {
                    if (filter_lower.empty()) {
                        visible_animations.push_back(i);
                        continue;
                    }
                    std::string name_lower = animations[i].second;
                    std::transform(name_lower.begin(), name_lower.end(),
                                   name_lower.begin(), ::tolower);
                    if (name_lower.find(filter_lower) != std::string::npos) {
                        visible_animations.push_back(i);
                    }
                }
                auto& player = Anim::global_player();
                const Anim::AnimClip* current = player.clip();
                if (current) {
                    const bool playing = player.state() ==
                        Anim::AnimPlayer::State::Playing;
                    const bool paused = player.state() ==
                        Anim::AnimPlayer::State::Paused;
                    if (UI::icon_button("##entity_anim_stop", ICON_FA_STOP,
                                        26.0f, false)) {
                        player.stop();
                    }
                    ImGui::SameLine();
                    const char* glyph = playing ? ICON_FA_PAUSE : ICON_FA_PLAY;
                    if (UI::icon_button("##entity_anim_play", glyph,
                                        30.0f, true)) {
                        if (playing) player.pause();
                        else if (paused) player.resume();
                        else player.play(current, player.is_loop());
                    }
                    ImGui::SameLine();
                    if (UI::icon_button("##entity_anim_loop", ICON_FA_REPEAT,
                                        26.0f, false,
                                        player.is_loop())) {
                        player.set_loop(!player.is_loop());
                    }
                    const float duration =
                        Anim::clip_duration_seconds(*current);
                    float time = player.time();
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::SliderFloat("##entity_anim_time", &time,
                                           0.0f,
                                           (std::max)(duration, 0.001f),
                                           "%.2fs")) {
                        player.seek(time);
                    }
                } else {
                    ImGui::TextDisabled("Select an animation to play it");
                }
                const float animation_list_height = (std::min)(
                    240.0f,
                    (std::max)(100.0f, ImGui::GetContentRegionAvail().y));
                ImGui::BeginChild("##entity_animation_list",
                                  ImVec2(0.0f, animation_list_height),
                                  false);
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(visible_animations.size()));
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart;
                         row < clipper.DisplayEnd; ++row) {
                        const auto& animation =
                            animations[visible_animations[
                                static_cast<std::size_t>(row)]];
                        const bool selected =
                            S.anim_selected_clip ==
                            static_cast<int>(animation.first);
                        ImGui::PushID(row);
                        const float duration = Anim::clip_duration_seconds(
                            S.anim_clips[animation.first]);
                        char animation_label[192];
                        std::snprintf(animation_label,
                                      sizeof(animation_label),
                                      "%s  (%.2fs)",
                                      animation.second.c_str(), duration);
                        if (ImGui::Selectable(animation_label, selected)) {
                            S.anim_selected_clip =
                                static_cast<int>(animation.first);
                            player.play(&S.anim_clips[animation.first],
                                        player.is_loop());
                        }
                        ImGui::PopID();
                    }
                }
                clipper.End();
                ImGui::EndChild();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f),
                               "Model parts");
            if (entity.model_hashes.empty()) {
                ImGui::TextDisabled("None");
            } else {
                for (std::uint32_t hash : entity.model_hashes) {
                    const FlatAssetEntry* match =
                        FindGlobalModelAssetByPathHash(hash);
                    if (match) {
                        ImGui::Bullet();
                        ImGui::SameLine();
                        ImGui::TextWrapped("%s", match->full_path.c_str());
                    } else {
                        ImGui::BulletText("Unresolved model 0x%08X", hash);
                    }
                }
            }

            const auto gameplay =
                g_global_entity_gameplay.find(entity.entity_hash);
            if (gameplay != g_global_entity_gameplay.end()) {
                draw_entity_gameplay_details(gameplay->second, false);
            }
            ImGui::EndChild();
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    if (::g_tex_popout_open && ::g_tex_popout_srv) {
        int tw = 0, th = 0;
        ID3D11Resource* res = nullptr;
        ::g_tex_popout_srv->GetResource(&res);
        if (res) {

            ID3D11Texture2D* t2d = (ID3D11Texture2D*)res;
            D3D11_TEXTURE2D_DESC desc{};
            t2d->GetDesc(&desc);
            tw = (int)desc.Width;
            th = (int)desc.Height;
            res->Release();
        }
        if (tw > 0 && th > 0) {
            std::string title = "Texture: "
                + std::filesystem::path(::g_tex_popout_name).filename().string()
                + "##tex_popout";

            ImGuiWindowFlags fl = ImGuiWindowFlags_NoCollapse
                                | ImGuiWindowFlags_NoResize
                                | ImGuiWindowFlags_AlwaysAutoResize;
            if (ImGui::Begin(title.c_str(), &::g_tex_popout_open, fl)) {

                ImGui::Checkbox("Show UVs", &::g_tex_popout_show_uvs);

                ImGui::Image((ImTextureID)::g_tex_popout_srv,
                             ImVec2((float)tw, (float)th));

                {
                    ImVec2 img_min = ImGui::GetItemRectMin();
                    ImGui::SetCursorScreenPos(img_min);
                    ImGui::InvisibleButton("##popout_hit",
                                           ImVec2((float)tw, (float)th));
                    if (ImGui::BeginPopupContextItem()) {
                        const std::string& preferred_bnk =
                            (S.selected_nested_index != -1 &&
                             !S.selected_nested_temp_path.empty())
                                ? S.selected_nested_temp_path
                                : S.selected_bnk;
                        tex_export_menu_named(::g_tex_popout_name,
                                              ::g_tex_popout_name,
                                              preferred_bnk, 0);
                        ImGui::EndPopup();
                    }
                }

                if (::g_tex_popout_show_uvs &&
                    ::g_tex_popout_mesh_idx >= 0 &&
                    (size_t)::g_tex_popout_mesh_idx < g_mp.meshes.size())
                {
                    uint32_t src = g_mp.meshes[(size_t)::g_tex_popout_mesh_idx].source_mesh_idx;
                    if (src < S.mdl_meshes.size()) {
                        const auto& geom = S.mdl_meshes[src];
                        if (!geom.uvs.empty() && !geom.indices.empty()) {
                            ImVec2 img_min = ImGui::GetItemRectMin();
                            ImVec2 img_max = ImGui::GetItemRectMax();
                            float w_px = img_max.x - img_min.x;
                            float h_px = img_max.y - img_min.y;
                            ImDrawList* dl = ImGui::GetWindowDrawList();

                            const ImU32 col = IM_COL32(255, 255, 255, 200);
                            const float thickness = 1.0f;

                            for (size_t i = 0; i + 2 < geom.indices.size(); i += 3) {
                                uint32_t a = geom.indices[i];
                                uint32_t b = geom.indices[i + 1];
                                uint32_t c = geom.indices[i + 2];
                                if ((size_t)a * 2 + 1 >= geom.uvs.size()) continue;
                                if ((size_t)b * 2 + 1 >= geom.uvs.size()) continue;
                                if ((size_t)c * 2 + 1 >= geom.uvs.size()) continue;
                                ImVec2 pa(img_min.x + geom.uvs[a * 2 + 0] * w_px,
                                          img_min.y + geom.uvs[a * 2 + 1] * h_px);
                                ImVec2 pb(img_min.x + geom.uvs[b * 2 + 0] * w_px,
                                          img_min.y + geom.uvs[b * 2 + 1] * h_px);
                                ImVec2 pc(img_min.x + geom.uvs[c * 2 + 0] * w_px,
                                          img_min.y + geom.uvs[c * 2 + 1] * h_px);
                                dl->AddLine(pa, pb, col, thickness);
                                dl->AddLine(pb, pc, col, thickness);
                                dl->AddLine(pc, pa, col, thickness);
                            }
                        }
                    }
                }
            }
            ImGui::End();
        }

        if (!::g_tex_popout_open) {
            ::g_tex_popout_srv = nullptr;
            ::g_tex_popout_name.clear();
        }
    }
}

void draw_heightmap_popout() {
    if (::g_heightmap_popout_open && ::g_heightmap_popout_srv) {
        const int hw = ::g_heightmap_popout_w;
        const int hh = ::g_heightmap_popout_h;

        if (hw > 0 && hh > 0) {
            ImGuiViewport* vp = ImGui::GetMainViewport();
            const float vw = vp->WorkSize.x;
            const float vh = vp->WorkSize.y;
            const float cap_w = vw * 0.8f;
            const float cap_h = vh * 0.8f;
            float scale = 1.0f;
            if ((float)hw > cap_w || (float)hh > cap_h) {
                scale = std::min(cap_w / (float)hw, cap_h / (float)hh);
            }
            const float dw = std::max(64.0f, (float)hw * scale);
            const float dh = std::max(64.0f, (float)hh * scale);

            std::string title = ::g_heightmap_popout_kind + ": " +
                              ::g_heightmap_popout_name
                              + "##heightmap_popout";
            ImGuiWindowFlags fl = ImGuiWindowFlags_NoCollapse
                                | ImGuiWindowFlags_AlwaysAutoResize;
            if (ImGui::Begin(title.c_str(),
                             &::g_heightmap_popout_open, fl)) {
                ImGui::Image((ImTextureID)::g_heightmap_popout_srv,
                             ImVec2(dw, dh));

                ImVec2 img_min = ImGui::GetItemRectMin();
                ImGui::SetCursorScreenPos(img_min);
                ImGui::InvisibleButton("##hmap_popout_hit",
                                       ImVec2(dw, dh));
                if (ImGui::BeginPopupContextItem()) {
                    tex_export_menu_rgba(::g_heightmap_popout_name,
                                         ::g_heightmap_popout_rgba,
                                         hw, hh);
                    ImGui::EndPopup();
                }
            }
            ImGui::End();
        }

        if (!::g_heightmap_popout_open) {
            if (::g_heightmap_popout_srv) {
                ::g_heightmap_popout_srv->Release();
                ::g_heightmap_popout_srv = nullptr;
            }
            ::g_heightmap_popout_name.clear();
            ::g_heightmap_popout_kind = "Heightmap";
            ::g_heightmap_popout_rgba.clear();
            ::g_heightmap_popout_w = 0;
            ::g_heightmap_popout_h = 0;
        }
    }
}
#endif

}

#ifdef _WIN32
bool select_level_entity_model(unsigned int entity_hash) {
    if (!entity_hash) return false;
    for (size_t mesh_index = 0; mesh_index < g_mp.meshes.size();
         ++mesh_index) {
        const MPPerMesh& mesh = g_mp.meshes[mesh_index];
        if (!mesh.is_entity_model) continue;
        for (const auto& range : mesh.pick_ranges) {
            if (range.gdb_entity_hash != entity_hash) continue;
            ::g_selected_level_mesh_idx = static_cast<int>(mesh_index);
            ::g_selected_level_pick_id = range.selection_id;
            ::g_selected_level_hash = range.inst_hash;
            g_sel_spawn_marker = -1;
            g_sel_pending_sp = -1;
            g_sel_pending_gen = -1;
            g_marker_clear_selection = false;
            return true;
        }
    }
    return false;
}

bool select_level_marker(std::size_t marker_index) {
    if (marker_index >= g_level_spawn_markers.size()) return false;
    if ((g_level_spawn_markers[marker_index].kind == 2 ||
         g_level_spawn_markers[marker_index].kind == 3 ||
         g_level_spawn_markers[marker_index].kind == 6) &&
        !g_level_spawn_markers[marker_index].model_hashes.empty()) {
        const unsigned int entity_hash =
            g_level_spawn_markers[marker_index].entity_hash;
        if (entity_hash && select_level_entity_model(entity_hash)) {
            return true;
        }
        const uint32_t selection_id =
            0x70000000u | uint32_t(marker_index);
        for (size_t mesh_index = 0; mesh_index < g_mp.meshes.size();
             ++mesh_index) {
            const MPPerMesh& mesh = g_mp.meshes[mesh_index];
            if (!mesh.is_entity_model) continue;
            for (const auto& range : mesh.pick_ranges) {
                if (range.selection_id != selection_id) continue;
                ::g_selected_level_mesh_idx = int(mesh_index);
                ::g_selected_level_pick_id = selection_id;
                ::g_selected_level_hash = range.inst_hash;
                g_sel_spawn_marker = -1;
                g_sel_pending_sp = -1;
                g_sel_pending_gen = -1;
                g_marker_clear_selection = false;
                return true;
            }
        }
    }
    g_sel_spawn_marker = static_cast<int>(marker_index);
    g_sel_pending_sp = -1;
    g_sel_pending_gen = -1;
    g_marker_clear_selection = true;
    return true;
}

void request_level_add_menu_at(const float engine_pos[3]) {
    if (!engine_pos) return;
    for (int i = 0; i < 3; ++i) {
        g_add_menu_requested_pos[i] = engine_pos[i];
    }
    g_add_menu_requested = true;
}

void draw_render_panel(ID3D11Device* device) {

    if (S.show_gdb_render) {
        draw_gdb_in_panel();
    } else if (S.content_tabs_visible && ContentTabs::HasTabs()) {
        ContentTabs::DrawTabBar();
        const ContentTabs::Kind kind = ContentTabs::ActiveKind();
        if (kind == ContentTabs::Kind::Lua ||
            kind == ContentTabs::Kind::Quest ||
            kind == ContentTabs::Kind::CustomQuest) {
            draw_lua_in_panel();
        } else if (kind == ContentTabs::Kind::Level) {
            const FlatAssetEntry* level_entry =
                ContentTabs::ActiveLevelEntry();
            const bool landscape_panel =
                level_entry && LandscapePanel::AppliesTo(*level_entry);
            if (landscape_panel) {
                LandscapePanel::DrawSidePanel(*level_entry, device);
                ImGui::SameLine();
                ImGui::BeginChild("##level_view_area", ImVec2(0, 0), false,
                                  ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse);
            }
            if (g_mp.has_model && S.terrain_mode && g_mp.no_tilt) {
                draw_model_in_panel(device);
            } else {
                draw_placeholder();
            }
            if (landscape_panel) ImGui::EndChild();
        } else if (kind == ContentTabs::Kind::Model) {
            if (ContentTabs::ActiveHasModel() && g_mp.has_model &&
                !S.terrain_mode && !g_mp.no_tilt) {
                draw_model_in_panel(device);
            } else {
                draw_placeholder();
            }
        } else if (kind == ContentTabs::Kind::Item) {
            if (ContentTabs::ActiveHasModel() && g_mp.has_model &&
                !S.terrain_mode && !g_mp.no_tilt) {
                draw_model_in_panel(device);
            } else {
                draw_item_tab_content();
            }
        } else if (kind == ContentTabs::Kind::Entity) {
            if (ContentTabs::ActiveHasModel() && g_mp.has_model &&
                !S.terrain_mode && !g_mp.no_tilt) {
                draw_model_in_panel(device);
            } else {
                draw_entity_tab_content();
            }
        } else {
            draw_placeholder();
        }
    } else if (g_mp.has_model) {
        draw_model_in_panel(device);
    } else if (S.texture_window_srv) {
        draw_texture_in_panel(device);
    } else if (S.show_lua_render) {
        draw_lua_in_panel();
    } else {
        draw_placeholder();
    }

    draw_heightmap_popout();
}
#else
namespace {
void draw_texture_in_panel_gl() {
    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(origin,
                      ImVec2(origin.x + region.x, origin.y + region.y),
                      IM_COL32(20, 22, 28, 255));

    if (!S.texture_window_gl || S.texture_window_width <= 0 || S.texture_window_height <= 0) {
        const char* msg = S.texture_window_name.empty()
            ? "Texture decode failed"
            : S.texture_window_name.c_str();
        ImVec2 sz = ImGui::CalcTextSize(msg);
        ImVec2 pos(origin.x + (region.x - sz.x) * 0.5f,
                   origin.y + (region.y - sz.y) * 0.5f);
        dl->AddText(pos, IM_COL32(255, 90, 90, 230), msg);
        ImGui::Dummy(region);
        return;
    }

    float tw = (float)S.texture_window_width;
    float th = (float)S.texture_window_height;
    float scale = std::min(region.x / tw, region.y / th);
    if (scale > 4.0f) scale = 4.0f;
    float dw = tw * scale;
    float dh = th * scale;
    float x0 = origin.x + (region.x - dw) * 0.5f;
    float y0 = origin.y + (region.y - dh) * 0.5f;

    dl->AddImage((ImTextureID)(intptr_t)S.texture_window_gl,
                 ImVec2(x0, y0),
                 ImVec2(x0 + dw, y0 + dh));

    ImGui::SetCursorScreenPos(ImVec2(x0, y0));
    ImGui::InvisibleButton("##tex_preview_hit", ImVec2(dw, dh));
    if (S.tex_info_ok && !S.texture_blob.empty() &&
        ImGui::BeginPopupContextItem()) {
        tex_export_menu_blob(S.texture_window_name,
                             S.texture_blob,
                             S.texture_mip_index);
        ImGui::EndPopup();
    }
}

void apply_orbit_to_flycam_gl() {
    float cy = std::cos(S.cam_yaw);
    float sy = std::sin(S.cam_yaw);
    float cp = std::cos(S.cam_pitch);
    float sp = std::sin(S.cam_pitch);
    g_flycam.pos[0] = g_mp.center[0] + sy * cp * S.cam_dist * g_mp.radius;
    const float target_y = g_mp.center[1] + S.cam_target_offset_y;
    g_flycam.pos[1] = target_y + sp * S.cam_dist * g_mp.radius;
    g_flycam.pos[2] = g_mp.center[2] + cy * cp * S.cam_dist * g_mp.radius;
    float dx = g_mp.center[0] - g_flycam.pos[0];
    float dy = target_y - g_flycam.pos[1];
    float dz = g_mp.center[2] - g_flycam.pos[2];
    float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len > 0.0001f) {
        g_flycam.yaw = std::atan2(dx, dz);
        g_flycam.pitch = std::asin(dy / len);
    }
}

void draw_materials_overlay_gl(const ImVec2& origin,
                               const ImVec2& region,
                               float next_overlay_y) {
    if (g_mp.has_model && g_mp.lod_count > 1 &&
        !details_panel_docked()) {
        static float s_lod_alpha = 0.30f;
        const float kIdleAlpha = 0.30f;
        const float kHoverAlpha = 1.00f;

        ImGui::SetNextWindowPos(ImVec2(origin.x + 6, next_overlay_y));
        ImGui::SetNextWindowSize(ImVec2(190, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(s_lod_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_lod_alpha);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##lod_overlay", nullptr, fl)) {
            bool hovering = ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                ImGuiHoveredFlags_ChildWindows);
            float target = hovering ? kHoverAlpha : kIdleAlpha;
            s_lod_alpha += (target - s_lod_alpha) * 0.18f;
            if (std::fabs(s_lod_alpha - target) < 0.005f) s_lod_alpha = target;

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "LOD");
            const int lod_count = (int)g_mp.lod_count;
            int current = g_mp.selected_lod;
            if (current < -1 || current >= lod_count) current = 0;

            if (ImGui::RadioButton("All", current == -1)) {
                g_mp.selected_lod = -1;
            }
            for (int i = 0; i < lod_count; ++i) {
                ImGui::SameLine();
                char lbl[16];
                std::snprintf(lbl, sizeof(lbl), "%d", i);
                if (ImGui::RadioButton(lbl, current == i)) {
                    g_mp.selected_lod = i;
                }
            }

            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            next_overlay_y = wp.y + ws.y + 6.0f;
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    if (!g_mp.has_model || g_mp.meshes.empty()) {
        ::g_highlight_mesh_idx = -1;
        ::g_isolate_mesh_idx = -1;
        ::g_tex_popout_open = false;
        ::g_tex_popout_gl = 0;
        ::g_tex_popout_name.clear();
        ::g_tex_popout_mesh_idx = -1;
        return;
    }

    static float s_mat_alpha = 0.30f;
    const float kIdleAlpha = 0.30f;
    const float kHoverAlpha = 1.00f;
    const float kMatW = 296.0f;
    float max_h = std::max(160.0f,
                           region.y - (next_overlay_y - origin.y) - 20.0f);

    ImGui::SetNextWindowPos(ImVec2(origin.x + 6, next_overlay_y));
    ImGui::SetNextWindowSizeConstraints(ImVec2(kMatW, 0.0f),
                                        ImVec2(kMatW, max_h));
    ImGui::SetNextWindowBgAlpha(s_mat_alpha * 0.78f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_mat_alpha);

    ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##materials_overlay", nullptr, fl)) {
        bool hovering = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_AllowWhenBlockedByPopup |
            ImGuiHoveredFlags_ChildWindows);
        float target = hovering ? kHoverAlpha : kIdleAlpha;
        s_mat_alpha += (target - s_mat_alpha) * 0.18f;
        if (std::fabs(s_mat_alpha - target) < 0.005f) s_mat_alpha = target;

        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Materials");
        ImGui::Separator();

        const ImVec2 thumb_size(48, 48);
        if (!g_mp.no_tilt) for (size_t mi = 0; mi < g_mp.meshes.size(); ++mi) {
        auto& mesh = g_mp.meshes[mi];

        if (g_mp.selected_lod >= 0 &&
            mesh.lod_index != (uint32_t)g_mp.selected_lod) {
            continue;
        }

        ImGui::PushID((int)mi);
        ImGui::TextUnformatted(mesh.name.c_str());
            bool h = (::g_highlight_mesh_idx == (int)mi);
            bool iso = (::g_isolate_mesh_idx == (int)mi);
            if (ImGui::Checkbox("Highlight", &h)) {
                if (h) {
                    ::g_highlight_mesh_idx = (int)mi;
                    ::g_isolate_mesh_idx = -1;
                } else if (::g_highlight_mesh_idx == (int)mi) {
                    ::g_highlight_mesh_idx = -1;
                }
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Isolate", &iso)) {
                if (iso) {
                    ::g_isolate_mesh_idx = (int)mi;
                    ::g_highlight_mesh_idx = -1;
                } else if (::g_isolate_mesh_idx == (int)mi) {
                    ::g_isolate_mesh_idx = -1;
                }
            }

            struct ThumbSpec {
                const char* slot_id;
                unsigned int tex;
                const std::string* name;
                bool* visible;
            };
            ThumbSpec thumbs[5] = {
                {"diffuse",  mesh.tex_diffuse,  &mesh.diffuse_tex_name,  &mesh.diffuse_visible},
                {"normal",   mesh.tex_normal,   &mesh.normal_tex_name,   &mesh.normal_visible},
                {"specular", mesh.tex_specular, &mesh.specular_tex_name, &mesh.specular_visible},
                {"metallic", mesh.tex_metallic, &mesh.metallic_tex_name, &mesh.metallic_visible},
                {"extra",    mesh.tex_extra,    &mesh.extra_tex_name,    &mesh.extra_visible},
            };

            bool any_thumb = false;
            for (int ti = 0; ti < 5; ++ti) {
                const ThumbSpec& t = thumbs[ti];
                if (!t.tex || t.tex == g_mp.default_tex) continue;
                if (t.name->empty()) continue;
                if (any_thumb) ImGui::SameLine();
                any_thumb = true;
                ImGui::PushID(t.slot_id);
                ImGui::BeginGroup();
                ImVec4 tint = (*t.visible) ? ImVec4(1, 1, 1, 1)
                                           : ImVec4(0.45f, 0.45f, 0.45f, 1);
                if (ImGui::ImageButton("##t",
                                       (ImTextureID)(intptr_t)t.tex,
                                       thumb_size,
                                       ImVec2(0, 0), ImVec2(1, 1),
                                       ImVec4(0, 0, 0, 0), tint)) {
                    ::g_tex_popout_gl = t.tex;
                    ::g_tex_popout_name = *t.name;
                    ::g_tex_popout_open = true;
                    ::g_tex_popout_mesh_idx = (int)mi;
                }
                if (ImGui::BeginPopupContextItem()) {
                    const std::string& preferred_bnk =
                        (S.selected_nested_index != -1 &&
                         !S.selected_nested_temp_path.empty())
                            ? S.selected_nested_temp_path
                            : S.selected_bnk;
                    tex_export_menu_named(*t.name, *t.name,
                                          preferred_bnk, 0);
                    ImGui::EndPopup();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s\n[%s]", t.name->c_str(), t.slot_id);
                }
                ImGui::Checkbox("##vis", t.visible);
                ImGui::EndGroup();
                ImGui::PopID();
            }
            if (!any_thumb) ImGui::TextDisabled("(no textures)");
            ImGui::Separator();
        ImGui::PopID();
    }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void draw_gdb_placements_overlay_gl(const ImVec2& origin, const ImVec2& region) {
    if (g_level_gdb_placements.empty() || !S.show_gdb_placements ||
        !g_mp.no_tilt) return;

    const float fx = std::sin(g_flycam.yaw) * std::cos(g_flycam.pitch);
    const float fy = std::sin(g_flycam.pitch);
    const float fz = std::cos(g_flycam.yaw) * std::cos(g_flycam.pitch);
    const float rx = fz, ry = 0.0f, rz = -fx;
    const float ux = fy * rz;
    const float uy = fz * rx - fx * rz;
    const float uz = fx * ry - fy * rx;
    const float tan_half_fov = std::tan(3.1415926535f / 6.0f);
    const float aspect = region.x / std::max(1.0f, region.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const int gw = g_pending_terrain_ghf_width;
    const int gh = g_pending_terrain_ghf_height;
    const float tile = g_pending_terrain_ghf_tile_size > 0.0f
        ? g_pending_terrain_ghf_tile_size : 0.5f;
    const auto& heights = g_pending_terrain_ghf_heights;
    const bool have_terrain = gw > 0 && gh > 0 &&
        heights.size() == size_t(gw) * size_t(gh);
    auto sample_height = [&](float x, float z) {
        if (!have_terrain) return 0.0f;
        int ix = std::clamp(int(x / tile), 0, gw - 1);
        int iz = std::clamp(int(z / tile), 0, gh - 1);
        return heights[size_t(iz) * size_t(gw) + size_t(ix)];
    };

    for (const auto& placement : g_level_gdb_placements) {
        const float wx = placement.x;
        const float wy = sample_height(placement.x, placement.y) + 1.0f;
        const float wz = placement.y;
        const float dx = wx - g_flycam.pos[0];
        const float dy = wy - g_flycam.pos[1];
        const float dz = wz - g_flycam.pos[2];
        const float view_x = dx * rx + dy * ry + dz * rz;
        const float view_y = dx * ux + dy * uy + dz * uz;
        const float view_z = dx * fx + dy * fy + dz * fz;
        if (view_z <= 0.05f) continue;
        const float ndc_x = view_x / (view_z * tan_half_fov * aspect);
        const float ndc_y = view_y / (view_z * tan_half_fov);
        if (ndc_x < -1.2f || ndc_x > 1.2f ||
            ndc_y < -1.2f || ndc_y > 1.2f) continue;
        ImVec2 point(origin.x + (ndc_x * 0.5f + 0.5f) * region.x,
                     origin.y + (1.0f - (ndc_y * 0.5f + 0.5f)) * region.y);
        const bool player_start = placement.marker == 0x00004B40u;
        const float radius = player_start ? 4.0f : 2.5f;
        dl->AddCircleFilled(point, radius,
            player_start ? IM_COL32(255, 80, 80, 230)
                         : IM_COL32(120, 220, 255, 180));
        if (player_start) {
            dl->AddCircle(point, radius + 1.0f,
                          IM_COL32(0, 0, 0, 200), 12, 1.0f);
            dl->AddText(ImVec2(point.x + radius + 4.0f, point.y - 7.0f),
                        IM_COL32(255, 150, 150, 235), "Player start");
        }
    }
}

void draw_details_overlays_gl(const ImVec2& origin, const ImVec2& region);

void draw_model_in_panel_gl() {
    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    int w = std::max(1, (int)region.x);
    int h = std::max(1, (int)region.y);

    if (!g_mp_initialized) {
        g_mp_initialized = MP_Init(g_mp, w, h);
    }
    if (!g_mp_initialized) {
        ImGui::Dummy(region);
        return;
    }

    MP_Resize(g_mp, w, h);
    ImGui::InvisibleButton("##model_render", region);
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();

    if (S.terrain_mode) {
        float dt = ImGui::GetIO().DeltaTime;
        if (hovered || g_flycam.is_looking ||
            g_flycam.right_press_pending) {
            ::render_panel_handle_flycam(dt);
        }
    } else {
        if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            const float kOrbitSensitivity = 0.008f;
            S.cam_yaw += d.x * kOrbitSensitivity;
            S.cam_pitch += d.y * kOrbitSensitivity;
            const float kPitchLimit = 1.5f;
            if (S.cam_pitch > kPitchLimit) S.cam_pitch = kPitchLimit;
            if (S.cam_pitch < -kPitchLimit) S.cam_pitch = -kPitchLimit;
        }
        if (hovered) {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {
                S.cam_dist *= (wheel > 0.0f) ? 0.9f : 1.111f;
                if (S.cam_dist < 0.3f) S.cam_dist = 0.3f;
                if (S.cam_dist > 50.0f) S.cam_dist = 50.0f;
            }
        }
        apply_orbit_to_flycam_gl();
    }

    for (size_t i = 0; i < g_mp.meshes.size(); ++i) {
        g_mp.meshes[i].highlight = ((int)i == ::g_highlight_mesh_idx);
        g_mp.meshes[i].isolated = ((int)i == ::g_isolate_mesh_idx);
    }

    MP_Render(g_mp, g_flycam);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    unsigned int tex = MP_GetTexture(g_mp);
    if (tex) {
        dl->AddImage((ImTextureID)(intptr_t)tex,
                     origin,
                     ImVec2(origin.x + region.x, origin.y + region.y),
                     ImVec2(0.0f, 1.0f),
                     ImVec2(1.0f, 0.0f));
    }

    if (hovered && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (S.content_tabs_visible && ContentTabs::HasTabs()) {
            ContentTabs::CloseActive();
        } else {
            MP_Release(g_mp);
            g_mp.has_model = false;
            g_mp_initialized = false;
            S.show_model_preview = false;
            S.model_preview_open = false;
            S.selected_bone = -1;
        }
    }

    dl->AddRectFilled(ImVec2(origin.x + 6, origin.y + 6),
                      ImVec2(origin.x + 196, origin.y + 70),
                      IM_COL32(20, 22, 28, 200), 4.0f);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 12));
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Controls");
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 30));
    ImGui::TextDisabled(S.terrain_mode ? "R-Drag  look" : "L-Drag  rotate");
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 46));
    ImGui::TextDisabled(S.terrain_mode ? "WASD/QE  move" : "Wheel  zoom  /  ESC  close");

    if (S.terrain_mode) draw_gdb_placements_overlay_gl(origin, region);
    draw_materials_overlay_gl(origin, region, origin.y + 76.0f);
    draw_details_overlays_gl(origin, region);
}

void draw_details_overlays_gl(const ImVec2& origin, const ImVec2& region) {
    if (S.show_item_details && S.selected_item >= 0 &&
        S.selected_item < (int)g_item_details.size() &&
        !LevelEdit::Enabled()) {
        const auto& it = g_item_details[(size_t)S.selected_item];
        static unsigned int icon_tex = 0;
        static uint32_t icon_for = 0xFFFFFFFFu;
        static int icon_w = 0, icon_h = 0;
        if (g_item_icon_dirty.exchange(false) || icon_for != it.record_hash) {
            icon_for = it.record_hash;
            if (icon_tex) glDeleteTextures(1, &icon_tex);
            icon_tex = 0; icon_w = icon_h = 0;
            std::vector<unsigned char> tex_buf;
            std::vector<uint8_t> rgba;
            bool has_alpha = false;
            if (!it.icon_tex.empty() &&
                build_any_tex_buffer_for_name(it.icon_tex, tex_buf, {}) &&
                decode_tex_to_rgba(tex_buf, rgba, icon_w, icon_h,
                                   &has_alpha, -1) && icon_w > 0 && icon_h > 0) {
                icon_tex = create_gl_texture_from_rgba(
                    icon_w, icon_h, rgba.data());
            }
        }
        static float alpha = 0.30f;
        constexpr float width = 300.0f, pad = 6.0f;
        ImGui::SetNextWindowPos(
            ImVec2(origin.x + region.x - width - pad, origin.y + pad));
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(width, 0), ImVec2(width, std::max(200.0f, region.y - 12.0f)));
        ImGui::SetNextWindowBgAlpha(alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##item_details_overlay", nullptr, flags)) {
            const ImVec2 wp = ImGui::GetWindowPos();
            const ImVec2 ws = ImGui::GetWindowSize();
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            bool hovered = mouse.x >= wp.x && mouse.x < wp.x + ws.x &&
                           mouse.y >= wp.y && mouse.y < wp.y + ws.y;
            static bool was_hovered = false;
            if (!hovered && was_hovered && ImGui::GetIO().MouseDown[0])
                hovered = true;
            was_hovered = hovered;
            const float target = hovered ? 1.0f : 0.30f;
            alpha += (target - alpha) * 0.18f;
            if (std::fabs(alpha - target) < 0.005f) alpha = target;
            ImGui::TextColored(ImVec4(1, .9f, .5f, 1), "Item Details");
            ImGui::Separator();
            std::string name;
            if (it.name_tag) TextBank::Lookup(it.name_tag, name);
            if (name.empty()) name = it.label;
            ImGui::TextColored(ImVec4(.65f, .85f, 1, 1), "%s", name.c_str());
            if (icon_tex) {
                float w = (float)icon_w, h = (float)icon_h;
                if (std::max(w, h) > 80.0f) {
                    float s = 80.0f / std::max(w, h); w *= s; h *= s;
                }
                ImGui::Image((ImTextureID)(intptr_t)icon_tex, ImVec2(w, h));
            }
            if (it.money >= 0) ImGui::Text("Value: %d gold", it.money);
            std::string desc;
            if (it.desc_tag) TextBank::Lookup(it.desc_tag, desc);
            if (!desc.empty()) {
                ImGui::Spacing(); ImGui::TextColored(
                    ImVec4(.65f, .85f, 1, 1), "Description");
                ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(desc.c_str());
                ImGui::PopTextWrapPos();
            }
            if (!it.stats.empty()) {
                ImGui::Spacing(); ImGui::TextColored(
                    ImVec4(.65f, .85f, 1, 1), "Stats");
                if (ImGui::BeginTable("##item_stats", 2,
                        ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Field");
                    ImGui::TableSetupColumn(
                        "Value", ImGuiTableColumnFlags_WidthFixed, 84.0f);
                    for (const auto& value : it.stats) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(value.first.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(value.second.c_str());
                    }
                    ImGui::EndTable();
                }
            }
        }
        ImGui::End(); ImGui::PopStyleVar();
    }

    if (S.show_entity_details && S.selected_entity >= 0 &&
        S.selected_entity < (int)g_global_entity_catalog.size() &&
        !LevelEdit::Enabled() &&
        ContentTabs::ActiveKind() == ContentTabs::Kind::Entity) {
        const auto& entity = g_global_entity_catalog[(size_t)S.selected_entity];
        static float alpha = 0.30f;
        constexpr float width = 350.0f, pad = 6.0f;
        const float height = std::min(620.0f, std::max(180.0f, region.y - 12.0f));
        ImGui::SetNextWindowPos(
            ImVec2(origin.x + region.x - width - pad, origin.y + pad));
        ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar;
        if (ImGui::Begin("##entity_details_overlay", nullptr, flags)) {
            const ImVec2 wp = ImGui::GetWindowPos();
            const ImVec2 ws = ImGui::GetWindowSize();
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            bool hovered = mouse.x >= wp.x && mouse.x < wp.x + ws.x &&
                           mouse.y >= wp.y && mouse.y < wp.y + ws.y;
            static bool was_hovered = false;
            if (!hovered && was_hovered && ImGui::GetIO().MouseDown[0])
                hovered = true;
            was_hovered = hovered;
            const float target = hovered ? 1.0f : 0.30f;
            alpha += (target - alpha) * 0.18f;
            if (std::fabs(alpha - target) < 0.005f) alpha = target;
            ImGui::TextColored(ImVec4(1, .9f, .5f, 1), "Entity Details");
            ImGui::Separator();
            ImGui::BeginChild("##entity_details_scroll");
            const std::string& name = entity.display_name.empty()
                ? entity.name : entity.display_name;
            ImGui::TextColored(ImVec4(.65f, .85f, 1, 1), "%s", name.c_str());
            if (S.dev_mode) {
                if (!entity.display_name.empty() &&
                    entity.display_name != entity.name)
                    ImGui::TextDisabled("Internal: %s", entity.name.c_str());
                ImGui::TextDisabled("Entity 0x%08X", entity.entity_hash);
            }

            static uint32_t cached_entity = 0;
            static uint64_t cached_bindings = 0;
            static uint64_t cached_catalog = 0;
            static size_t cached_clips = 0;
            static std::string anim_filter;
            static std::vector<std::pair<size_t, std::string>> animations;
            const uint64_t bindings = Anim::model_animation_binding_revision();
            if (cached_entity != entity.entity_hash ||
                cached_bindings != bindings ||
                cached_catalog != g_global_entity_catalog_revision ||
                cached_clips != S.anim_clips.size()) {
                animations.clear();
                anim_filter.clear();
                std::unordered_set<size_t> seen;
                const std::unordered_set<uint32_t> models(
                    entity.model_hashes.begin(), entity.model_hashes.end());
                for (const auto& binding : Anim::model_animation_bindings()) {
                    if (!models.count(binding.model_path_hash) ||
                        binding.clip_index >= S.anim_clips.size() ||
                        !seen.insert(binding.clip_index).second)
                        continue;
                    std::string anim_name = binding.animation_name.empty()
                        ? binding.source_name : binding.animation_name;
                    if (anim_name.empty())
                        anim_name = S.anim_clips[binding.clip_index].name;
                    animations.emplace_back(
                        binding.clip_index, std::move(anim_name));
                }
                cached_entity = entity.entity_hash;
                cached_bindings = bindings;
                cached_catalog = g_global_entity_catalog_revision;
                cached_clips = S.anim_clips.size();
            }

            ImGui::Spacing(); ImGui::Separator();
            ImGui::TextColored(ImVec4(.55f, .9f, 1, 1),
                               "Animations (%zu)", animations.size());
            if (animations.empty()) {
                ImGui::TextDisabled("None indexed");
            } else {
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##entity_animation_filter",
                    "Filter animations...", &anim_filter);
                std::string needle = anim_filter;
                std::transform(needle.begin(), needle.end(),
                               needle.begin(), ::tolower);
                std::vector<size_t> visible;
                for (size_t i = 0; i < animations.size(); ++i) {
                    std::string candidate = animations[i].second;
                    std::transform(candidate.begin(), candidate.end(),
                                   candidate.begin(), ::tolower);
                    if (needle.empty() ||
                        candidate.find(needle) != std::string::npos)
                        visible.push_back(i);
                }
                auto& player = Anim::global_player();
                const Anim::AnimClip* current = player.clip();
                if (current) {
                    const bool playing =
                        player.state() == Anim::AnimPlayer::State::Playing;
                    const bool paused =
                        player.state() == Anim::AnimPlayer::State::Paused;
                    if (UI::icon_button("##entity_anim_stop", ICON_FA_STOP,
                                        26.0f, false))
                        player.stop();
                    ImGui::SameLine();
                    if (UI::icon_button("##entity_anim_play",
                            playing ? ICON_FA_PAUSE : ICON_FA_PLAY,
                            30.0f, true)) {
                        if (playing) player.pause();
                        else if (paused) player.resume();
                        else player.play(current, player.is_loop());
                    }
                    ImGui::SameLine();
                    if (UI::icon_button("##entity_anim_loop", ICON_FA_REPEAT,
                            26.0f, false, player.is_loop()))
                        player.set_loop(!player.is_loop());
                    float time = player.time();
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::SliderFloat("##entity_anim_time", &time, 0.0f,
                            std::max(Anim::clip_duration_seconds(*current),
                                     0.001f), "%.2fs"))
                        player.seek(time);
                } else {
                    ImGui::TextDisabled("Select an animation to play it");
                }
                const float list_h = std::min(
                    240.0f, std::max(100.0f,
                                     ImGui::GetContentRegionAvail().y));
                ImGui::BeginChild("##entity_animation_list",
                                  ImVec2(0.0f, list_h), false);
                ImGuiListClipper clipper;
                clipper.Begin((int)visible.size());
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart;
                         row < clipper.DisplayEnd; ++row) {
                        const auto& animation =
                            animations[visible[(size_t)row]];
                        char label[192];
                        std::snprintf(label, sizeof(label), "%s  (%.2fs)",
                            animation.second.c_str(),
                            Anim::clip_duration_seconds(
                                S.anim_clips[animation.first]));
                        ImGui::PushID(row);
                        if (ImGui::Selectable(label,
                                S.anim_selected_clip ==
                                    (int)animation.first)) {
                            S.anim_selected_clip = (int)animation.first;
                            player.play(&S.anim_clips[animation.first],
                                        player.is_loop());
                        }
                        ImGui::PopID();
                    }
                }
                clipper.End();
                ImGui::EndChild();
            }
            ImGui::Spacing(); ImGui::Separator();
            ImGui::TextColored(ImVec4(.55f, .9f, 1, 1), "Model parts");
            if (entity.model_hashes.empty()) ImGui::TextDisabled("None");
            for (uint32_t hash : entity.model_hashes) {
                const FlatAssetEntry* match = FindGlobalModelAssetByPathHash(hash);
                if (match) ImGui::BulletText("%s", match->full_path.c_str());
                else ImGui::BulletText("Unresolved model 0x%08X", hash);
            }
            const auto gameplay = g_global_entity_gameplay.find(entity.entity_hash);
            if (gameplay != g_global_entity_gameplay.end())
                draw_entity_gameplay_details(gameplay->second, false);
            ImGui::EndChild();
        }
        ImGui::End(); ImGui::PopStyleVar();
    }
}

void draw_texture_popout_gl() {
    if (!::g_tex_popout_open || !::g_tex_popout_gl) return;

    int tw = 0;
    int th = 0;
    GLint prev_tex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
    glBindTexture(GL_TEXTURE_2D, ::g_tex_popout_gl);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex);

    if (tw > 0 && th > 0) {
        std::string title = "Texture: "
            + std::filesystem::path(::g_tex_popout_name).filename().string()
            + "##tex_popout";
        ImGuiWindowFlags fl = ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin(title.c_str(), &::g_tex_popout_open, fl)) {
            ImGui::Checkbox("Show UVs", &::g_tex_popout_show_uvs);
            ImGui::Image((ImTextureID)(intptr_t)::g_tex_popout_gl,
                         ImVec2((float)tw, (float)th));

            ImVec2 img_min = ImGui::GetItemRectMin();
            ImGui::SetCursorScreenPos(img_min);
            ImGui::InvisibleButton("##popout_hit",
                                   ImVec2((float)tw, (float)th));
            if (ImGui::BeginPopupContextItem()) {
                const std::string& preferred_bnk =
                    (S.selected_nested_index != -1 &&
                     !S.selected_nested_temp_path.empty())
                        ? S.selected_nested_temp_path
                        : S.selected_bnk;
                tex_export_menu_named(::g_tex_popout_name,
                                      ::g_tex_popout_name,
                                      preferred_bnk, 0);
                ImGui::EndPopup();
            }

            if (::g_tex_popout_show_uvs &&
                ::g_tex_popout_mesh_idx >= 0 &&
                (size_t)::g_tex_popout_mesh_idx < g_mp.meshes.size()) {
                uint32_t src = g_mp.meshes[(size_t)::g_tex_popout_mesh_idx].source_mesh_idx;
                if (src < S.mdl_meshes.size()) {
                    const auto& geom = S.mdl_meshes[src];
                    if (!geom.uvs.empty() && !geom.indices.empty()) {
                        ImVec2 img_max = ImGui::GetItemRectMax();
                        float w_px = img_max.x - img_min.x;
                        float h_px = img_max.y - img_min.y;
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        const ImU32 col = IM_COL32(255, 255, 255, 200);
                        for (size_t i = 0; i + 2 < geom.indices.size(); i += 3) {
                            uint32_t a = geom.indices[i];
                            uint32_t b = geom.indices[i + 1];
                            uint32_t c = geom.indices[i + 2];
                            if ((size_t)a * 2 + 1 >= geom.uvs.size()) continue;
                            if ((size_t)b * 2 + 1 >= geom.uvs.size()) continue;
                            if ((size_t)c * 2 + 1 >= geom.uvs.size()) continue;
                            ImVec2 pa(img_min.x + geom.uvs[a * 2 + 0] * w_px,
                                      img_min.y + geom.uvs[a * 2 + 1] * h_px);
                            ImVec2 pb(img_min.x + geom.uvs[b * 2 + 0] * w_px,
                                      img_min.y + geom.uvs[b * 2 + 1] * h_px);
                            ImVec2 pc(img_min.x + geom.uvs[c * 2 + 0] * w_px,
                                      img_min.y + geom.uvs[c * 2 + 1] * h_px);
                            dl->AddLine(pa, pb, col, 1.0f);
                            dl->AddLine(pb, pc, col, 1.0f);
                            dl->AddLine(pc, pa, col, 1.0f);
                        }
                    }
                }
            }
        }
        ImGui::End();
    }

    if (!::g_tex_popout_open) {
        ::g_tex_popout_gl = 0;
        ::g_tex_popout_name.clear();
    }
}
}

void draw_render_panel() {
    if (S.show_gdb_render) {
        draw_gdb_in_panel();
    } else if (S.content_tabs_visible && ContentTabs::HasTabs()) {
        ContentTabs::DrawTabBar();
        const ContentTabs::Kind kind = ContentTabs::ActiveKind();
        if (kind == ContentTabs::Kind::Lua ||
            kind == ContentTabs::Kind::Quest ||
            kind == ContentTabs::Kind::CustomQuest) {
            draw_lua_in_panel();
        } else if (kind == ContentTabs::Kind::Level) {
            const FlatAssetEntry* level_entry =
                ContentTabs::ActiveLevelEntry();
            const bool landscape_panel =
                level_entry && LandscapePanel::AppliesTo(*level_entry);
            if (landscape_panel) {
                LandscapePanel::DrawSidePanel(*level_entry);
                ImGui::SameLine();
                ImGui::BeginChild("##level_view_area", ImVec2(0, 0), false,
                                  ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse);
            }
            if (g_mp.has_model && S.terrain_mode && g_mp.no_tilt) {
                draw_model_in_panel_gl();
            } else {
                draw_placeholder();
            }
            if (landscape_panel) ImGui::EndChild();
        } else if (kind == ContentTabs::Kind::Model) {
            if (ContentTabs::ActiveHasModel() && g_mp.has_model &&
                !S.terrain_mode && !g_mp.no_tilt) {
                draw_model_in_panel_gl();
            } else {
                draw_placeholder();
            }
        } else if (kind == ContentTabs::Kind::Item) {
            if (ContentTabs::ActiveHasModel() && g_mp.has_model &&
                !S.terrain_mode && !g_mp.no_tilt) {
                draw_model_in_panel_gl();
            } else {
                draw_item_tab_content();
            }
        } else if (kind == ContentTabs::Kind::Entity) {
            if (ContentTabs::ActiveHasModel() && g_mp.has_model &&
                !S.terrain_mode && !g_mp.no_tilt) {
                draw_model_in_panel_gl();
            } else {
                draw_entity_tab_content();
            }
        } else {
            draw_placeholder();
        }
    } else if (g_mp.has_model) {
        draw_model_in_panel_gl();
    } else if (S.texture_window_gl) {
        draw_texture_in_panel_gl();
    } else if (S.show_lua_render) {
        draw_lua_in_panel();
    } else {
        draw_placeholder();
    }
    draw_texture_popout_gl();
}
#endif

}
