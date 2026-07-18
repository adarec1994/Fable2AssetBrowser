    if (g_mp.no_tilt && ::g_selected_level_mesh_idx >= 0 &&
        ::g_selected_level_mesh_idx < (int)g_mp.meshes.size())
    {
        const auto& sel_mesh =
            g_mp.meshes[(size_t)::g_selected_level_mesh_idx];
        float sel_pos[3] = {0.0f, 0.0f, 0.0f};
        float sel_rot[3] = {0.0f, 0.0f, 0.0f};
        bool  sel_has_rot = false;
        bool  sel_found = false;
        uint32_t sel_gdb_entity_hash = 0;
        if (::g_selected_level_pick_id != 0) {
            for (const auto& pr : sel_mesh.pick_ranges) {
                if (pr.selection_id != ::g_selected_level_pick_id) continue;
                sel_gdb_entity_hash = pr.gdb_entity_hash;
                if (pr.has_transform) {
                    sel_pos[0] = pr.inst_pos[0];
                    sel_pos[1] = pr.inst_pos[1];
                    sel_pos[2] = pr.inst_pos[2];
                    sel_rot[0] = pr.inst_rot_deg[0];
                    sel_rot[1] = pr.inst_rot_deg[1];
                    sel_rot[2] = pr.inst_rot_deg[2];
                    sel_has_rot = true;
                } else {
                    sel_pos[0] = pr.center[0];
                    sel_pos[1] = pr.center[2];
                    sel_pos[2] = pr.center[1];
                }
                sel_found = true;
                break;
            }
        }
        if (!sel_found) {
            sel_pos[0] = sel_mesh.center[0];
            sel_pos[1] = sel_mesh.center[2];
            sel_pos[2] = sel_mesh.center[1];
        }
        const bool whole_mesh_sel = (::g_selected_level_pick_id == 0);
        const uint32_t edit_key = whole_mesh_sel
            ? (0x80000000u | (uint32_t)::g_selected_level_mesh_idx)
            : ::g_selected_level_pick_id;
        {
            float d_pos[3], d_rot[3];
            if (LevelEdit::EditFor(edit_key, d_pos, d_rot)) {
                sel_pos[0] += d_pos[0];
                sel_pos[1] += d_pos[1];
                sel_pos[2] += d_pos[2];
                sel_rot[0] += d_rot[0];
                sel_rot[1] += d_rot[1];
                sel_rot[2] += d_rot[2];
            }
        }

        auto range_in_group = [](const MPPerMesh::PickRange& pr) {
            if (pr.selection_id == ::g_selected_level_pick_id) return true;
            return ::g_selected_level_hash != 0 &&
                   pr.inst_hash == ::g_selected_level_hash;
        };
        auto collect_group_ids = [&]() {
            std::vector<uint32_t> ids;
            if (whole_mesh_sel) {
                ids.push_back(edit_key);
                return ids;
            }
            std::unordered_set<uint32_t> seen;
            for (const auto& m2 : g_mp.meshes) {
                for (const auto& pr : m2.pick_ranges) {
                    if (!range_in_group(pr)) continue;
                    if (seen.insert(pr.selection_id).second) {
                        ids.push_back(pr.selection_id);
                    }
                }
            }
            return ids;
        };
        enum { kEditMove, kEditRotate, kEditDelete };
        auto apply_group_edit = [&](int what, const float v[3]) {
            if (whole_mesh_sel) {
                const float orig[3] = { sel_mesh.center[0],
                                        sel_mesh.center[2],
                                        sel_mesh.center[1] };
                LevelEdit::InstInfo info;
                info.orig_pos = orig;
                if (what == kEditMove) {
                    LevelEdit::AddMove(edit_key, v, info);
                } else if (what == kEditRotate) {
                    LevelEdit::AddRotate(edit_key, v, info);
                } else {
                    LevelEdit::SetDeleted(edit_key, info);
                }
                return;
            }
            std::unordered_set<uint32_t> done;
            for (const auto& m2 : g_mp.meshes) {
                for (const auto& pr : m2.pick_ranges) {
                    if (!range_in_group(pr)) continue;
                    if (!done.insert(pr.selection_id).second) continue;
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
                    if (what == kEditMove) {
                        LevelEdit::AddMove(pr.selection_id, v, info);
                    } else if (what == kEditRotate) {
                        LevelEdit::AddRotate(pr.selection_id, v, info);
                    } else {
                        LevelEdit::SetDeleted(pr.selection_id, info);
                    }
                }
            }
        };
        const bool sel_finite = std::isfinite(sel_pos[0]) &&
                                std::isfinite(sel_pos[1]) &&
                                std::isfinite(sel_pos[2]);
        const bool edit_active = LevelEdit::Enabled() &&
                                 !LevelEdit::Saving() &&
                                 (whole_mesh_sel || sel_found) &&
                                 sel_finite;

        static int      s_dbg_idx = -2;
        static uint32_t s_dbg_id  = 0xFFFFFFFFu;
        const bool dbg_sel_changed =
            s_dbg_idx != ::g_selected_level_mesh_idx ||
            s_dbg_id  != ::g_selected_level_pick_id;
        if (dbg_sel_changed) {
            s_dbg_idx = ::g_selected_level_mesh_idx;
            s_dbg_id  = ::g_selected_level_pick_id;
            DebugTrace::log(
                "sel: idx=%d id=%u hash=%llu ranges=%zu found=%d whole=%d "
                "finite=%d pos=(%.2f,%.2f,%.2f) edit_active=%d",
                ::g_selected_level_mesh_idx, ::g_selected_level_pick_id,
                (unsigned long long)::g_selected_level_hash,
                sel_mesh.pick_ranges.size(), sel_found ? 1 : 0,
                whole_mesh_sel ? 1 : 0, sel_finite ? 1 : 0,
                sel_pos[0], sel_pos[1], sel_pos[2], edit_active ? 1 : 0);
        }
