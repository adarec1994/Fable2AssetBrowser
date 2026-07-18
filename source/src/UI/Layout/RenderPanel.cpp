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
#include "../Panels/DetailsPanel.h"
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

#include "RenderPanel/Shared/ContentViews.inl"

#ifdef _WIN32

namespace {
#include "RenderPanel/DirectX/TexturePreview.inl"
#include "RenderPanel/DirectX/SceneProjection.inl"
#include "RenderPanel/DirectX/EntityDetails.inl"
#include "RenderPanel/DirectX/SceneInteraction.inl"
#include "RenderPanel/DirectX/ModelView.inl"
#include "RenderPanel/DirectX/Popouts.inl"
#endif

}

#ifdef _WIN32
#include "RenderPanel/DirectX/PublicApi.inl"
#else
namespace {
#include "RenderPanel/OpenGL/TexturePreview.inl"
#include "RenderPanel/OpenGL/Overlays.inl"
#include "RenderPanel/OpenGL/ModelView.inl"
#include "RenderPanel/OpenGL/Popouts.inl"
}

#include "RenderPanel/OpenGL/PublicApi.inl"
#endif

}
