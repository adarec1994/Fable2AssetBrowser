static void append_transformed_prop_geom(std::vector<MDLMeshGeom>& out,
                                         const MDLMeshGeom& src,
                                         const Level::PropInstance& inst,
                                         float ,
                                         float )
{
    MDLMeshGeom dst = src;

    for (size_t i = 0; i + 2 < dst.positions.size(); i += 3) {
        const float lx = src.positions[i + 0];
        const float ly = src.positions[i + 2];
        const float lz = src.positions[i + 1];
        transform_instance_point(inst, lx, ly, lz,
                                 dst.positions[i + 0],
                                 dst.positions[i + 1],
                                 dst.positions[i + 2]);
    }

    if (dst.normals.size() == src.normals.size()) {
        for (size_t i = 0; i + 2 < dst.normals.size(); i += 3) {
            const float lx = src.normals[i + 0];
            const float ly = src.normals[i + 2];
            const float lz = src.normals[i + 1];
            transform_instance_normal(inst, lx, ly, lz,
                                      dst.normals[i + 0],
                                      dst.normals[i + 1],
                                      dst.normals[i + 2]);
        }
    }

    dst.name = src.name.empty()
        ? std::string("prop")
        : std::string("prop: ") + src.name;
    out.push_back(std::move(dst));
}

static void append_level_props_to_geoms(std::vector<MDLMeshGeom>& geoms)
{
    if (g_pending_level_prop_blocks.empty()) return;

    const float terrain_cx =
        (float(g_pending_terrain_ghf_width) - 1.0f) * 0.5f *
        g_pending_terrain_ghf_tile_size;
    const float terrain_cz =
        (float(g_pending_terrain_ghf_height) - 1.0f) * 0.5f *
        g_pending_terrain_ghf_tile_size;

    std::unordered_map<std::string, CachedPropModel> cache;
    size_t instances_seen = 0;
    size_t instances_loaded = 0;
    size_t models_failed = 0;
    size_t misses_logged = 0;
    uint32_t next_selection_id = 1;

    if (g_pending_level_model_body_bnk.empty()) {
        OutputLog::warn("level props: no level model body BNK resolved");
    } else {
        OutputLog::info("level props: model body BNK " +
                        std::filesystem::path(g_pending_level_model_body_bnk)
                            .filename().string());
    }

    for (const auto& block : g_pending_level_prop_blocks) {
        if (block.model_path.empty()) continue;
        auto& cached = cache[block.model_path];
        if (!cached.loaded && cached.geoms.empty()) {
            cached.loaded =
                load_cached_prop_model(block.model_path,
                                       g_pending_level_model_body_bnk,
                                       cached);
            if (!cached.loaded) {
                ++models_failed;
                if (misses_logged < 5) {
                    ++misses_logged;
                    OutputLog::warn("level props: model load miss " +
                                    block.model_path);
                }
                cached.loaded = true;
            }
        }

        if (cached.geoms.empty()) continue;

        std::vector<MDLMeshGeom> combined(cached.geoms.size());
        std::vector<size_t> chunk_index(cached.geoms.size(), 0);
        for (size_t gi = 0; gi < cached.geoms.size(); ++gi) {
            const auto& src = cached.geoms[gi];
            init_combined_prop_geom(combined[gi], src, block.model_path,
                                    block.instances.size(), block.type, 0);
        }

        for (const auto& inst : block.instances) {
            const uint32_t selection_id = next_selection_id++;
            if (next_selection_id == 0) next_selection_id = 1;
            ++instances_seen;
            for (size_t gi = 0; gi < cached.geoms.size(); ++gi) {
                const auto& src = cached.geoms[gi];
                if (!src.positions.empty() && !src.indices.empty()) {
                    if (would_exceed_combined_prop_limits(combined[gi], src)) {
                        flush_combined_prop_geom(
                            geoms, combined[gi], src, block.model_path,
                            block.instances.size(), block.type,
                            chunk_index[gi]);
                    }
                    merge_transformed_instance_into(combined[gi], src, inst,
                                                    selection_id);
                }
            }
            ++instances_loaded;
        }

        for (auto& cg : combined) {
            if (!cg.positions.empty() && !cg.indices.empty()) {
                geoms.push_back(std::move(cg));
            }
        }
        (void)terrain_cx; (void)terrain_cz;
    }

    OutputLog::info("level props: appended " +
                    std::to_string(instances_loaded) +
                    " of " +
                    std::to_string([&] {
                        size_t n = 0;
                        for (const auto& b : g_pending_level_prop_blocks) {
                            n += b.instances.size();
                        }
                        return n;
                    }()) +
                    " prop instances" +
                    (models_failed ? " (" + std::to_string(models_failed) +
                                      " model load misses)" : std::string()));
}
