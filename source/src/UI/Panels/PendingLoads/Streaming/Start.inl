static bool start_level_prop_stream(std::vector<MDLMeshGeom> geoms,
                                    MDLInfo info)
{
    if (g_level_prop_stream.worker.joinable()) {
        g_level_prop_stream.worker.join();
    }
    g_level_prop_stream.phase.store(0);
    g_level_prop_stream.instances_loaded.store(0);
    g_level_prop_stream.total_instances = 0;
    g_level_prop_stream.model_misses    = 0;
    g_level_prop_stream.terrain_textures.clear();
    g_level_prop_stream.geoms           = std::move(geoms);
    g_level_prop_stream.info            = std::move(info);
    g_level_prop_stream.blocks          = g_pending_level_prop_blocks;
    g_level_prop_stream.entity_batches.clear();
    std::unordered_map<std::string, size_t> entity_batch_by_key;
    for (size_t marker_index = 0;
         marker_index < g_level_spawn_markers.size(); ++marker_index) {
        const LevelSpawnMarker& marker =
            g_level_spawn_markers[marker_index];
        const bool placed_character = marker.kind == 3;
        const bool generator_spawn_preview =
            marker.kind == 2 && !marker.model_hashes.empty();
        if ((!placed_character && !generator_spawn_preview) ||
            marker.entity_hash == 0 ||
            (placed_character &&
             LevelEdit::EntityRemovalPending(marker.entity_hash)) ||
            (generator_spawn_preview &&
             LevelEdit::SpawnPointRemovalPending(marker.entity_hash))) {
            continue;
        }

        std::vector<uint32_t> model_hashes = marker.model_hashes;







        std::unordered_set<uint32_t> seen_model_hashes;
        model_hashes.erase(
            std::remove_if(model_hashes.begin(), model_hashes.end(),
                           [&](uint32_t hash) {
                               return hash == 0 ||
                                      !seen_model_hashes.insert(hash).second;
                           }),
            model_hashes.end());
        if (model_hashes.empty()) continue;

        std::ostringstream key_builder;
        for (uint32_t hash : model_hashes) {
            key_builder << std::hex << hash << ';';
        }
        const std::string key = key_builder.str();
        auto inserted = entity_batch_by_key.emplace(
            key, g_level_prop_stream.entity_batches.size());
        if (inserted.second) {
            LevelPropStreamState::EntityBatch batch;
            batch.model_hashes = model_hashes;
            batch.label = generator_spawn_preview &&
                                  !marker.creature_name.empty()
                ? marker.creature_name
                : marker.name;
            g_level_prop_stream.entity_batches.push_back(std::move(batch));
        }
        auto& batch = g_level_prop_stream.entity_batches[inserted.first->second];
        if (batch.label.empty() && !marker.name.empty()) {
            batch.label = marker.name;
        }

        LevelPropStreamState::EntityInstance entity_instance;
        Level::PropInstance& instance = entity_instance.instance;
        instance.hash = marker.entity_hash;
        instance.gdb_entity_hash = marker.entity_hash;
        instance.values[0] = marker.x;
        instance.values[1] = marker.y;
        instance.values[2] = marker.z;
        instance.gdb_pos_off[0] = marker.pos_off[0];
        instance.gdb_pos_off[1] = marker.pos_off[1];
        instance.gdb_pos_off[2] = marker.pos_off[2];
        instance.gdb_rot_off[0] = marker.rot_off[0];
        instance.gdb_rot_off[1] = marker.rot_off[1];
        instance.gdb_rot_off[2] = marker.rot_off[2];
        instance.lev_rec_kind = 3;
        fill_entity_instance_rotation(instance, marker);
        entity_instance.selection_id =
            0x70000000u | uint32_t(marker_index);
        batch.instances.push_back(std::move(entity_instance));
    }
    g_level_prop_stream.model_body_bnk  = g_pending_level_model_body_bnk;
    g_level_prop_stream.sky_theme       = g_pending_level_sky_theme;
    g_level_prop_stream.cloud_theme     = g_pending_level_cloud_theme;
    g_level_prop_stream.weather_theme   = g_pending_level_weather_theme;
    g_level_prop_stream.environment_timeline =
        g_pending_level_environment_timeline;
    g_level_prop_stream.terrain_tile_size = g_pending_terrain_ghf_tile_size;
    g_level_prop_stream.terrain_width   = g_pending_terrain_ghf_width;
    g_level_prop_stream.terrain_height  = g_pending_terrain_ghf_height;

    for (const auto& b : g_level_prop_stream.blocks) {
        g_level_prop_stream.total_instances += b.instances.size();
    }
    for (const auto& batch : g_level_prop_stream.entity_batches) {
        g_level_prop_stream.total_instances += batch.instances.size();
    }
    if (g_level_prop_stream.total_instances == 0) {
        g_level_prop_stream.geoms.clear();
        g_level_prop_stream.info = MDLInfo{};
        return false;
    }

    progress_open((int)g_level_prop_stream.total_instances,
                  "Loading level models...");

    g_level_prop_stream.phase.store(1);
    g_level_prop_stream.worker = std::thread(prop_worker_run,
                                             &g_level_prop_stream);
    return true;
}
