#ifdef _WIN32

bool delete_selected_level_object()
{
    extern int g_selected_level_mesh_idx;
    extern uint32_t g_selected_level_pick_id;
    extern uint64_t g_selected_level_hash;

    if (!LevelEdit::Enabled() || LevelEdit::Saving()) return false;

    const uint64_t hash = g_selected_level_hash;
    const uint32_t pick = g_selected_level_pick_id;
    const int mesh_idx = g_selected_level_mesh_idx;
    constexpr uint64_t kAddBase = 0xADD0000000000000ull;

    bool did = false;
    if (hash >= kAddBase && hash < kAddBase + 0x10000000ull) {
        const int add_idx = (int)(hash - kAddBase);
        LevelEdit::RemoveAddition(add_idx);
        MP_RemoveMeshesByInstHash(g_mp, hash);
        did = true;
    } else if (pick != 0 || hash != 0) {
        std::vector<uint32_t> group_ids;
        std::unordered_set<uint32_t> seen;
        for (const auto& m2 : g_mp.meshes) {
            for (const auto& pr : m2.pick_ranges) {
                const bool in_group =
                    (pick != 0 && pr.selection_id == pick) ||
                    (hash != 0 && pr.inst_hash == hash);
                if (!in_group) continue;
                if (seen.insert(pr.selection_id).second) {
                    group_ids.push_back(pr.selection_id);
                }
            }
        }
        if (!group_ids.empty()) {
            LevelEdit::PushUndoSnapshot(group_ids);
        }
        seen.clear();
        for (const auto& m2 : g_mp.meshes) {
            for (const auto& pr : m2.pick_ranges) {
                const bool in_group =
                    (pick != 0 && pr.selection_id == pick) ||
                    (hash != 0 && pr.inst_hash == hash);
                if (!in_group) continue;
                if (!seen.insert(pr.selection_id).second) continue;
                LevelEdit::InstInfo info;
                info.orig_pos = pr.inst_pos;
                info.orig_rot_deg[0] = pr.inst_rot_deg[0];
                info.orig_rot_deg[1] = pr.inst_rot_deg[1];
                info.orig_rot_deg[2] = pr.inst_rot_deg[2];
                info.lev_off = pr.pos_file_offset;
                info.lev_kind = pr.lev_rec_kind;
                info.gdb_off = pr.gdb_pos_off;
                info.gdb_rot_off = pr.gdb_rot_off;
                info.gdb_entity_hash = pr.gdb_entity_hash;
                LevelEdit::SetDeleted(pr.selection_id, info);
                did = true;
            }
        }
    } else if (mesh_idx >= 0 && mesh_idx < (int)g_mp.meshes.size()) {
        const auto& m = g_mp.meshes[(size_t)mesh_idx];
        const float orig[3] = {m.center[0], m.center[2], m.center[1]};
        LevelEdit::InstInfo info;
        info.orig_pos = orig;
        LevelEdit::SetDeleted(0x80000000u | (uint32_t)mesh_idx, info);
        did = true;
    }

    if (did) {
        g_selected_level_mesh_idx = -1;
        g_selected_level_pick_id = 0;
        g_selected_level_hash = 0;
        OutputLog::info("level edit: selection deleted (Save bakes it)");
    }
    return did;
}

#endif
