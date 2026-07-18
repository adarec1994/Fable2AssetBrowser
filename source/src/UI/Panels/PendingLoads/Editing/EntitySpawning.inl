bool append_level_entity_model_at(ID3D11Device* device,
                                  const std::vector<uint32_t>& model_hashes,
                                  size_t marker_index,
                                  const float engine_pos[3])
{
    extern int g_selected_level_mesh_idx;
    extern uint32_t g_selected_level_pick_id;
    extern uint64_t g_selected_level_hash;

    if (!device || !g_mp.has_model || !g_mp.no_tilt ||
        model_hashes.empty()) {
        return false;
    }

    EntityModels::ResolvedModel resolved;
    std::string error;
    if (!EntityModels::Resolve(model_hashes, resolved, &error) ||
        resolved.meshes.empty()) {
        OutputLog::error("level edit: could not render entity (" +
                         error + ")");
        return false;
    }

    Level::PropInstance instance;
    instance.values[0] = engine_pos[0];
    instance.values[1] = engine_pos[1];
    instance.values[2] = engine_pos[2];
    instance.values[7] = 1.0f;
    instance.values[9] = instance.values[10] = instance.values[11] = 1.0f;
    const int addition_index = marker_index < g_level_spawn_markers.size()
        ? g_level_spawn_markers[marker_index].pending_addition_index : -1;
    instance.lev_rec_kind = addition_index >= 0 ? 5 : 3;
    instance.pos_file_offset = addition_index >= 0
        ? uint32_t(addition_index + 1) : 0;

    const uint32_t selection_id =
        0x70000000u | uint32_t(marker_index);
    std::vector<MDLMeshGeom> out;
    for (MDLMeshGeom& source : resolved.meshes) {
        if (source.positions.empty() || source.indices.empty()) continue;
        source.bone_ids.clear();
        source.bone_weights.clear();
        source.is_cloth = false;
        source.cloth_sim = false;
        source.is_entity_model = true;

        MDLMeshGeom combined;
        init_combined_prop_geom(combined, source, "entity", 1, 0xE3, 0);
        combined.is_entity_model = true;
        merge_transformed_instance_into(combined, source, instance,
                                        selection_id);
        if (!combined.positions.empty() && !combined.indices.empty()) {
            out.push_back(std::move(combined));
        }
    }
    if (out.empty()) return false;

    MDLInfo dummy_info;
    MP_Build(device, out, dummy_info, g_mp, true);
    g_selected_level_pick_id = selection_id;
    g_selected_level_hash = 0;
    g_selected_level_mesh_idx = -1;
    for (size_t mesh_index = g_mp.meshes.size(); mesh_index-- > 0;) {
        for (const auto& range : g_mp.meshes[mesh_index].pick_ranges) {
            if (range.selection_id != selection_id) continue;
            g_selected_level_mesh_idx = int(mesh_index);
            break;
        }
        if (g_selected_level_mesh_idx >= 0) break;
    }
    return g_selected_level_mesh_idx >= 0;
}
