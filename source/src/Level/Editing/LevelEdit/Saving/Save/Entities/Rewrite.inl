    bool have_chest_adds = false;
    for (const auto& a : s.additions) {
        if (!a.removed && a.as_entity()) {
            have_chest_adds = true;
            break;
        }
    }
    if (!s.contents_edits.empty() || !s.contents_loot_edits.empty() ||
        have_chest_adds ||
        !gdb_entity_deletes.empty() ||
        !s.generators.empty() || !s.spawn_point_adds.empty() ||
        !s.spawn_point_deletes.empty() ||
        legacy_spawn_points_pending || legacy_generators_pending) {
        {
            char dbg[200];
            std::snprintf(
                dbg, sizeof(dbg),
                "gdb REWRITE triggered: contents=%zu loot=%zu "
                "chest_adds=%d ent_dels=%zu gens=%zu sp_adds=%zu "
                "sp_dels=%zu legacy_sp=%d legacy_gen=%d",
                s.contents_edits.size(), s.contents_loot_edits.size(),
                have_chest_adds ? 1 : 0, gdb_entity_deletes.size(),
                s.generators.size(), s.spawn_point_adds.size(),
                s.spawn_point_deletes.size(),
                legacy_spawn_points_pending ? 1 : 0,
                legacy_generators_pending ? 1 : 0);
            DebugLog::Write("save.rewrite", dbg);
        }
        progress_update(6, 100, "Rewriting entity data...");
        std::vector<uint8_t> gbytes;
        if (!s.gdb.file_path.empty()) {
            std::ifstream f(s.gdb.file_path, std::ios::binary);
            if (f) {
                f.seekg(0, std::ios::end);
                gbytes.resize(size_t(f.tellg()));
                f.seekg(0);
                f.read(reinterpret_cast<char*>(gbytes.data()),
                       std::streamsize(gbytes.size()));
                if (!f) gbytes.clear();
            }
        } else if (s.gdb.valid) {
            try {
                gbytes = BnkCache::extract_bytes(s.gdb.bnk_path,
                                                 s.gdb.file_index);
            } catch (...) {
                gbytes.clear();
            }
            if (!gbytes.empty() && !target_patchable_in_place(s.gdb)) {

                for (const auto& p : gdb_patches) {
                    if (size_t(p.off) + 4 <= gbytes.size()) {
                        put_f32_be(gbytes.data() + p.off, p.v);
                    }
                }
            }
        }
        if (gbytes.empty()) {
            msg = "save failed: .gdb source unavailable for chest "
                  "contents edits";
            return false;
        }
        GdbEdit::GdbFile g;
        std::string gerr;
        if (!g.Parse(gbytes, gerr)) {
            msg = "save failed: .gdb parse for contents edits: " + gerr;
            return false;
        }
        const std::unordered_set<uint32_t> requested_gdb_entity_deletes =
            gdb_entity_deletes;
        for (const auto& marker : g_level_spawn_markers) {
            if (marker.kind != 1 ||
                !requested_gdb_entity_deletes.count(marker.entity_hash)) {
                continue;
            }
            gdb_entity_deletes.insert(marker.spawn_point_entities.begin(),
                                      marker.spawn_point_entities.end());
        }
        if (legacy_spawn_points_pending) {
            std::string repair_err;
            if (!repair_legacy_spawn_points(
                    g, g_level_spawn_donor, spawn_points_repaired,
                    repair_err)) {
                msg = "save failed: " + repair_err;
                return false;
            }
        }
        if (legacy_generators_pending) {
            std::string repair_err;
            if (!repair_legacy_generators(
                    g, g_level_spawn_donor, generators_repaired,
                    repair_err)) {
                msg = "save failed: " + repair_err;
                return false;
            }
        }
        for (const auto& deletion : s.spawn_point_deletes) {
            std::string deletion_err;
            if (!remove_spawn_point_reference(
                    g, g_level_spawn_donor, deletion, deletion_err)) {
                msg = "save failed: " + deletion_err;
                return false;
            }
            ++spawn_points_deleted;
        }
        for (uint32_t entity_hash : gdb_entity_deletes) {
            if (!g.RemoveRecord(entity_hash)) {
                if (!requested_gdb_entity_deletes.count(entity_hash)) {
                    continue;
                }
                char hash_text[16];
                std::snprintf(hash_text, sizeof(hash_text), "0x%08X",
                              entity_hash);
                msg = std::string("save failed: entity ") + hash_text +
                      " is not in the editable level GDB";
                return false;
            }
            ++gdb_entities_deleted;
        }
        std::unordered_set<uint32_t> content_entities;
        for (const auto& kv : s.contents_edits) {
            content_entities.insert(kv.first);
        }
        for (const auto& kv : s.contents_loot_edits) {
            content_entities.insert(kv.first);
        }
        for (uint32_t entity_hash : content_entities) {
            std::vector<uint32_t> items;
            auto items_edit = s.contents_edits.find(entity_hash);
            if (items_edit != s.contents_edits.end()) {
                items = items_edit->second;
            } else {
                auto authored = g_level_entity_contents.find(entity_hash);
                if (authored != g_level_entity_contents.end()) {
                    for (const auto& item : authored->second.initial_items) {
                        items.push_back(item.record_hash);
                    }
                }
            }
            uint32_t loot_table = 0;
            const auto loot_edit =
                s.contents_loot_edits.find(entity_hash);
            const bool replace_loot =
                loot_edit != s.contents_loot_edits.end();
            if (replace_loot) loot_table = loot_edit->second;
            std::string aerr;
            if (apply_chest_contents(g, entity_hash, items, loot_table,
                                     replace_loot, aerr)) {
                ++contents_applied;
            } else {
            }
        }

        std::vector<std::pair<std::string, uint32_t>> new_save_entities;
        for (const auto& a : s.additions) {
            if (a.removed || !a.as_entity()) continue;
            std::string aerr;
            const uint32_t eh =
                create_entity_addition(g, a, babel_edits, aerr);
            if (!eh) {
                continue;
            }
            const char* name_fmt = a.is_dig_spot
                ? "F2AB_DigSpot_%08X" : "F2AB_Container_%08X";
            if (a.silver_keys_needed > 0) {
                name_fmt = "F2AB_SilverKeyChest_%08X";
            } else if (a.entity_kind == AdditionEntityKind::SilverKey) {
                name_fmt = "F2AB_Key_%08X";
            } else if (a.entity_kind == AdditionEntityKind::GenericProp) {
                name_fmt = "F2AB_Prop_%08X";
            } else if (a.entity_kind == AdditionEntityKind::Npc) {
                name_fmt = "F2AB_NPC_%08X";
            }
            char name[48];
            std::snprintf(name, sizeof(name), name_fmt, eh);
            const bool authored_named_entity =
                (a.entity_kind == AdditionEntityKind::Npc ||
                 a.entity_kind == AdditionEntityKind::GenericProp) &&
                !a.entity_name.empty();
            const std::string entity_name = authored_named_entity
                ? a.entity_name : std::string(name);
            g.AddNameMapping(entity_name, eh);
            new_save_entities.emplace_back(entity_name, eh);
            ++new_entities_created;
        }

        if (!s.generators.empty() || !s.spawn_point_adds.empty()) {
            const Gdb::SpawnDonorInfo& donor = g_level_spawn_donor;
            if (!donor.valid()) {
            } else {
                for (const auto& ga : s.generators) {
                    if (ga.removed || ga.creature_name.empty()) {
                        continue;
                    }
                    std::string gerr2;
                    if (create_generator_entity(g, donor, ga,
                                                new_save_entities,
                                                gerr2)) {
                        ++generators_created;
                    } else {
                    }
                }
                for (const auto& spa : s.spawn_point_adds) {
                    std::string serr2;
                    const uint32_t sp = create_spawn_point_entity(
                        g, donor, spa.pos, serr2);
                    if (!sp) {
                        continue;
                    }
                    char nm[32];
                    std::snprintf(nm, sizeof(nm), "F2AB_SP_%08X", sp);
                    std::string fname;
                    for (size_t n = 1; n < 10000; ++n) {
                        const std::string candidate =
                            "SpawnPoint" + std::to_string(n);
                        GdbEdit::Field existing;
                        if (!g.FindLocalField(
                                spa.spawn_points_record,
                                fnv1_32(candidate), existing)) {
                            fname = candidate;
                            break;
                        }
                    }
                    if (fname.empty()) {
                        continue;
                    }
                    if (!g.AddField(spa.spawn_points_record,
                                    fnv1_32(fname), 7, sp)) {
                    } else {
                        g.AddNameMapping(nm, sp);
                        new_save_entities.emplace_back(nm, sp);
                        ++generators_created;
                    }
                }
            }
        }
        if (!new_save_entities.empty() || !s.spawn_point_deletes.empty() ||
            !gdb_entity_deletes.empty()) {

            const int save_idx = find_level_save_index(s.lev.bnk_path,
                                                       s.lev.file_index);
            std::string serr;
            if (save_idx < 0) {
                serr = ".save entry not found";
            } else {
                try {
                    save_rewrite_bytes = BnkCache::extract_bytes(
                        s.lev.bnk_path, save_idx);
                } catch (...) {
                    save_rewrite_bytes.clear();
                }
                if (save_rewrite_bytes.empty()) {
                    serr = ".save extract failed";
                } else {
                    std::unordered_set<uint32_t> deleted_entities;
                    for (const auto& deletion : s.spawn_point_deletes) {
                        deleted_entities.insert(
                            deletion.spawn_point_entity);
                    }
                    deleted_entities.insert(gdb_entity_deletes.begin(),
                                            gdb_entity_deletes.end());
                    save_entities_deleted = remove_save_entities(
                        save_rewrite_bytes, deleted_entities);
                    if (append_save_entities(save_rewrite_bytes,
                                             new_save_entities,
                                             is_bwsslums_level(
                                                 s.lev.bnk_path,
                                                 s.lev.file_index),
                                             serr)) {
                        save_rewrite_index = save_idx;
                        save_rewrite_bnk = s.lev.bnk_path;
                    }
                }
            }
            if (save_rewrite_index < 0) {
                save_rewrite_bytes.clear();
                new_entities_created = 0;
                if (!s.spawn_point_deletes.empty()) {
                    msg = "save failed: could not update spawn point "
                          "registrations: " + serr;
                    return false;
                }
                if (!gdb_entity_deletes.empty()) {
                    msg = "save failed: could not update entity "
                          "registrations: " + serr;
                    return false;
                }
            }
        }

        if (contents_applied > 0 || new_entities_created > 0 ||
            generators_created > 0 || gdb_entities_deleted > 0 ||
            spawn_points_deleted > 0 ||
            spawn_points_repaired > 0 ||
            generators_repaired > 0) {
            gdb_rewrite_bytes = g.Serialize();
            if (!s.gdb.file_path.empty()) {
                gdb_rewrite_loose = s.gdb.file_path;
            } else {
                gdb_rewrite_bnk = s.gdb.bnk_path;
                gdb_rewrite_index = s.gdb.file_index;
                gdb_rewrite_iso = s.gdb.in_iso;
            }
        }
    }
