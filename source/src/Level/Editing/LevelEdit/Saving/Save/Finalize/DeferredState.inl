    if (!write_additions(s, msg)) return false;

    msg = "saved " + std::to_string(lev_written) +
          " level-file patch(es), " + std::to_string(gdb_written) +
          " gdb component(s)";
    if (rs_written) {
        msg += ", " + std::to_string(rs_written) + " rotation patch(es)";
    }
    if (skipped || rs_visual) {
        msg += " (" + std::to_string(skipped + rs_visual) +
               " visual-only edit(s) not saved)";
    }
    msg += bake_note;
    deferred_work = need_bake || !gdb_rewrite_bytes.empty() ||
                    save_rewrite_index >= 0 || !babel_edits.empty();
    if (!deferred_work) {
        s.dirty = false;
        if (lev_written || gdb_written || rs_written) {
            BnkCache::invalidate(s.lev.bnk_path);
            if (!s.gdb.bnk_path.empty()) {
                BnkCache::invalidate(s.gdb.bnk_path);
            }
            reload_needed = true;
            reload_entry = s.entry;
            msg += "; reloading";
        }
    } else {
        s.saving = true;
    }
    }

    if (!deferred_work) {
        if (reload_needed) Level::OpenAsync(reload_entry);
        return true;
    }
