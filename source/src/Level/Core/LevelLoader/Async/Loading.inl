bool IsAsyncLoadInProgress()
{
    return g_level_async_loading.load() ||
           g_pending_terrain_load.load() ||
           S.show_progress.load();
}

void OpenAsync(const FlatAssetEntry& entry)
{
    bool expected = false;
    if (!g_level_async_loading.compare_exchange_strong(expected, true)) {
        OutputLog::warn("level load already in progress");
        return;
    }

    S.cancel_requested.store(false);

    progress_open(100, "Loading level...");
    std::thread([entry]() {
        progress_update(5, 100, "Extracting level...");
        const bool ok = Open(entry);
        const bool cancelled = S.cancel_requested.load();
        if (cancelled) {
            g_pending_terrain_load.store(false);
            g_pending_level_prop_blocks.clear();
            g_pending_adjacent_terrain_meshes.clear();
            g_pending_terrain_mesh = Level::TerrainMesh{};
            g_pending_terrain_ehf_bytes.clear();
            g_pending_terrain_ghf_heights.clear();
            g_pending_terrain_ghf_payload.clear();
            g_pending_terrain_ghf_width = 0;
            g_pending_terrain_ghf_height = 0;
            g_pending_terrain_ghf_tile_size = 1.0f;
            g_pending_terrain_ghf_entry = FlatAssetEntry{};
            g_pending_terrain_level_entry = FlatAssetEntry{};
            g_pending_terrain_label.clear();
            g_pending_level_model_body_bnk.clear();
            g_pending_level_water_present = false;
            g_pending_level_water_scene = Level::WaterScene{};
            g_pending_level_water_theme = Gdb::WaterTheme{};
            g_pending_level_sky_theme = Gdb::SkyTheme{};
            g_pending_level_cloud_theme = Gdb::CloudTheme{};
            g_pending_level_weather_theme = Gdb::WeatherTheme{};
            g_pending_level_environment_timeline =
                Gdb::EnvironmentThemeTimeline{};
            g_level_havok_collision.clear();
            g_level_gdb_placements.clear();
            g_level_entity_contents.clear();
            g_level_entity_gameplay.clear();
            g_level_property_details.clear();
            g_level_entity_text.clear();
            g_level_spawn_markers.clear();
            g_level_creature_catalog.clear();
            g_level_vfs_texture_body_bnks.clear();
            g_level_vfs_model_bnks.clear();
            g_level_vfs_streaming_bnks.clear();
            progress_done();
            OutputLog::warn("Level load cancelled.");
            S.cancel_requested.store(false);
        } else if (!ok) {
            progress_done();
        } else {
            progress_update(70, 100, "Preparing render...");
            if (!g_pending_terrain_load.load()) {
                progress_done();
            }
        }
        g_level_async_loading.store(false);
    }).detach();
}
