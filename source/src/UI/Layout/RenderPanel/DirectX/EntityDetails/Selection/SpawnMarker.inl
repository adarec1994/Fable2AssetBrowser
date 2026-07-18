static int selected_level_spawn_marker_index()
{
    if (g_sel_spawn_marker >= 0 &&
        g_sel_spawn_marker < int(g_level_spawn_markers.size())) {
        return g_sel_spawn_marker;
    }
    if ((::g_selected_level_pick_id & 0xF0000000u) == 0x70000000u) {
        const size_t marker_index =
            size_t(::g_selected_level_pick_id & 0x0FFFFFFFu);
        if (marker_index < g_level_spawn_markers.size()) {
            return int(marker_index);
        }
    }
    if (::g_selected_level_mesh_idx < 0 ||
        ::g_selected_level_mesh_idx >= int(g_mp.meshes.size()) ||
        ::g_selected_level_pick_id == 0) {
        return -1;
    }
    const MPPerMesh& mesh =
        g_mp.meshes[size_t(::g_selected_level_mesh_idx)];
    uint32_t entity_hash = 0;
    for (const auto& range : mesh.pick_ranges) {
        if (range.selection_id != ::g_selected_level_pick_id) continue;
        entity_hash = range.gdb_entity_hash;
        break;
    }
    if (entity_hash == 0) return -1;
    for (size_t marker_index = 0;
         marker_index < g_level_spawn_markers.size(); ++marker_index) {
        if (g_level_spawn_markers[marker_index].entity_hash ==
            entity_hash) {
            return int(marker_index);
        }
    }
    return -1;
}
