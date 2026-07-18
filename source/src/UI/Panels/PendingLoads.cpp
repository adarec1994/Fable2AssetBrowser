#include "../UI_Panels.h"
#include "PanelInternal.h"
#include "../ModelPreview.h"
#include "../EntityModelResolver.h"
#include "../ContentTabs.h"
#include "../OutputLog.h"
#include "../../textures/TexParser.h"
#include "../../textures/LhTexCodec.h"
#include "../../MDL/ModelParser.h"
#include "../../MDL/mdl_converter.h"
#include "../../animations/AnimBank.h"
#include "../../Level/Core/LevelLoader.h"
#include "../../Level/Terrain/TextureAtlasDecoder.h"
#include "../../Level/Terrain/TerrainTextureRegistry.h"
#include "../../Level/Terrain/EhfLodThumbnails.h"
#include "../../Level/Terrain/EhfChunkParser.h"
#include "../../Level/Terrain/TerrainSplat.h"
#include "../../Level/Terrain/TerrainEdit.h"
#include "../../Level/Terrain/TerrainPaint.h"
#include "../../Level/Editing/LevelEdit.h"
#include "../../Level/Creation/LandscapeAuthoring.h"
#include "../../Level/Skybox/SkyboxPreviewBinding.h"
#include "../../Utilities/Files.h"
#include "../../Utilities/Utils.h"
#include "../../Utilities/Progress.h"
#include "../../BNKCore.cpp"
#include <filesystem>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <mutex>
#include <thread>
#include <ctime>
#include <cstring>
#include <cctype>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#ifndef _WIN32
#include <GL/glew.h>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

std::atomic<bool> g_pending_mdl_load{false};
int g_pending_mdl_index = -1;
std::string g_pending_mdl_full_path;
std::atomic<bool> g_pending_mdl_is_item{false};
std::atomic<bool> g_item_icon_dirty{false};

std::atomic<bool> g_pending_tex_load{false};
int g_pending_tex_index = -1;

extern ModelPreview g_mp;
extern bool         g_mp_initialized;
extern FlyCam       g_flycam;

namespace {
#include "PendingLoads/Cache/State.inl"
#include "PendingLoads/Models/Paths.inl"
#include "PendingLoads/Models/Loading.inl"
#include "PendingLoads/Geometry/Transforms.inl"
#include "PendingLoads/Geometry/Batching.inl"

#ifdef _WIN32
#include "PendingLoads/Streaming/State.inl"
#include "PendingLoads/Streaming/Worker.inl"
#include "PendingLoads/Streaming/Start.inl"
#include "PendingLoads/Terrain/GeneratedTextures.inl"
#include "PendingLoads/Streaming/Upload.inl"
#endif

#include "PendingLoads/Geometry/PropAppending.inl"

}

#ifdef _WIN32
#include "PendingLoads/Editing/ModelSpawning.inl"
#include "PendingLoads/Editing/EntitySpawning.inl"
#include "PendingLoads/Editing/ContainerSpawning.inl"
#endif

#include "PendingLoads/Editing/Guard.inl"

#ifdef _WIN32
void process_pending_loads(ID3D11Device* device) {
#else
void process_pending_loads() {
#endif
#include "PendingLoads/Processing/Guards.inl"
#include "PendingLoads/Processing/ModelLoads.inl"
#include "PendingLoads/Processing/TextureLoads.inl"
#include "PendingLoads/Processing/PreviewBuild.inl"

#ifdef _WIN32
#include "PendingLoads/Processing/DirectX/HeightmapViews.inl"
#include "PendingLoads/Processing/DirectX/Terrain.inl"
#else
#include "PendingLoads/Processing/OpenGL/Terrain.inl"
#endif
}
