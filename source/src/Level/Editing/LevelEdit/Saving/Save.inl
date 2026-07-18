bool Save(std::string& msg) {
    if (!GameBackup::RequireBackup(msg)) return false;
    {
        FlatAssetEntry entry_copy;
        {
            std::lock_guard<std::mutex> lk(mtx());
            entry_copy = st().entry;
        }
        
        
        
        if (Level::Creation::IsCustomLooseLevel(entry_copy)) {
            return SaveWorkingCopy(msg);
        }
    }
    bool reload_needed = false;
    FlatAssetEntry reload_entry;
    bool need_bake = false;
    bool bake_iso = false;
    std::string bake_bnk_path;
    std::string bake_vpath;
    int bake_index = -1;
    size_t bake_count = 0;
    size_t render_placements_deleted = 0;
    std::vector<uint8_t> bake_bytes;
    int bake_ed_index = -1;
    std::vector<uint8_t> bake_ed_bytes;
    int bake_lmp_index = -1;
    std::vector<uint8_t> bake_lmp_bytes;
    int bake_lvstream_index = -1;
    std::vector<uint8_t> bake_lvstream_bytes;
    std::string bake_streaming_path;
    int bake_models_index = -1;
    std::vector<uint8_t> bake_models_bytes;
    std::vector<BnkWriter::EntryReplacement> bake_more;

    std::vector<uint8_t> gdb_rewrite_bytes;
    std::string gdb_rewrite_bnk;
    int gdb_rewrite_index = -1;
    std::string gdb_rewrite_loose;
    bool gdb_rewrite_iso = false;
    size_t contents_applied = 0;

    std::vector<uint8_t> save_rewrite_bytes;
    int save_rewrite_index = -1;
    std::string save_rewrite_bnk;
    size_t new_entities_created = 0;
    size_t generators_created = 0;
    size_t gdb_entities_deleted = 0;
    size_t spawn_points_deleted = 0;
    size_t save_entities_deleted = 0;
    size_t spawn_points_repaired = 0;
    size_t generators_repaired = 0;
    size_t save_physics_patched = 0;
    std::unordered_map<uint32_t, std::string> babel_edits;
    bool deferred_work = false;
    {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (!s.available) { msg = "no level loaded"; return false; }
    if (s.saving) { msg = "a save is already in progress"; return false; }

    DebugTrace::log("save: lev bnk='%s' idx=%d iso=%d comp=%d valid=%d "
                    "gdb bnk='%s' additions=%zu edits=%zu",
                    s.lev.bnk_path.c_str(), s.lev.file_index,
                    s.lev.in_iso ? 1 : 0, s.lev.compressed ? 1 : 0,
                    s.lev.valid ? 1 : 0, s.gdb.bnk_path.c_str(),
                    s.additions.size(), s.edits.size());

    progress_update(2, 100, "Writing level patches...");

    size_t lev_written = 0, gdb_written = 0, rs_written = 0;
    size_t skipped = 0, rs_visual = 0;
    struct LevPatch { uint32_t off; float v[3]; int n; };
    std::vector<LevPatch> lev_patches;
    struct GdbPatch { uint32_t off; float v; };
    std::vector<GdbPatch> gdb_patches;
    std::vector<SavePhysPatch> save_physics_patches;
    std::vector<LevelPlacementDelete> level_placement_deletes;
    std::unordered_set<uint32_t> gdb_entity_deletes;

    size_t adds_updated = 0;
    for (const auto& kv : s.edits) {
        const EditEntry& e = kv.second;
        if (!e.changed()) continue;
        if (!e.deleted && e.gdb_entity_hash != 0 && e.lev_kind != 5) {
            SavePhysPatch sp;
            sp.hash = e.gdb_entity_hash;
            sp.pos[0] = e.orig[0] + e.delta[0];
            sp.pos[1] = e.orig[1] + e.delta[1];
            sp.pos[2] = e.orig[2] + e.delta[2];
            sp.rot_deg[0] = e.orig_rot[0] + e.rot_deg[0];
            sp.rot_deg[1] = e.orig_rot[1] + e.rot_deg[1];
            sp.rot_deg[2] = e.orig_rot[2] + e.rot_deg[2];
            sp.set_rot = e.rotated() && !e.deleted;
            save_physics_patches.push_back(sp);
        }
        if (e.lev_kind == 5) {
            if (e.lev_off >= 1 && e.lev_off <= s.additions.size()) {
                Addition& a = s.additions[e.lev_off - 1];
                if (e.deleted) {
                    a.removed = true;
                } else {
                    a.pos[0] = e.orig[0] + e.delta[0];
                    a.pos[1] = e.orig[1] + e.delta[1];
                    a.pos[2] = e.orig[2] + e.delta[2];
                    a.yaw_deg = e.orig_rot[2] + e.rot_deg[2];
                }
                ++adds_updated;
            }
            continue;
        }
        if (e.deleted) {
            if (e.lev_off != 0 && e.lev_kind >= 1 && e.lev_kind <= 4) {
                level_placement_deletes.push_back(
                    {e.lev_off, e.lev_kind});
            } else if (e.gdb_entity_hash != 0) {
                gdb_entity_deletes.insert(e.gdb_entity_hash);
            } else {
                ++skipped;
            }
            continue;
        }
        if (e.moved()) {
            const float np[3] = { e.orig[0] + e.delta[0],
                                  e.orig[1] + e.delta[1],
                                  e.orig[2] + e.delta[2] };

            bool wrote = false;
            if (e.lev_off != 0) {
                lev_patches.push_back({ e.lev_off,
                                        { np[0], np[1], np[2] }, 3 });
                wrote = true;
            }
            if (e.gdb_off[0] || e.gdb_off[1] || e.gdb_off[2]) {
                for (int i = 0; i < 3; ++i) {
                    if (e.gdb_off[i]) {
                        gdb_patches.push_back({ e.gdb_off[i], np[i] });
                    }
                }
                wrote = true;
            }
            if (!wrote) {
                ++skipped;
            }
        }
        if (e.rotated() && !e.deleted) {
            bool wrote_rot = false;
            if (e.lev_kind == 1 && e.lev_off != 0) {
                const float yaw =
                    (e.orig_rot[2] + e.rot_deg[2]) * kDegToRad;
                lev_patches.push_back({ e.lev_off + 24,
                                        { std::sin(yaw), std::cos(yaw),
                                          0 }, 2 });
                wrote_rot = true;
                if (e.rot_deg[0] != 0.0f || e.rot_deg[1] != 0.0f) {
                    ++rs_visual;
                }
            }
            if (e.gdb_rot_off[0] && e.gdb_rot_off[1] &&
                e.gdb_rot_off[2]) {
                gdb_patches.push_back({ e.gdb_rot_off[0],
                    (e.orig_rot[2] + e.rot_deg[2]) * kDegToRad });
                gdb_patches.push_back({ e.gdb_rot_off[1],
                    (e.orig_rot[1] + e.rot_deg[1]) * kDegToRad });
                gdb_patches.push_back({ e.gdb_rot_off[2],
                    (e.orig_rot[0] + e.rot_deg[0]) * kDegToRad });
                wrote_rot = true;
            }
            if (wrote_rot) {
                ++rs_written;
            } else {
                ++rs_visual;
            }
        }
    }
    babel_edits = s.text_edits;
    bool legacy_spawn_points_pending = false;
    bool legacy_generators_pending = false;
    if (g_level_spawn_donor.valid()) {
        std::vector<uint8_t> probe_bytes;
        if (!s.gdb.file_path.empty()) {
            std::ifstream f(s.gdb.file_path, std::ios::binary);
            if (f) {
                f.seekg(0, std::ios::end);
                probe_bytes.resize(size_t(f.tellg()));
                f.seekg(0);
                f.read(reinterpret_cast<char*>(probe_bytes.data()),
                       std::streamsize(probe_bytes.size()));
                if (!f) probe_bytes.clear();
            }
        } else if (s.gdb.valid) {
            try {
                probe_bytes = BnkCache::extract_bytes(
                    s.gdb.bnk_path, s.gdb.file_index);
            } catch (...) {
                probe_bytes.clear();
            }
        }
        if (!probe_bytes.empty()) {
            GdbEdit::GdbFile probe;
            std::string probe_err;
            if (probe.Parse(probe_bytes, probe_err)) {
                legacy_spawn_points_pending = has_legacy_spawn_points(
                    probe, g_level_spawn_donor);
                legacy_generators_pending = has_legacy_generators(
                    probe, g_level_spawn_donor);
            }
        }
    }
    if (lev_patches.empty() && level_placement_deletes.empty() &&
        gdb_patches.empty() && adds_updated == 0 &&
        s.additions.empty() && s.contents_edits.empty() &&
        s.contents_loot_edits.empty() &&
        save_physics_patches.empty() && babel_edits.empty() &&
        gdb_entity_deletes.empty() &&
        s.generators.empty() && s.spawn_point_adds.empty() &&
        s.spawn_point_deletes.empty() &&
        !legacy_spawn_points_pending && !legacy_generators_pending) {
        msg = (skipped || rs_visual)
                  ? "no file-backed changes to save (visual-only edits "
                    "skipped)"
                  : "no changes to save";
        return true;
    }

    std::string err;
    if (!lev_patches.empty()) {
        if (target_patchable_in_place(s.lev)) {
            for (const auto& p : lev_patches) {
                if ((uint64_t)p.off + (uint64_t)p.n * 4 >
                    s.lev.on_disk_size) continue;
                if (!patch_target(s.lev, p.off, p.v, p.n, err)) {
                    msg = "save failed (level file): " + err;
                    return false;
                }
                ++lev_written;
            }
            BnkCache::invalidate(s.lev.bnk_path);
        } else {
            std::vector<uint8_t> bytes;
            try {
                bytes = BnkCache::extract_bytes(s.lev.bnk_path,
                                                s.lev.file_index);
            } catch (...) { bytes.clear(); }
            if (bytes.empty()) {
                msg = "level re-extract failed";
                return false;
            }
            for (const auto& p : lev_patches) {
                if ((size_t)p.off + (size_t)p.n * 4 > bytes.size())
                    continue;
                for (int i = 0; i < p.n; ++i) {
                    put_f32_be(bytes.data() + p.off + i * 4, p.v[i]);
                }
                ++lev_written;
            }
            const auto out = edited_levels_dir() /
                std::filesystem::path(s.entry.full_path).filename();
            std::error_code ec;
            std::filesystem::create_directories(out.parent_path(), ec);
            std::ofstream f(out, std::ios::binary);
            if (!f) { msg = "could not write " + out.string(); return false; }
            f.write(reinterpret_cast<const char*>(bytes.data()),
                    (std::streamsize)bytes.size());
            OutputLog::warn("level edit: chunked level entry - patched "
                            "copy exported to " + out.string());
        }
    }
    if (!gdb_patches.empty()) {
        if (target_patchable_in_place(s.gdb)) {
            for (const auto& p : gdb_patches) {
                if (s.gdb.on_disk_size &&
                    (uint64_t)p.off + 4 > s.gdb.on_disk_size) continue;
                if (!patch_target(s.gdb, p.off, &p.v, 1, err)) {
                    msg = "save failed (.gdb): " + err;
                    return false;
                }
                ++gdb_written;
            }
            if (!s.gdb.bnk_path.empty()) {
                BnkCache::invalidate(s.gdb.bnk_path);
            }
        } else if (s.gdb.valid) {
            std::vector<uint8_t> bytes;
            try {
                bytes = BnkCache::extract_bytes(s.gdb.bnk_path,
                                                s.gdb.file_index);
            } catch (...) { bytes.clear(); }
            if (!bytes.empty()) {
                for (const auto& p : gdb_patches) {
                    if ((size_t)p.off + 4 > bytes.size()) continue;
                    put_f32_be(bytes.data() + p.off, p.v);
                    ++gdb_written;
                }
                const auto out = edited_levels_dir() /
                    (std::filesystem::path(s.entry.full_path)
                         .stem().string() + ".gdb");
                std::error_code ec;
                std::filesystem::create_directories(out.parent_path(), ec);
                std::ofstream f(out, std::ios::binary);
                if (f) {
                    f.write(reinterpret_cast<const char*>(bytes.data()),
                            (std::streamsize)bytes.size());
                    OutputLog::warn("level edit: chunked .gdb entry - "
                                    "patched copy exported to " +
                                    out.string());
                }
            }
        } else {
            skipped += gdb_patches.size() / 3;
        }
    }

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
                DebugTrace::log(
                    "save: contents edit 0x%08X skipped: %s",
                    entity_hash, aerr.c_str());
            }
        }

        std::vector<std::pair<std::string, uint32_t>> new_save_entities;
        for (const auto& a : s.additions) {
            if (a.removed || !a.as_entity()) continue;
            std::string aerr;
            const uint32_t eh =
                create_entity_addition(g, a, babel_edits, aerr);
            if (!eh) {
                DebugTrace::log("save: entity addition skipped: %s",
                                aerr.c_str());
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
                DebugTrace::log(
                    "save: generator author skipped: no donor "
                    "generator/spawn point in this level");
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
                        DebugTrace::log(
                            "save: generator author failed: %s",
                            gerr2.c_str());
                    }
                }
                for (const auto& spa : s.spawn_point_adds) {
                    std::string serr2;
                    const uint32_t sp = create_spawn_point_entity(
                        g, donor, spa.pos, serr2);
                    if (!sp) {
                        DebugTrace::log(
                            "save: spawn point author failed: %s",
                            serr2.c_str());
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
                        DebugTrace::log(
                            "save: no free native spawn point field for "
                            "0x%08X", spa.spawn_points_record);
                        continue;
                    }
                    if (!g.AddField(spa.spawn_points_record,
                                    fnv1_32(fname), 7, sp)) {
                        DebugTrace::log(
                            "save: spawn list append failed for "
                            "0x%08X", spa.spawn_points_record);
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
                DebugTrace::log(
                    "save: chest .save registry rewrite skipped: %s",
                    serr.c_str());
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
            DebugTrace::log(
                "save: .save PhysicsData patched %zu of %zu entit(ies)",
                save_physics_patched, save_physics_patches.size());
            if (save_physics_patched == 0 &&
                new_entities_created == 0 &&
                generators_created == 0 &&
                save_entities_deleted == 0) {
                save_rewrite_bytes.clear();
                save_rewrite_index = -1;
                save_rewrite_bnk.clear();
            }
        } else {
            DebugTrace::log(
                "save: .save entry unavailable for PhysicsData patches");
        }
    }

    std::string bake_note;
    std::vector<std::string> gen_asset_models;
    for (const auto& addition : s.additions) {
        if (addition.removed) continue;
        gen_asset_models.insert(gen_asset_models.end(),
                                addition.asset_models.begin(),
                                addition.asset_models.end());
    }
    for (const auto& ga : s.generators) {
        if (ga.removed) continue;
        for (const auto& mp : ga.asset_models) {
            gen_asset_models.push_back(mp);
        }
    }
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
                } else {
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
                                        DebugTrace::log(
                                            "save: engine_data idx=%d "
                                            "resource list +%zu hash(es)",
                                            ed_idx, mdl_hashes.size());
                                    } else {
                                        bake_ed_bytes.clear();
                                        DebugTrace::log(
                                            "save: engine_data already "
                                            "lists all placed models");
                                    }
                                } else {
                                    bake_ed_bytes.clear();
                                    DebugTrace::log(
                                        "save: engine_data patch "
                                        "skipped: %s", perr.c_str());
                                }
                            } else {
                                DebugTrace::log(
                                    "save: engine_data entry not found "
                                    "(%s)", key.c_str());
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
                                        DebugTrace::log(
                                            "save: lmp idx=%d probe "
                                            "record(s) appended",
                                            lmp_idx);
                                    } else {
                                        bake_lmp_bytes.clear();
                                        DebugTrace::log(
                                            "save: lmp already has all "
                                            "placed instances");
                                    }
                                } else {
                                    bake_lmp_bytes.clear();
                                    DebugTrace::log(
                                        "save: lmp patch skipped: %s",
                                        perr.c_str());
                                }
                            } else {
                                DebugTrace::log(
                                    "save: lmp entry not found (%s)",
                                    lmp_key.c_str());
                            }

                            const std::filesystem::path data_dir =
                                std::filesystem::path(s.lev.bnk_path)
                                    .parent_path();
                            const std::string streaming_path =
                                (data_dir / "streaming.bnk").string();
                            const std::string globals_path =
                                (data_dir / "Globals" /
                                 "globals_models.bnk").string();
                            const std::string models_key =
                                stem + "_models.bnk";
                            std::vector<BnkWriter::EntryAddition>
                                mdl_adds;
                            std::vector<BnkWriter::EntryAddition>
                                stream_adds;
                            const int models_idx =
                                std::filesystem::exists(streaming_path)
                                    ? BnkCache::find_index(streaming_path,
                                                           models_key)
                                    : -1;
                            std::vector<uint8_t> models_blob;
                            if (models_idx >= 0) {
                                models_blob = BnkCache::extract_bytes(
                                    streaming_path, models_idx);
                            }
                            std::vector<std::string> inject_paths;
                            for (const auto& a : s.additions) {
                                if (a.removed) continue;
                                inject_paths.push_back(a.model_path);
                            }
                            for (const auto& mp : gen_asset_models) {
                                inject_paths.push_back(mp);
                            }
                            for (const auto& inj_path : inject_paths) {
                                const std::string lp =
                                    lower_model_path(inj_path);
                                const std::string want = norm_key(lp);
                                bool have = false;
                                if (!models_blob.empty()) {
                                    try {
                                        BNKReader lm(models_blob);
                                        have = nested_bank_has(lm, want);
                                    } catch (...) {}
                                }
                                if (!have &&
                                    BnkCache::find_index(globals_path,
                                                         want) >= 0) {
                                    have = true;
                                }
                                bool queued = false;
                                for (const auto& q : mdl_adds) {
                                    if (norm_key(q.name) == want) {
                                        queued = true;
                                        break;
                                    }
                                }
                                if (have || queued) continue;
                                std::string src_name;
                                std::vector<uint8_t> src_payload;
                                if (models_idx < 0 ||
                                    !find_in_nested_banks(
                                        streaming_path, "_models.bnk",
                                        want, src_name, src_payload)) {
                                    DebugTrace::log(
                                        "save: model body not found "
                                        "anywhere: %s", want.c_str());
                                    continue;
                                }
                                mdl_adds.push_back(
                                    {src_name, std::move(src_payload)});
                                const size_t slash = want.rfind('/');
                                if (slash != std::string::npos) {
                                    const std::string folder =
                                        want.substr(0, slash + 1);
                                    size_t got =
                                        collect_folder_from_nested_banks(
                                            s.lev.bnk_path,
                                            "_streaming.bnk", folder,
                                            stream_adds);
                                    if (got == 0) {
                                        for (const auto& other :
                                             S.bnk_paths) {
                                            if (other == s.lev.bnk_path) {
                                                continue;
                                            }
                                            got =
                                            collect_folder_from_nested_banks(
                                                other, "_streaming.bnk",
                                                folder, stream_adds);
                                            if (got) {
                                                DebugTrace::log(
                                                    "save: streaming donor "
                                                    "bnk %s",
                                                    other.c_str());
                                                break;
                                            }
                                        }
                                    }
                                    DebugTrace::log(
                                        "save: inject %s (+%zu streaming "
                                        "file(s))", src_name.c_str(),
                                        got);
                                }
                            }
                            if (!mdl_adds.empty()) {
                                const std::string scen_dir =
                                    stem.substr(0, stem.rfind('/') + 1);
                                const std::string hdrs_key =
                                    stem + "_texture_headers.bnk";
                                const std::string body_key =
                                    scen_dir + "textures.bnk";
                                const std::string mani_key =
                                    body_key + ".manifest";
                                const int hdrs_idx = BnkCache::find_index(
                                    s.lev.bnk_path, hdrs_key);
                                const int body_idx = BnkCache::find_index(
                                    s.lev.bnk_path, body_key);
                                const int mani_idx = BnkCache::find_index(
                                    s.lev.bnk_path, mani_key);
                                std::unordered_set<std::string> have_hdr;
                                std::unordered_set<std::string> have_body;
                                std::vector<uint8_t> hdrs_blob, body_blob;
                                if (hdrs_idx >= 0) {
                                    hdrs_blob = BnkCache::extract_bytes(
                                        s.lev.bnk_path, hdrs_idx);
                                    try {
                                        BNKReader r(hdrs_blob);
                                        for (const auto& fe :
                                             r.list_files())
                                            have_hdr.insert(
                                                norm_key(fe.name));
                                    } catch (...) {}
                                }
                                if (body_idx >= 0) {
                                    body_blob = BnkCache::extract_bytes(
                                        s.lev.bnk_path, body_idx);
                                    try {
                                        BNKReader r(body_blob);
                                        for (const auto& fe :
                                             r.list_files())
                                            have_body.insert(
                                                norm_key(fe.name));
                                    } catch (...) {}
                                }
                                {
                                    const int sh_idx =
                                        BnkCache::find_index(
                                            s.lev.bnk_path,
                                            "worlds/albion/shared/"
                                            "shared_6281.bnk");
                                    if (sh_idx >= 0) {
                                        try {
                                            std::vector<uint8_t> sh =
                                                BnkCache::extract_bytes(
                                                    s.lev.bnk_path,
                                                    sh_idx);
                                            BNKReader r(sh);
                                            for (const auto& fe :
                                                 r.list_files())
                                                have_body.insert(
                                                    norm_key(fe.name));
                                        } catch (...) {}
                                    }
                                }
                                std::vector<BnkWriter::EntryAddition>
                                    hdr_adds, body_adds;
                                std::string mani_append;
                                for (const auto& ma : mdl_adds) {
                                    std::vector<std::string> texs;
                                    collect_tex_refs(ma.payload, texs);
                                    for (const auto& t : texs) {
                                        if (!have_hdr.count(t)) {
                                            std::string sn;
                                            std::vector<uint8_t> sp;
                                            if (find_in_nested_banks(
                                                    s.lev.bnk_path,
                                                    "_texture_headers"
                                                    ".bnk",
                                                    t, sn, sp)) {
                                                hdr_adds.push_back(
                                                    {sn,
                                                     std::move(sp)});
                                                have_hdr.insert(t);
                                            } else {
                                                DebugTrace::log(
                                                    "save: tex header "
                                                    "not found: %s",
                                                    t.c_str());
                                            }
                                        }
                                        if (!have_body.count(t)) {
                                            std::string sn;
                                            std::vector<uint8_t> sp;
                                            if (find_in_nested_banks(
                                                    s.lev.bnk_path,
                                                    "/textures.bnk", t,
                                                    sn, sp)) {
                                                body_adds.push_back(
                                                    {sn,
                                                     std::move(sp)});
                                                have_body.insert(t);
                                                std::string tl = t;
                                                std::replace(tl.begin(),
                                                             tl.end(),
                                                             '/', '\\');
                                                mani_append +=
                                                    "\"" + tl + "\" \"" +
                                                    tl + "\" 0 0 3\r\n";
                                            } else {
                                                DebugTrace::log(
                                                    "save: tex body not "
                                                    "found: %s",
                                                    t.c_str());
                                            }
                                        }
                                    }
                                }
                                std::string terr;
                                if (!hdr_adds.empty() &&
                                    hdrs_idx >= 0 &&
                                    BnkWriter::AddEntriesToBnkBytes(
                                        hdrs_blob, hdr_adds, terr)) {
                                    BnkWriter::EntryReplacement r;
                                    r.file_index = hdrs_idx;
                                    r.payload = std::move(hdrs_blob);
                                    bake_more.push_back(std::move(r));
                                    DebugTrace::log(
                                        "save: +%zu texture header(s)",
                                        hdr_adds.size());
                                } else if (!hdr_adds.empty()) {
                                    DebugTrace::log(
                                        "save: tex header add failed: "
                                        "%s", terr.c_str());
                                }
                                if (!body_adds.empty() &&
                                    body_idx >= 0 &&
                                    BnkWriter::AddEntriesToBnkBytes(
                                        body_blob, body_adds, terr)) {
                                    BnkWriter::EntryReplacement r;
                                    r.file_index = body_idx;
                                    r.payload = std::move(body_blob);
                                    bake_more.push_back(std::move(r));
                                    DebugTrace::log(
                                        "save: +%zu texture bodies",
                                        body_adds.size());
                                    if (mani_idx >= 0 &&
                                        !mani_append.empty()) {
                                        std::vector<uint8_t> mani =
                                            BnkCache::extract_bytes(
                                                s.lev.bnk_path,
                                                mani_idx);
                                        const bool crlf =
                                            std::find(mani.begin(),
                                                      mani.end(),
                                                      (uint8_t)'\r') !=
                                            mani.end();
                                        if (!crlf) {
                                            std::string tmp;
                                            for (char c : mani_append)
                                                if (c != '\r')
                                                    tmp.push_back(c);
                                            mani_append = tmp;
                                        }
                                        mani.insert(mani.end(),
                                                    mani_append.begin(),
                                                    mani_append.end());
                                        BnkWriter::EntryReplacement r2;
                                        r2.file_index = mani_idx;
                                        r2.payload = std::move(mani);
                                        bake_more.push_back(
                                            std::move(r2));
                                    }
                                } else if (!body_adds.empty()) {
                                    DebugTrace::log(
                                        "save: tex body add failed: %s",
                                        terr.c_str());
                                }
                                std::string aerr;
                                if (!BnkWriter::AddEntriesToBnkBytes(
                                        models_blob, mdl_adds, aerr)) {
                                    DebugTrace::log(
                                        "save: models bank add failed: "
                                        "%s", aerr.c_str());
                                } else {
                                    bake_models_index = models_idx;
                                    bake_models_bytes =
                                        std::move(models_blob);
                                    bake_streaming_path = streaming_path;
                                }
                                if (bake_models_index >= 0 &&
                                    !stream_adds.empty()) {
                                    const std::string lvs_key =
                                        stem + "_streaming.bnk";
                                    const int lvs_idx =
                                        BnkCache::find_index(
                                            s.lev.bnk_path, lvs_key);
                                    if (lvs_idx >= 0) {
                                        std::vector<uint8_t> lvs =
                                            BnkCache::extract_bytes(
                                                s.lev.bnk_path, lvs_idx);
                                        std::string serr;
                                        std::vector<
                                            BnkWriter::EntryAddition>
                                            fresh;
                                        try {
                                            BNKReader lr(lvs);
                                            for (auto& sa : stream_adds) {
                                                if (!nested_bank_has(
                                                        lr,
                                                        norm_key(
                                                            sa.name))) {
                                                    fresh.push_back(
                                                        std::move(sa));
                                                }
                                            }
                                        } catch (...) {}
                                        if (!fresh.empty() &&
                                            BnkWriter::
                                                AddEntriesToBnkBytes(
                                                    lvs, fresh, serr)) {
                                            bake_lvstream_index = lvs_idx;
                                            bake_lvstream_bytes =
                                                std::move(lvs);
                                        } else if (!fresh.empty()) {
                                            DebugTrace::log(
                                                "save: level streaming "
                                                "add failed: %s",
                                                serr.c_str());
                                        }
                                    }
                                }
                            }
                        }
                    } catch (const std::exception& ex) {
                        bake_ed_index = -1;
                        bake_ed_bytes.clear();
                        bake_lmp_index = -1;
                        bake_lmp_bytes.clear();
                        DebugTrace::log(
                            "save: engine_data/lmp patch failed: %s",
                            ex.what());
                    } catch (...) {
                        bake_ed_index = -1;
                        bake_ed_bytes.clear();
                        bake_lmp_index = -1;
                        bake_lmp_bytes.clear();
                    }
                }
                s.saving = true;
            }
        } else {
            if (!level_placement_deletes.empty()) {
                msg = "save failed: this level entry cannot be rewritten "
                      "to remove render placements";
                return false;
            }
            bake_note = std::string("; placed model(s) kept app-side "
                                    "(level BNK not rewritable: ") +
                        (s.lev.compressed ? "chunk-compressed"
                                          : "no locator") + ")";
        }
        if (!need_bake) {
            DebugTrace::log("save: bake not started:%s",
                            bake_note.c_str());
        }
    }

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

    std::string berr;
    bool rebuilt = true;
    if (need_bake) {
    progress_update(10, 100, "Rebuilding level BNK...");
    BnkCache::invalidate(bake_bnk_path);
    if (bake_iso) {
        rebuilt = BnkWriter::RebuildIsoLevelBnk(bake_vpath, bake_index,
                                                bake_bytes, berr);
    } else {
        std::vector<BnkWriter::EntryReplacement> reps(1);
        reps[0].file_index = bake_index;
        reps[0].payload = std::move(bake_bytes);
        if (bake_ed_index >= 0 && !bake_ed_bytes.empty()) {
            BnkWriter::EntryReplacement r;
            r.file_index = bake_ed_index;
            r.payload = std::move(bake_ed_bytes);
            reps.push_back(std::move(r));
        }
        if (bake_lmp_index >= 0 && !bake_lmp_bytes.empty()) {
            BnkWriter::EntryReplacement r;
            r.file_index = bake_lmp_index;
            r.payload = std::move(bake_lmp_bytes);
            reps.push_back(std::move(r));
        }
        if (bake_lvstream_index >= 0 && !bake_lvstream_bytes.empty()) {
            BnkWriter::EntryReplacement r;
            r.file_index = bake_lvstream_index;
            r.payload = std::move(bake_lvstream_bytes);
            reps.push_back(std::move(r));
        }
        for (auto& r : bake_more) reps.push_back(std::move(r));
        rebuilt = BnkWriter::RebuildWithReplacedEntries(bake_bnk_path,
                                                        reps, berr);
        if (rebuilt && bake_models_index >= 0 &&
            !bake_models_bytes.empty()) {
            progress_update(75, 100, "Rebuilding streaming.bnk...");
            BnkCache::invalidate(bake_streaming_path);
            std::vector<BnkWriter::EntryReplacement> sreps(1);
            sreps[0].file_index = bake_models_index;
            sreps[0].payload = std::move(bake_models_bytes);
            rebuilt = BnkWriter::RebuildWithReplacedEntries(
                bake_streaming_path, sreps, berr);
            BnkCache::invalidate(bake_streaming_path);
            DebugTrace::log(
                "save: streaming.bnk models inject %s %s",
                rebuilt ? "OK" : "FAILED", berr.c_str());
        }
    }
    DebugTrace::log("save: bake %s (iso=%d target='%s') %s",
                    rebuilt ? "OK" : "FAILED", bake_iso ? 1 : 0,
                    bake_iso ? bake_vpath.c_str() : bake_bnk_path.c_str(),
                    berr.c_str());
    }

    bool contents_ok = true;
    if (rebuilt && !gdb_rewrite_bytes.empty()) {
        progress_update(85, 100, "Writing container data...");
        std::string gerr;
        if (!gdb_rewrite_loose.empty()) {
            std::ofstream f(gdb_rewrite_loose,
                            std::ios::binary | std::ios::trunc);
            contents_ok = bool(f);
            if (contents_ok) {
                f.write(reinterpret_cast<const char*>(
                            gdb_rewrite_bytes.data()),
                        std::streamsize(gdb_rewrite_bytes.size()));
                contents_ok = f.good();
            }
            if (!contents_ok) gerr = "loose .gdb write failed";
            if (contents_ok && save_rewrite_index >= 0 &&
                !save_rewrite_bnk.empty()) {
                BnkCache::invalidate(save_rewrite_bnk);
                contents_ok = BnkWriter::RebuildWithReplacedEntry(
                    save_rewrite_bnk, save_rewrite_index,
                    save_rewrite_bytes, gerr);
                BnkCache::invalidate(save_rewrite_bnk);
            }
        } else if (gdb_rewrite_iso) {
            contents_ok = BnkWriter::RebuildIsoLevelBnk(
                ISO::IsoMount::strip_iso_prefix(gdb_rewrite_bnk),
                gdb_rewrite_index, gdb_rewrite_bytes, gerr);
            if (contents_ok && save_rewrite_index >= 0) {
                contents_ok = BnkWriter::RebuildIsoLevelBnk(
                    ISO::IsoMount::strip_iso_prefix(save_rewrite_bnk),
                    save_rewrite_index, save_rewrite_bytes, gerr);
            }
        } else if (gdb_rewrite_index >= 0) {
            BnkCache::invalidate(gdb_rewrite_bnk);
            std::vector<BnkWriter::EntryReplacement> reps(1);
            reps[0].file_index = gdb_rewrite_index;
            reps[0].payload = std::move(gdb_rewrite_bytes);
            const bool save_same_bnk =
                save_rewrite_index >= 0 &&
                save_rewrite_bnk == gdb_rewrite_bnk;
            if (save_same_bnk) {
                BnkWriter::EntryReplacement r;
                r.file_index = save_rewrite_index;
                r.payload = std::move(save_rewrite_bytes);
                reps.push_back(std::move(r));
            }
            contents_ok = BnkWriter::RebuildWithReplacedEntries(
                gdb_rewrite_bnk, reps, gerr);
            BnkCache::invalidate(gdb_rewrite_bnk);
            if (contents_ok && save_rewrite_index >= 0 && !save_same_bnk) {
                BnkCache::invalidate(save_rewrite_bnk);
                contents_ok = BnkWriter::RebuildWithReplacedEntry(
                    save_rewrite_bnk, save_rewrite_index,
                    save_rewrite_bytes, gerr);
                BnkCache::invalidate(save_rewrite_bnk);
            }
        } else {
            contents_ok = false;
            gerr = "no .gdb target";
        }
        DebugTrace::log(
            "save: gdb contents rewrite %s (%zu edit(s), %zu new "
            "entit(ies), %zu deleted entit(ies), %zu deleted spawn "
            "point(s), %zu repaired spawn point(s), %zu repaired "
            "generator(s)) %s",
            contents_ok ? "OK" : "FAILED", contents_applied,
            new_entities_created, gdb_entities_deleted,
            spawn_points_deleted,
            spawn_points_repaired, generators_repaired, gerr.c_str());
    }

    if (rebuilt && contents_ok && gdb_rewrite_bytes.empty() &&
        save_rewrite_index >= 0 && !save_rewrite_bytes.empty()) {
        progress_update(85, 100, "Writing entity save data...");
        std::string gerr;
        BnkCache::invalidate(save_rewrite_bnk);
        if (ISO::IsoMount::is_iso_path(save_rewrite_bnk)) {
            contents_ok = BnkWriter::RebuildIsoLevelBnk(
                ISO::IsoMount::strip_iso_prefix(save_rewrite_bnk),
                save_rewrite_index, save_rewrite_bytes, gerr);
        } else {
            contents_ok = BnkWriter::RebuildWithReplacedEntry(
                save_rewrite_bnk, save_rewrite_index, save_rewrite_bytes,
                gerr);
        }
        BnkCache::invalidate(save_rewrite_bnk);
        DebugTrace::log("save: .save physics rewrite %s (%zu entit(ies)) %s",
                        contents_ok ? "OK" : "FAILED",
                        save_physics_patched, gerr.c_str());
    }

    size_t text_written = 0;
    if (rebuilt && contents_ok && !babel_edits.empty()) {
        progress_update(92, 100, "Writing text banks...");
        std::string root = S.root_dir;
        {
            std::error_code ec;
            std::filesystem::path rp(root);
            if (!root.empty() &&
                std::filesystem::is_regular_file(rp, ec)) {
                root = rp.parent_path().string();
            }
        }
        std::string terr;
        if (TextBank::ApplyEdits(root, babel_edits, terr)) {
            text_written = babel_edits.size();
        } else {
            contents_ok = false;
            OutputLog::error("level edit: text bank write failed: " +
                             terr);
        }
    }

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
}
