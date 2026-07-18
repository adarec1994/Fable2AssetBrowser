    if (!save_physics_patches.empty()) {
        if (save_rewrite_bytes.empty()) {
            const int save_idx = find_level_save_index(s.lev.bnk_path,
                                                       s.lev.file_index);
            if (save_idx >= 0) {
                try {
                    save_rewrite_bytes = BnkCache::extract_bytes(
                        s.lev.bnk_path, save_idx);
                } catch (...) {
                    save_rewrite_bytes.clear();
                }
                if (!save_rewrite_bytes.empty()) {
                    save_rewrite_index = save_idx;
                    save_rewrite_bnk = s.lev.bnk_path;
                }
            }
        }
        if (!save_rewrite_bytes.empty()) {
            save_physics_patched = apply_save_physics_patches(
                save_rewrite_bytes, save_physics_patches);
            if (save_physics_patched == 0 &&
                new_entities_created == 0 &&
                generators_created == 0 &&
                save_entities_deleted == 0) {
                save_rewrite_bytes.clear();
                save_rewrite_index = -1;
                save_rewrite_bnk.clear();
            }
        } else {
        }
    }
