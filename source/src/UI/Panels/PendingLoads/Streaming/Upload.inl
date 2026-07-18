static bool stream_level_prop_batch(ID3D11Device* device)
{
    const int phase = g_level_prop_stream.phase.load(std::memory_order_acquire);
    if (phase == 0 || phase == 3) return false;

    const size_t loaded =
        g_level_prop_stream.instances_loaded.load(std::memory_order_relaxed);
    const size_t total = g_level_prop_stream.total_instances;
    progress_update((int)loaded, (int)std::max<size_t>(total, 1),
                    S.cancel_requested.load()
                        ? std::string("Cancelling prop load...")
                        : std::string());

    if (phase != 2) return true;

    if (g_level_prop_stream.worker.joinable()) {
        g_level_prop_stream.worker.join();
    }

    if (S.cancel_requested.load()) {
        OutputLog::warn("prop upload aborted: cancel requested");
        g_level_prop_stream.geoms.clear();
        g_level_prop_stream.geoms.shrink_to_fit();
        g_level_prop_stream.blocks.clear();
        g_level_prop_stream.entity_batches.clear();
        g_level_prop_stream.terrain_textures.clear();
        g_level_prop_stream.phase.store(3);
        progress_done();
        S.cancel_requested.store(false);
        return true;
    }

    if (!g_level_prop_stream.geoms.empty()) {
        FlyCam saved_cam = g_flycam;
        if (!g_mp_initialized) {
            MP_Init(device, g_mp, 800, 600);
            g_mp_initialized = true;
        }
        try {
            MP_Build(device, g_level_prop_stream.geoms,
                     g_level_prop_stream.info, g_mp);
            Skybox::PreviewBinding::ApplySkyTheme(
                g_mp, g_level_prop_stream.sky_theme);
            Skybox::PreviewBinding::ApplyCloudTheme(
                g_mp, g_level_prop_stream.cloud_theme);
            Skybox::PreviewBinding::ApplyWeatherTheme(
                g_mp, g_level_prop_stream.weather_theme,
                g_level_prop_stream.sky_theme);
            Skybox::PreviewBinding::ApplyEnvironmentTimeline(
                g_mp, g_level_prop_stream.environment_timeline);
        } catch (const std::exception& e) {
            OutputLog::error(std::string("level props: MP_Build failed (") +
                             e.what() + ")");
        } catch (...) {
            OutputLog::error("level props: MP_Build failed (unknown exception)");
        }
        bind_generated_terrain_textures(
            device,
            g_level_prop_stream.terrain_textures,
            "terrain texture rebound after prop upload");
        size_t water_meshes = 0;
        for (const auto& m : g_mp.meshes) {
            if (m.is_water) ++water_meshes;
        }
        if (water_meshes > 0) {
            OutputLog::success("water: " + std::to_string(water_meshes) +
                               " mesh(es) active after prop upload");
        }
        g_mp.no_tilt = true;
        S.terrain_mode = true;
        g_flycam = saved_cam;
    }

    OutputLog::info("level props: streamed " + std::to_string(loaded) +
                    " prop instances" +
                    (g_level_prop_stream.model_misses
                        ? " (" + std::to_string(g_level_prop_stream.model_misses) +
                          " model load misses)"
                        : std::string()));
    progress_done();

    g_level_prop_stream.geoms.clear();
    g_level_prop_stream.geoms.shrink_to_fit();
    g_level_prop_stream.blocks.clear();
    g_level_prop_stream.blocks.shrink_to_fit();
    g_level_prop_stream.entity_batches.clear();
    g_level_prop_stream.entity_batches.shrink_to_fit();
    g_level_prop_stream.terrain_textures.clear();
    g_level_prop_stream.terrain_textures.shrink_to_fit();
    g_level_prop_stream.phase.store(3);
    return true;
}
