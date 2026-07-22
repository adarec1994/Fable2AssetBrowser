bool SaveWorkingCopy(std::string& msg) {
    DebugLog::Scope debug_scope("Save level working copy");
    FlatAssetEntry entry_copy;
    bool edits_applied = false;
    size_t models = 0;
    size_t gens = 0;
    size_t patched = 0;
    size_t entity_deletes = 0;
    {
        std::lock_guard<std::mutex> lk(mtx());
        auto& s = st();
        if (!s.available) {
            msg = "no level loaded";
            return false;
        }
        entry_copy = s.entry;
        std::string werr;
        if (!write_additions(s, werr) || !write_spawns(s, werr)) {
            msg = werr;
            return false;
        }

        struct GdbPatch {
            uint32_t off;
            float v;
        };
        std::vector<GdbPatch> patches;
        std::unordered_set<uint32_t> deletes;
        std::vector<uint32_t> consumed;
        for (const auto& kv : s.edits) {
            const EditEntry& e = kv.second;
            if (!e.changed()) continue;
            if (e.lev_kind == 5) continue;
            if (e.deleted) {
                if (e.gdb_entity_hash) {
                    deletes.insert(e.gdb_entity_hash);
                    consumed.push_back(kv.first);
                }
                continue;
            }
            bool used = false;
            if (e.moved() &&
                (e.gdb_off[0] || e.gdb_off[1] || e.gdb_off[2])) {
                const float np[3] = {e.orig[0] + e.delta[0],
                                     e.orig[1] + e.delta[1],
                                     e.orig[2] + e.delta[2]};
                for (int i = 0; i < 3; ++i) {
                    if (e.gdb_off[i]) {
                        patches.push_back({e.gdb_off[i], np[i]});
                    }
                }
                used = true;
            }
            if (e.rotated() && e.gdb_rot_off[0] && e.gdb_rot_off[1] &&
                e.gdb_rot_off[2]) {
                patches.push_back(
                    {e.gdb_rot_off[0],
                     (e.orig_rot[2] + e.rot_deg[2]) * kDegToRad});
                patches.push_back(
                    {e.gdb_rot_off[1],
                     (e.orig_rot[1] + e.rot_deg[1]) * kDegToRad});
                patches.push_back(
                    {e.gdb_rot_off[2],
                     (e.orig_rot[0] + e.rot_deg[0]) * kDegToRad});
                used = true;
            }
            if (used) consumed.push_back(kv.first);
        }

        if (!patches.empty() || !deletes.empty()) {
            std::string gdb_file = s.gdb.file_path;
            if (gdb_file.empty() && !s.lev.file_path.empty()) {
                const std::filesystem::path candidate =
                    std::filesystem::path(s.lev.file_path)
                        .replace_extension(".gdb");
                std::error_code ec;
                if (std::filesystem::is_regular_file(candidate, ec)) {
                    gdb_file = candidate.string();
                    DebugLog::Write("save.gdb",
                                    "working-copy derived loose gdb: " +
                                        gdb_file);
                }
            }
            if (gdb_file.empty()) {
                msg = "this level has no editable .gdb";
                return false;
            }
            FileTarget gdb_target;
            gdb_target.file_path = gdb_file;
            gdb_target.valid = true;
            std::string perr;
            for (const auto& p : patches) {
                if (!patch_target(gdb_target, p.off, &p.v, 1, perr)) {
                    DebugLog::Write("save.gdb",
                                    "working-copy patch FAILED off=" +
                                        std::to_string(p.off) + ": " +
                                        perr);
                    msg = "save failed (.gdb): " + perr;
                    return false;
                }
                {
                    char dbg[96];
                    std::snprintf(dbg, sizeof(dbg),
                                  "working-copy patched off=%u val=%.3f",
                                  p.off, p.v);
                    DebugLog::Write("save.gdb", dbg);
                }
                ++patched;
            }
            if (!deletes.empty()) {
                std::vector<uint8_t> gbytes;
                {
                    std::ifstream f(gdb_file, std::ios::binary);
                    if (f) {
                        f.seekg(0, std::ios::end);
                        gbytes.resize(size_t(f.tellg()));
                        f.seekg(0);
                        f.read(reinterpret_cast<char*>(gbytes.data()),
                               std::streamsize(gbytes.size()));
                        if (!f) gbytes.clear();
                    }
                }
                if (gbytes.empty()) {
                    msg = "save failed: could not read " + gdb_file;
                    return false;
                }
                GdbEdit::GdbFile g;
                std::string gerr;
                if (!g.Parse(gbytes, gerr)) {
                    msg = "save failed: .gdb parse: " + gerr;
                    return false;
                }
                for (uint32_t hash : deletes) {
                    if (g.RemoveRecord(hash)) ++entity_deletes;
                }
                const std::vector<uint8_t> out = g.Serialize();
                std::ofstream f(gdb_file,
                                std::ios::binary | std::ios::trunc);
                if (!f) {
                    msg = "save failed: could not write " + gdb_file;
                    return false;
                }
                f.write(reinterpret_cast<const char*>(out.data()),
                        std::streamsize(out.size()));
                if (!f) {
                    msg = "save failed: .gdb write";
                    return false;
                }

                if (!s.lev.file_path.empty()) {
                    const std::filesystem::path save_path =
                        std::filesystem::path(s.lev.file_path)
                            .replace_extension(".save");
                    std::vector<uint8_t> sbytes;
                    std::ifstream sf(save_path, std::ios::binary);
                    if (sf) {
                        sf.seekg(0, std::ios::end);
                        sbytes.resize(size_t(sf.tellg()));
                        sf.seekg(0);
                        sf.read(reinterpret_cast<char*>(sbytes.data()),
                                std::streamsize(sbytes.size()));
                        if (!sf) sbytes.clear();
                    }
                    if (!sbytes.empty() &&
                        remove_save_entities(sbytes, deletes) > 0) {
                        std::ofstream of(save_path, std::ios::binary |
                                                        std::ios::trunc);
                        if (of) {
                            of.write(reinterpret_cast<const char*>(
                                         sbytes.data()),
                                     std::streamsize(sbytes.size()));
                        }
                    }
                }
            }
            if (!deletes.empty()) {
                s.edits.clear();
                s.undo_stack.clear();
            } else {
                for (uint32_t id : consumed) s.edits.erase(id);
            }
            edits_applied = true;
        }

        for (const auto& a : s.additions) {
            if (!a.removed) ++models;
        }
        for (const auto& g : s.generators) {
            if (!g.removed) ++gens;
        }
        bool still_dirty = false;
        for (const auto& kv : s.edits) {
            if (kv.second.changed()) {
                still_dirty = true;
                break;
            }
        }
        s.dirty = still_dirty;
    }
    if (edits_applied) {
        Level::OpenAsync(entry_copy);
    }
    msg = "level saved: " + std::to_string(models) +
          " placed model(s), " + std::to_string(gens) + " generator(s)";
    if (patched || entity_deletes) {
        msg += ", " + std::to_string(patched) + " transform value(s), " +
               std::to_string(entity_deletes) + " entity removal(s)";
    }
    debug_scope.Result("success | models=" + std::to_string(models) +
                       " | generators=" + std::to_string(gens) +
                       " | gdb_patches=" + std::to_string(patched) +
                       " | gdb_deletes=" + std::to_string(entity_deletes));
    return true;
}

void ClearEdits() {
    std::lock_guard<std::mutex> lk(mtx());
    st().edits.clear();
    st().undo_stack.clear();
    st().contents_edits.clear();
    st().contents_loot_edits.clear();
    st().dirty = false;
    ++st().revision;
}
