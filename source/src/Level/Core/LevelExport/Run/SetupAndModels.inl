bool run_export(const FlatAssetEntry& entry, ExportFormat format)
{
    const char* ext = (format == ExportFormat::GLB) ? ".glb" : ".fbx";
    const char* fmt_label = (format == ExportFormat::GLB) ? "GLB" : "FBX";

    const std::filesystem::path root =
        S.export_dir.empty() ? std::filesystem::path("extracted")
                             : std::filesystem::path(S.export_dir);
    const std::string folder_name = level_folder_name(entry);
    const auto out_dir = root / folder_name;
    const auto model_dir = out_dir / "models";
    const auto terrain_dir = out_dir / "terrain";
    const auto terrain_texture_dir = terrain_dir / "textures";

    std::error_code ec;
    std::filesystem::create_directories(model_dir, ec);
    if (ec) {
        OutputLog::error("level export: cannot create " + model_dir.string() +
                         " (" + ec.message() + ")");
        return false;
    }
    std::filesystem::create_directories(terrain_texture_dir, ec);
    if (ec) {
        OutputLog::error("level export: cannot create " +
                         terrain_texture_dir.string() + " (" +
                         ec.message() + ")");
        return false;
    }

    ExportOnlyLevelLoadGuard export_load_guard;

    progress_update(3, 100, "Preparing level export...");
    if (!Open(entry) || S.cancel_requested.load() || S.exiting.load()) {
        return false;
    }

    progress_update(18, 100, "Collecting export data...");

    const TerrainMesh terrain_mesh = g_pending_terrain_mesh;
    const std::vector<uint8_t> terrain_ehf = g_pending_terrain_ehf_bytes;
    const std::vector<PropBlock> prop_blocks = g_pending_level_prop_blocks;
    const std::string model_body_bnk = g_pending_level_model_body_bnk;
    const FlatAssetEntry terrain_entry = g_pending_terrain_level_entry;
    const int ghf_w = g_pending_terrain_ghf_width;
    const int ghf_h = g_pending_terrain_ghf_height;
    const float ghf_tile = g_pending_terrain_ghf_tile_size;

    g_pending_terrain_load.store(false);

    std::map<std::string, size_t> instance_counts;
    for (const auto& block : prop_blocks) {
        if (block.model_path.empty()) continue;
        instance_counts[block.model_path] += block.instances.size();
    }

    std::unordered_map<std::string, std::string> model_files;
    int exported_models = 0;
    int reused_models = 0;
    int failed_models = 0;
    int model_index = 0;
    const int model_total =
        std::max(1, static_cast<int>(instance_counts.size()));

    for (const auto& kv : instance_counts) {
        if (S.cancel_requested.load() || S.exiting.load()) return false;
        const std::string& model_path = kv.first;
        const std::string out_leaf = model_output_name(model_path, ext);
        const auto out_path = model_dir / out_leaf;
        std::vector<unsigned char> mdl_buf;
        progress_update(70 + (model_index * 12) / model_total, 100,
                        "Exporting model " + std::to_string(model_index + 1) +
                        "/" + std::to_string(model_total));
        if (file_exists_nonempty(out_path)) {
            model_files[model_path] = std::string("models/") + out_leaf;
            ++reused_models;
            ++model_index;
            continue;
        }
        if (!build_mdl_buffer_for_name_with_body(model_path, model_body_bnk,
                                                 mdl_buf)) {
            ++failed_models;
            ++model_index;
            continue;
        }

        std::string err;
        bool ok = false;
        try {
            if (format == ExportFormat::GLB) {
                ok = mdl_to_glb_full(mdl_buf, out_path.string(),
                                     model_path, err);
            } else {
                ok = mdl_to_fbx_full(mdl_buf, out_path.string(),
                                     model_path, err, false);
            }
        } catch (const std::exception& ex) {
            err = ex.what();
        } catch (...) {
            err = "unknown exporter exception";
        }
        if (ok) {
            model_files[model_path] = std::string("models/") + out_leaf;
            ++exported_models;
        } else {
            ++failed_models;
            if (!err.empty()) {
                OutputLog::warn("level export: model failed: " + model_path +
                                " (" + err + ")");
            }
        }
        ++model_index;
    }

    if (S.cancel_requested.load() || S.exiting.load()) return false;
