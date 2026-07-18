    EhfParsedBody ehf;
    const bool ehf_ok = ParseEhfBody(terrain_ehf, ehf);
    if (!ehf_ok && !terrain_ehf.empty()) {
        OutputLog::warn("level export: terrain material parse failed: " +
                        ehf.error);
    }

    progress_update(84, 100, "Exporting terrain...");
    const std::string terrain_file =
        std::string("terrain/terrain") +
        ((format == ExportFormat::GLB) ? ".glb" : ".fbx");
    const auto terrain_path = out_dir / terrain_file;
    std::string terrain_err;
    const bool terrain_ok =
        (format == ExportFormat::GLB)
            ? write_terrain_glb(terrain_mesh, terrain_path, terrain_err)
            : write_terrain_fbx(terrain_mesh, terrain_path, terrain_err);
    if (!terrain_ok) {
        OutputLog::warn("level export: terrain mesh failed: " + terrain_err);
    }

    std::unordered_map<std::string, std::string> terrain_tex_cache;
    std::vector<std::array<std::string, 6>> lod_texture_files;
    std::vector<std::string> weight_files;
    int weight_w = 0;
    int weight_h = 0;
    if (ehf_ok) {
        lod_texture_files.resize(ehf.lods.size());
        for (size_t li = 0; li < ehf.lods.size(); ++li) {
            for (int si = 0; si < 6; ++si) {
                std::string rel;
                if (export_one_terrain_texture(
                        ehf.lods[li].strs[si],
                        terrain_entry.bnk_path.empty()
                            ? model_body_bnk
                            : terrain_entry.bnk_path,
                        terrain_texture_dir, "terrain/textures",
                        terrain_tex_cache, rel)) {
                    lod_texture_files[li][si] = rel;
                }
            }
        }

        TerrainWeightExport weights;
        const auto weight_dir = terrain_dir / "weights";
        std::filesystem::create_directories(weight_dir, ec);
        const int expected_weights =
            std::min<int>(std::max<int>(1, int(ehf.lods.size())), 32);
        weight_w = int(ehf.chunk_w) * 32 + 1;
        weight_h = int(ehf.chunk_h) * 32 + 1;
        weight_files.resize(size_t(expected_weights));
        bool weights_reused = expected_weights > 0;
        for (int wi = 0; wi < expected_weights; ++wi) {
            const std::string leaf =
                "weight_" + (wi < 10 ? std::string("0") : std::string()) +
                std::to_string(wi) + ".png";
            const auto path = weight_dir / leaf;
            weight_files[size_t(wi)] = std::string("terrain/weights/") + leaf;
            if (!file_exists_nonempty(path)) {
                weights_reused = false;
            }
        }
        if (!weights_reused && build_terrain_weight_maps(ehf, weights)) {
            weight_w = weights.width;
            weight_h = weights.height;
            weight_files.resize(size_t(weights.material_count));
            for (int wi = 0; wi < weights.material_count; ++wi) {
                const std::string leaf =
                    "weight_" + (wi < 10 ? std::string("0") : std::string()) +
                    std::to_string(wi) + ".png";
                const auto path = weight_dir / leaf;
                if (tex_export_png(path.string(),
                                   weights.rgba[size_t(wi)].data(),
                                   weights.width,
                                   weights.height)) {
                    weight_files[size_t(wi)] =
                        std::string("terrain/weights/") + leaf;
                }
            }
        } else if (!weights_reused) {
            OutputLog::warn("level export: terrain weight maps unavailable");
        }
    }

    std::string splat_rel;
    if (ehf_ok && !ehf.splat_indices.empty()) {
        splat_rel = "terrain/splat_indices.bin";
        std::string err;
        const auto splat_path = out_dir / splat_rel;
        if (!file_exists_nonempty(splat_path) &&
            !write_bytes(splat_path, ehf.splat_indices, err)) {
            OutputLog::warn("level export: splat write failed: " + err);
            splat_rel.clear();
        }
    }
