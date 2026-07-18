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
