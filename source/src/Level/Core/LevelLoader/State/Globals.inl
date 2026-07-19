std::atomic<bool>   g_pending_terrain_load{false};
std::atomic<bool>   g_level_export_only_load{false};
Level::TerrainMesh  g_pending_terrain_mesh;
std::string         g_pending_terrain_label;
FlatAssetEntry      g_pending_terrain_level_entry;
std::vector<uint8_t> g_pending_terrain_ehf_bytes;
std::vector<Level::PendingAdjacentTerrain> g_pending_adjacent_terrain_meshes;

std::vector<uint8_t>  g_pending_terrain_ghf_payload;
std::vector<float>    g_pending_terrain_ghf_heights;
float                 g_pending_terrain_ghf_tile_size = 1.f;
int                   g_pending_terrain_ghf_width = 0;
int                   g_pending_terrain_ghf_height = 0;
FlatAssetEntry        g_pending_terrain_ghf_entry;
std::vector<Level::PropBlock> g_pending_level_prop_blocks;
std::string                   g_pending_level_model_body_bnk;
Level::WaterScene             g_pending_level_water_scene;
bool                          g_pending_level_water_present = false;
Gdb::WaterTheme               g_pending_level_water_theme;
Gdb::SkyTheme                 g_pending_level_sky_theme;
Gdb::CloudTheme               g_pending_level_cloud_theme;
Gdb::WeatherTheme             g_pending_level_weather_theme;
Gdb::EnvironmentThemeTimeline g_pending_level_environment_timeline;
std::vector<Fx::Placement>    g_pending_level_fx;
Fx::Bank                      g_particle_bank;
bool                          g_particle_bank_loaded = false;
std::vector<std::string>           g_level_vfs_texture_body_bnks;
std::vector<std::string>           g_level_vfs_model_bnks;
std::vector<std::string>           g_level_vfs_streaming_bnks;
std::vector<HavokCollisionMesh>    g_level_havok_collision;
std::vector<GdbWorldPlacement>     g_level_gdb_placements;
std::unordered_map<uint32_t, Gdb::EntityContents> g_level_entity_contents;
std::unordered_map<uint32_t, Gdb::EntityGameplayDetails>
    g_level_entity_gameplay;
std::unordered_map<uint32_t, Gdb::PropertyDetails>
    g_level_property_details;
std::vector<Gdb::ItemCatalogEntry> g_level_item_catalog;
std::vector<Gdb::ItemDetail> g_item_details;
std::unordered_map<uint32_t, Gdb::PropTemplateInfo>
    g_level_prop_entity_templates;
std::unordered_map<uint32_t, Gdb::EntityTextTags> g_level_entity_text;
std::vector<LevelSpawnMarker> g_level_spawn_markers;
std::vector<Gdb::TransitionPoint> g_level_transition_points;
Gdb::SpawnDonorInfo g_level_spawn_donor;
Gdb::NpcDonorInfo g_level_npc_donor;
std::vector<Gdb::CreatureCatalogEntry> g_level_creature_catalog;
std::vector<Gdb::CreatureCatalogEntry> g_global_entity_catalog;
std::unordered_map<uint32_t, std::vector<uint32_t>>
    g_global_entity_model_hashes;
uint64_t g_global_entity_catalog_revision = 0;
