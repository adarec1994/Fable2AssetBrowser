static void prop_worker_run(LevelPropStreamState* s)
{
    std::string current_model;
    try {
        const float terrain_cx =
            (float(s->terrain_width) - 1.0f) * 0.5f * s->terrain_tile_size;
        const float terrain_cz =
            (float(s->terrain_height) - 1.0f) * 0.5f * s->terrain_tile_size;

        std::unordered_map<std::string, CachedPropModel> cache;
        uint32_t next_selection_id = 1;
        bool cancelled = false;

        for (const auto& block : s->blocks) {
            current_model = block.model_path;
            if (S.cancel_requested.load()) {
                OutputLog::warn("prop bake worker aborted: cancel requested");
                cancelled = true;
                break;
            }
            if (block.model_path.empty()) {
                s->instances_loaded.fetch_add(block.instances.size(),
                                              std::memory_order_relaxed);
                continue;
            }

            auto& cached = cache[block.model_path];
            if (!cached.loaded) {
                load_cached_prop_model(block.model_path,
                                       s->model_body_bnk,
                                       cached);
                cached.loaded = true;
                if (cached.geoms.empty()) {
                    ++s->model_misses;
                    OutputLog::warn("level props: model load miss " +
                                    block.model_path);

                    std::string want = block.model_path;
                    size_t sl = want.find_last_of("/\\");
                    std::string want_base = (sl == std::string::npos)
                                               ? want : want.substr(sl + 1);
                    std::string stem = want_base;
                    size_t dot = stem.find_last_of('.');
                    if (dot != std::string::npos) stem.resize(dot);
                    std::transform(stem.begin(), stem.end(), stem.begin(),
                                   ::tolower);

                    int shown = 0;
                    for (const auto& e : S.all_mdl_files) {
                        if (shown >= 5) break;
                        std::string lname = e.name;
                        std::transform(lname.begin(), lname.end(),
                                       lname.begin(), ::tolower);
                        if (lname.find(stem) == std::string::npos) continue;
                        std::string bnk_leaf =
                            std::filesystem::path(e.bnk_path).filename().string();
                        OutputLog::info("  near-match: " + e.full_path +
                                        "  (in " + bnk_leaf + ")");
                        ++shown;
                    }
                    if (shown == 0) {
                        OutputLog::info("  no near-matches in " +
                                        std::to_string(S.all_mdl_files.size()) +
                                        "-entry global .mdl index");
                    }
                }
            }

            if (cached.geoms.empty()) {
                s->instances_loaded.fetch_add(block.instances.size(),
                                              std::memory_order_relaxed);
                continue;
            }
            if (S.cancel_requested.load()) {
                cancelled = true;
                break;
            }

            std::vector<MDLMeshGeom> combined(cached.geoms.size());
            std::vector<size_t> chunk_index(cached.geoms.size(), 0);
            for (size_t gi = 0; gi < cached.geoms.size(); ++gi) {
                const auto& src = cached.geoms[gi];
                init_combined_prop_geom(combined[gi], src, block.model_path,
                                        block.instances.size(), block.type, 0);
            }

            for (const auto& inst : block.instances) {
                if (S.cancel_requested.load()) {
                    cancelled = true;
                    break;
                }
                const uint32_t selection_id = next_selection_id++;
                if (next_selection_id == 0) next_selection_id = 1;
                for (size_t gi = 0; gi < cached.geoms.size(); ++gi) {
                    const auto& src = cached.geoms[gi];
                    if (!src.positions.empty() && !src.indices.empty()) {
                        if (would_exceed_combined_prop_limits(combined[gi],
                                                              src)) {
                            flush_combined_prop_geom(
                                s->geoms, combined[gi], src, block.model_path,
                                block.instances.size(), block.type,
                                chunk_index[gi]);
                        }
                        merge_transformed_instance_into(combined[gi], src, inst,
                                                        selection_id);
                    }
                }
                s->instances_loaded.fetch_add(1, std::memory_order_relaxed);
            }
            if (cancelled) break;

            for (auto& cg : combined) {
                if (!cg.positions.empty() && !cg.indices.empty()) {
                    s->geoms.push_back(std::move(cg));
                }
            }
            (void)terrain_cx; (void)terrain_cz;
        }

        std::unordered_map<std::string, CachedPropModel> entity_cache;
        for (const auto& batch : s->entity_batches) {
            if (cancelled || S.cancel_requested.load()) {
                cancelled = true;
                break;
            }
            std::ostringstream key_builder;
            for (uint32_t hash : batch.model_hashes) {
                key_builder << std::hex << hash << ';';
            }
            const std::string key = key_builder.str();
            auto& cached = entity_cache[key];
            if (!cached.loaded) {
                EntityModels::ResolvedModel resolved;
                std::string error;
                if (EntityModels::Resolve(batch.model_hashes, resolved,
                                          &error)) {
                    cached.info = std::move(resolved.info);
                    cached.geoms = std::move(resolved.meshes);
                    for (MDLMeshGeom& geom : cached.geoms) {




                        geom.bone_ids.clear();
                        geom.bone_weights.clear();
                        geom.is_cloth = false;
                        geom.cloth_sim = false;
                        geom.is_entity_model = true;
                    }
                } else {
                    ++s->model_misses;
                    OutputLog::warn(
                        "level entities: full model load miss for '" +
                        batch.label + "' (" + error + ")");
                }
                cached.loaded = true;
            }

            if (cached.geoms.empty()) {
                s->instances_loaded.fetch_add(batch.instances.size(),
                                              std::memory_order_relaxed);
                continue;
            }

            std::vector<MDLMeshGeom> combined(cached.geoms.size());
            std::vector<size_t> chunk_index(cached.geoms.size(), 0);
            const std::string model_label = batch.label.empty()
                ? std::string("entity") : batch.label;
            for (size_t gi = 0; gi < cached.geoms.size(); ++gi) {
                init_combined_prop_geom(combined[gi], cached.geoms[gi],
                                        model_label,
                                        batch.instances.size(), 0xE3, 0);
                combined[gi].is_entity_model = true;
            }

            for (const auto& entity_instance : batch.instances) {
                if (S.cancel_requested.load()) {
                    cancelled = true;
                    break;
                }
                for (size_t gi = 0; gi < cached.geoms.size(); ++gi) {
                    const auto& src = cached.geoms[gi];
                    if (src.positions.empty() || src.indices.empty()) continue;
                    if (would_exceed_combined_prop_limits(combined[gi], src)) {
                        flush_combined_prop_geom(
                            s->geoms, combined[gi], src, model_label,
                            batch.instances.size(), 0xE3,
                            chunk_index[gi]);
                        combined[gi].is_entity_model = true;
                    }
                    merge_transformed_instance_into(
                        combined[gi], src, entity_instance.instance,
                        entity_instance.selection_id);
                }
                s->instances_loaded.fetch_add(1,
                                              std::memory_order_relaxed);
            }
            if (cancelled) break;

            for (auto& geom : combined) {
                if (!geom.positions.empty() && !geom.indices.empty()) {
                    geom.is_entity_model = true;
                    s->geoms.push_back(std::move(geom));
                }
            }
        }

        s->phase.store(2, std::memory_order_release);
    } catch (const std::exception& e) {
        OutputLog::error("level props: prop bake worker aborted on " +
                         current_model + " (" + e.what() + ")");
        s->phase.store(2, std::memory_order_release);
    } catch (...) {
        OutputLog::error("level props: prop bake worker aborted on " +
                         current_model + " (unknown exception)");
        s->phase.store(2, std::memory_order_release);
    }
}
