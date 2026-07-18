    {
        std::lock_guard<std::mutex> lk(mtx());
        auto& s = st();
        s.saving = false;
        if (rebuilt && contents_ok) {
            if (text_written > 0) s.text_edits.clear();
            if (need_bake) {
                s.additions.clear();
                s.edits.clear();
                s.undo_stack.clear();
                {
                    std::error_code ec;
                    std::filesystem::remove(additions_path(), ec);
                }
            }
            if (contents_applied > 0) {
                s.contents_edits.clear();
                s.contents_loot_edits.clear();
            }
            if (gdb_entities_deleted > 0) {
                s.edits.clear();
                s.undo_stack.clear();
            }
            if (generators_created > 0) {
                s.generators.clear();
                s.spawn_point_adds.clear();
            }
            if (spawn_points_deleted > 0) {
                s.spawn_point_deletes.clear();
            }
            BnkCache::invalidate(s.lev.bnk_path);
            if (!s.gdb.bnk_path.empty()) {
                BnkCache::invalidate(s.gdb.bnk_path);
            }
            fill_bnk_target(s.lev);
            if (!s.gdb.bnk_path.empty()) fill_bnk_target(s.gdb);
            s.dirty = false;
            reload_needed = true;
            reload_entry = s.entry;
            if (need_bake && bake_count > 0) {
                msg += "; baked " + std::to_string(bake_count) +
                       " model(s) into the level BNK";
            }
            if (render_placements_deleted > 0) {
                msg += "; removed " +
                       std::to_string(render_placements_deleted) +
                       " render placement(s)";
            }
            if (contents_applied > 0) {
                msg += "; rewrote contents of " +
                       std::to_string(contents_applied) + " container(s)";
            }
            if (new_entities_created > 0) {
                msg += "; created " +
                       std::to_string(new_entities_created) +
                       " level entit(ies)";
            }
            if (save_physics_patched > 0) {
                msg += "; updated " +
                       std::to_string(save_physics_patched) +
                       " entity save transform(s)";
            }
            if (generators_created > 0) {
                msg += "; authored " +
                       std::to_string(generators_created) +
                       " generator/spawn point(s)";
            }
            if (gdb_entities_deleted > 0) {
                msg += "; deleted " +
                       std::to_string(gdb_entities_deleted) +
                       " GDB entit(ies)";
            }
            if (spawn_points_deleted > 0) {
                msg += "; deleted " +
                       std::to_string(spawn_points_deleted) +
                       " spawn point(s)";
            }
            if (spawn_points_repaired > 0) {
                msg += "; repaired " +
                       std::to_string(spawn_points_repaired) +
                       " legacy spawn point(s)";
            }
            if (generators_repaired > 0) {
                msg += "; repaired " +
                       std::to_string(generators_repaired) +
                       " legacy generator(s)";
            }
            if (text_written > 0) {
                msg += "; wrote " + std::to_string(text_written) +
                       " text entr(ies)";
            }
            msg += "; reloading";
        } else if (!rebuilt) {
            msg += "; BAKE FAILED: " + berr +
                   " (placements kept app-side)";
        } else {
            msg += "; CONTENTS REWRITE FAILED (edits kept app-side)";
        }
    }
    if (reload_needed) Level::OpenAsync(reload_entry);
    return rebuilt && contents_ok;
