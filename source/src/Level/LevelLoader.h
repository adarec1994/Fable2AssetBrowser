#pragma once

#include <atomic>
#include <string>
#include <vector>
#include <cstdint>

#include "HeightfieldLoader.h"
#include "GdbParser.h"
#include "WaterParser.h"

struct FlatAssetEntry;

namespace Level {

struct EngineLevelEntry {
    uint32_t type   = 0;
    size_t   offset = 0;
    size_t   size   = 0;
    std::string str_a;
    std::string str_b;
};

struct PropInstance {
    uint64_t hash = 0;
    uint8_t flags[3] = {0, 0, 0};
    float values[20] = {};
    bool has_full_transform = false;
    uint32_t pos_file_offset = 0;
    uint32_t gdb_pos_off[3] = {0, 0, 0};
};

struct PropBlock {
    size_t offset = 0;
    uint32_t type = 0;
    std::string model_path;
    std::string shadow_model_path;
    std::string lod_model_path;
    std::string extra_model_path;
    std::vector<PropInstance> instances;
};

struct EngineLevelInfo {
    bool                          ok        = false;
    std::string                   source_path;
    uint32_t                      version   = 0;
    uint32_t                      entry_count = 0;
    std::vector<EngineLevelEntry> entries;
    std::vector<PropBlock>        prop_blocks;
    std::string                   error;
};

struct LevelResources {
    std::string ehf_path;
    std::string ghf_path;
    std::string hdb_path;
    std::string genv_path;
    std::string ama_path;
    std::string amm_path;
    std::string amr_path;
    std::string model_body_bnk;
};

bool Open(const FlatAssetEntry& entry);

void OpenAsync(const FlatAssetEntry& entry);
bool IsAsyncLoadInProgress();

bool ParseEngineLevel(const std::vector<uint8_t>& bytes,
                      EngineLevelInfo&            out);

bool RenderHeightmapToRGBA(const FlatAssetEntry& entry,
                           std::vector<uint8_t>& out_rgba,
                           int&                  out_w,
                           int&                  out_h);

bool RenderPf99ToRGBA(const FlatAssetEntry& entry,
                      std::vector<uint8_t>& out_rgba,
                      int&                  out_w,
                      int&                  out_h);

bool DecodeLevelTextureAtlas(const FlatAssetEntry& level_entry,
                             std::vector<uint8_t>& out_rgba,
                             int&                  out_w,
                             int&                  out_h);

bool DecodeEhfTerrainAlbedo(const FlatAssetEntry& level_entry,
                            uint32_t              cells_w,
                            uint32_t              cells_h,
                            std::vector<uint8_t>& out_rgba,
                            int&                  out_w,
                            int&                  out_h);

bool DecodeEhfTerrainAlbedoFromBytes(const std::vector<uint8_t>& ehf,
                                     uint32_t              cells_w,
                                     uint32_t              cells_h,
                                     std::vector<uint8_t>& out_rgba,
                                     int&                  out_w,
                                     int&                  out_h);

bool DecodeEhfPaletteFirstDiffuse(const std::vector<uint8_t>& ehf,
                                  std::vector<uint8_t>& out_rgba,
                                  int&                  out_w,
                                  int&                  out_h,
                                  float&                out_tile_scale,
                                  std::string&          out_picked_name);

bool BakeEhfTerrainCompositeWithBnk(const std::vector<uint8_t>& ehf,
                                    const std::string& preferred_bnk,
                                    std::vector<uint8_t>& out_rgba,
                                    int& out_w, int& out_h,
                                    std::string& out_picked_name,
                                    bool allow_embedded_albedo = true);

bool BakeEhfVistaPageComposite(const std::vector<uint8_t>& ehf,
                               std::vector<uint8_t>& out_rgba,
                               int& out_w, int& out_h,
                               std::string& out_name);

bool BakeEhfTerrainCompositeAndSplat(
    const std::vector<uint8_t>& ehf,
    const std::string& preferred_bnk,
    std::vector<uint8_t>& out_rgba,
    int& out_w, int& out_h,
    std::string& out_picked_name,
    std::vector<uint8_t>& out_splat_rgba,
    int& out_splat_w, int& out_splat_h);

bool BakeEhfTerrainComposite(const std::vector<uint8_t>& ehf,
                             std::vector<uint8_t>&  out_rgba,
                             int&                   out_w,
                             int&                   out_h,
                             std::string&           out_picked_name);

}

extern std::atomic<bool>  g_pending_terrain_load;
extern std::atomic<bool>  g_level_export_only_load;
extern Level::TerrainMesh g_pending_terrain_mesh;
extern std::string        g_pending_terrain_label;
extern FlatAssetEntry     g_pending_terrain_level_entry;
extern std::vector<uint8_t> g_pending_terrain_ehf_bytes;

namespace Level {
// One background ("vista") patch drawn the way the engine draws it: each
// M x M cell is a 16x16 sub-grid whose atlas uv comes from the .ehf's two
// pf98 per-cell lookup tables (17 non-uniform coords per axis; one tile per
// cell, packed arbitrarily in the patch's page), textured with the patch's
// own decoded background-map page. Populated for the dedicated vista filler
// heightfields; when present it replaces the single-mesh + composite path.
struct VistaPatchGeom {
    TerrainMesh          mesh;
    std::vector<uint8_t> page_rgba;   // this patch's page atlas (RGBA)
    int                  page_w = 0;
    int                  page_h = 0;
};

struct PendingAdjacentTerrain {
    TerrainMesh          mesh;
    std::string          label;
    std::vector<uint8_t> ehf_bytes;
    std::string          preferred_bnk;
    bool                 preserve_mesh_uvs = false;
    bool                 prefer_embedded_albedo = false;
    std::vector<VistaPatchGeom> patch_geoms;   // per-patch path (preferred)
};
}
extern std::vector<Level::PendingAdjacentTerrain> g_pending_adjacent_terrain_meshes;

extern std::vector<uint8_t>  g_pending_terrain_ghf_payload;
extern std::vector<float>    g_pending_terrain_ghf_heights;
extern float                 g_pending_terrain_ghf_tile_size;
extern int                   g_pending_terrain_ghf_width;
extern int                   g_pending_terrain_ghf_height;
extern FlatAssetEntry        g_pending_terrain_ghf_entry;

extern std::vector<Level::PropBlock> g_pending_level_prop_blocks;
extern std::string                   g_pending_level_model_body_bnk;

extern Level::WaterScene g_pending_level_water_scene;
extern bool              g_pending_level_water_present;
extern Gdb::WaterTheme   g_pending_level_water_theme;
extern Gdb::SkyTheme     g_pending_level_sky_theme;
extern Gdb::CloudTheme   g_pending_level_cloud_theme;
extern Gdb::WeatherTheme g_pending_level_weather_theme;
extern Gdb::EnvironmentThemeTimeline g_pending_level_environment_timeline;

extern std::vector<std::string> g_level_vfs_texture_body_bnks;
extern std::vector<std::string> g_level_vfs_model_bnks;
extern std::vector<std::string> g_level_vfs_streaming_bnks;

struct HavokCollisionMesh {
    std::vector<float>    vertices;
    std::vector<uint16_t> indices16;
    std::vector<uint32_t> indices32;
};
extern std::vector<HavokCollisionMesh> g_level_havok_collision;

struct GdbWorldPlacement {
    float    x, y, z;
    float    yaw;
    float    rot_x, rot_y, rot_z;
    float    scale;
    uint32_t hash;
    uint32_t parent_hash;
    uint32_t model_path_hash;
    uint32_t marker;
};
extern std::vector<GdbWorldPlacement> g_level_gdb_placements;
