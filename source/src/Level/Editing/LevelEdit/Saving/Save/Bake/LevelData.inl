    if (!s.additions.empty() || !gen_asset_models.empty() ||
        !level_placement_deletes.empty()) {
        const bool rewritable = s.lev.valid && !s.lev.compressed &&
                                s.lev.file_path.empty();
        if (rewritable) {
            try {
                bake_bytes = BnkCache::extract_bytes(s.lev.bnk_path,
                                                     s.lev.file_index);
            } catch (...) {
                bake_bytes.clear();
            }
            std::string berr;

            std::vector<Addition> lev_additions;
            lev_additions.reserve(s.additions.size());
            for (const auto& a : s.additions) {
                if (!a.as_entity()) lev_additions.push_back(a);
            }
            if (bake_bytes.empty()) {
                if (!level_placement_deletes.empty()) {
                    msg = "save failed: level re-extract failed while "
                          "removing render placements";
                    return false;
                }
                bake_note = "; bake skipped: level re-extract failed";
            } else if (!remove_level_placements(
                           bake_bytes, level_placement_deletes,
                           render_placements_deleted, berr)) {
                msg = "save failed: " + berr;
                return false;
            } else if (!append_additions_to_level(
                           bake_bytes, lev_additions, berr)) {
                if (!level_placement_deletes.empty()) {
                    msg = "save failed after removing render placements: " +
                          berr;
                    return false;
                }
                bake_note = "; bake skipped: " + berr;
                bake_bytes.clear();
            } else {
                need_bake = true;
                bake_iso = s.lev.in_iso;
                bake_bnk_path = s.lev.bnk_path;
                bake_index = s.lev.file_index;
                bake_count = s.additions.size();
                if (bake_iso) {
                    bake_vpath =
                        ISO::IsoMount::strip_iso_prefix(s.lev.bnk_path);
                }
                    std::vector<uint32_t> mdl_hashes;
                    for (const auto& a : s.additions) {
                        if (a.removed) continue;
                        if (!a.model_path.empty()) {
                            mdl_hashes.push_back(
                                fnv1_32(lower_model_path(a.model_path)));
                        }

                        if (a.physics_file_hash) {
                            mdl_hashes.push_back(a.physics_file_hash);
                        } else if (a.silver_keys_needed > 0) {



                            mdl_hashes.push_back(0x9FF26AA5u);
                        }
                    }
                    for (const auto& mp : gen_asset_models) {
                        mdl_hashes.push_back(
                            fnv1_32(lower_model_path(mp)));
                    }
                    try {
                        const auto bc = BnkCache::get(s.lev.bnk_path);
                        std::string ed_name =
                            bc.reader->list_files()
                                [(size_t)s.lev.file_index].name;
                        const std::string suffix = ".engine_level";
                        std::string low = ed_name;
                        for (char& c : low)
                            c = (char)std::tolower((unsigned char)c);
                        const size_t sp = low.rfind(suffix);
                        if (sp != std::string::npos &&
                            sp + suffix.size() == low.size()) {
                            std::string stem = low.substr(0, sp);
                            std::replace(stem.begin(), stem.end(), '\\',
                                         '/');
                            const std::string key =
                                stem + ".engine_data";
                            const int ed_idx = BnkCache::find_index(
                                s.lev.bnk_path, key);
                            if (ed_idx >= 0) {
                                bake_ed_bytes = BnkCache::extract_bytes(
                                    s.lev.bnk_path, ed_idx);
                                bool changed = false;
                                std::string perr;
                                if (patch_engine_resource_list(
                                        bake_ed_bytes, mdl_hashes,
                                        changed, perr)) {
                                    if (changed) {
                                        bake_ed_index = ed_idx;
                                    } else {
                                        bake_ed_bytes.clear();
                                    }
                                } else {
                                    bake_ed_bytes.clear();
                                }
                            } else {
                            }
                            const std::string lmp_key = stem + ".lmp";
                            const int lmp_idx = BnkCache::find_index(
                                s.lev.bnk_path, lmp_key);
                            if (lmp_idx >= 0) {
                                bake_lmp_bytes = BnkCache::extract_bytes(
                                    s.lev.bnk_path, lmp_idx);
                                bool changed = false;
                                std::string perr;
                                if (patch_lmp_probes(bake_lmp_bytes,
                                                     s.additions,
                                                     bake_bytes, changed,
                                                     perr)) {
                                    if (changed) {
                                        bake_lmp_index = lmp_idx;
                                    } else {
                                        bake_lmp_bytes.clear();
                                    }
                                } else {
                                    bake_lmp_bytes.clear();
                                }
                            } else {
                            }
