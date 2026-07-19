bool apply_texture_replacement(const std::vector<TextureTargets>& groups,
                               const std::vector<TexturePart>& shared_mip0,
                               const TexWriter::BuiltTex& built,
                               std::string& err) {
    if (groups.empty()) {
        err = "no texture copies resolved";
        return false;
    }
    struct TargetChange {
        std::string path;
        std::vector<BnkWriter::EntryReplacement> replacements;
        std::vector<BnkWriter::EntryAddition> additions;
    };
    std::map<std::string, TargetChange> targets;
    auto replace = [&](const TexturePart& part,
                       const std::vector<uint8_t>& payload) {
        TargetChange& change = targets[part.path];
        change.path = part.path;
        change.replacements.push_back({part.index, payload});
    };

    // The texture blob is always assembled header || mip0-part || body.
    // When shared mip0 entries exist they are replaced in place (with an
    // empty payload when the new texture has no 1024 top mip, so the stale
    // original can never be prepended again). Without any shared entry the
    // top mip travels inside each body copy.
    std::vector<uint8_t> body_payload = built.body;
    if (shared_mip0.empty() && !built.mip0.empty()) {
        body_payload.insert(body_payload.begin(), built.mip0.begin(),
                            built.mip0.end());
    }
    for (const TextureTargets& target : groups) {
        if (target.header.index >= 0) replace(target.header, built.header);
        if (target.body.index >= 0) replace(target.body, body_payload);
    }
    for (const TexturePart& part : shared_mip0) {
        replace(part, built.mip0);
    }

    struct NestedChange {
        std::string path;
        std::string parent_path;
        int parent_index = -1;
    };
    std::vector<NestedChange> nested_changes;
    std::map<std::string, TargetChange> disk_targets;
    for (const auto& [path, change] : targets) {
        const auto parent_it = S.nested_bnk_parents.find(path);
        if (parent_it == S.nested_bnk_parents.end()) {
            TargetChange& disk = disk_targets[path];
            disk = change;
            continue;
        }
        if (S.nested_bnk_parents.count(parent_it->second)) {
            err = "Texture replacement does not support nested BNKs deeper "
                  "than one level.";
            return false;
        }
        const auto virtual_it = S.nested_bnk_virtual_paths.find(path);
        if (virtual_it == S.nested_bnk_virtual_paths.end()) {
            err = "Could not resolve the nested texture bank in its parent.";
            return false;
        }
        const int parent_index = BnkCache::find_index(
            parent_it->second, normalized_path(virtual_it->second));
        if (parent_index < 0) {
            err = "Could not find the nested texture bank entry in its "
                  "parent.";
            return false;
        }
        TargetChange& parent = disk_targets[parent_it->second];
        parent.path = parent_it->second;
        nested_changes.push_back({path, parent_it->second, parent_index});
    }

    // A stale index can hand us parents inside the pristine-backup copy;
    // writing there would silently divert the edit (and corrupt the backup).
    for (const auto& [path, change] : disk_targets) {
        if (GameBackup::IsBackupPath(path)) {
            err = "The replacement target resolves into the f2ab_backup "
                  "copy (" + path + "). Re-open the game folder so the "
                  "index points at the live banks, then retry.";
            return false;
        }
        // A materialized nested bank that lost its parent mapping would be
        // rebuilt as a throwaway temp file and the change silently lost.
        if (path.find("f2_nested_bnk_tree") != std::string::npos) {
            err = "internal: nested bank " + path + " is not mapped to its "
                  "parent bank. Re-open the game folder and retry.";
            return false;
        }
    }

    progress_update(66, 100, "Checking backup coverage");
    std::vector<std::string> paths;
    for (const auto& [path, change] : disk_targets) paths.push_back(path);
    if (!GameBackup::EnsureFilesCovered(paths, err)) {
        return false;
    }

    struct Snapshot {
        std::string path;
        std::vector<uint8_t> bytes;
    };
    auto read_snapshot = [&](const std::string& path,
                              Snapshot& snapshot) {
        snapshot.path = path;
        return read_mutable_file(path, snapshot.bytes, err);
    };
    auto restore = [](const std::vector<Snapshot>& snapshots) {
        for (const Snapshot& snapshot : snapshots) {
            std::string restore_error;
            restore_mutable_file(snapshot.path, snapshot.bytes,
                                 restore_error);
            BnkCache::invalidate(snapshot.path);
        }
    };

    std::vector<Snapshot> nested_snapshots;
    for (const NestedChange& nested : nested_changes) {
        progress_update(74, 100, "Rebuilding " +
            std::filesystem::path(nested.path).filename().string());
        Snapshot snapshot;
        if (!read_snapshot(nested.path, snapshot)) {
            restore(nested_snapshots);
            return false;
        }
        nested_snapshots.push_back(std::move(snapshot));
        TargetChange& change = targets[nested.path];
        BnkCache::invalidate(nested.path);
        if (!BnkWriter::RebuildWithChanges(
                nested.path, change.replacements, change.additions, err)) {
            err = std::filesystem::path(nested.path).filename().string() +
                  ": " + err;
            restore(nested_snapshots);
            return false;
        }
        BnkCache::invalidate(nested.path);
        Snapshot rebuilt;
        if (!read_snapshot(nested.path, rebuilt)) {
            restore(nested_snapshots);
            return false;
        }
        TargetChange& parent = disk_targets[nested.parent_path];
        parent.replacements.push_back(
            {nested.parent_index, std::move(rebuilt.bytes)});
    }

    bool any_iso = false;
    for (const auto& [path, change] : disk_targets) {
        if (ISO::IsoMount::is_iso_path(path)) { any_iso = true; break; }
    }

    if (any_iso) {
        // ISO members cannot be staged as sibling files; keep the
        // snapshot-rollback path for them.
        progress_update(74, 100, "Snapshotting banks for rollback");
        std::vector<Snapshot> disk_snapshots;
        for (const auto& [path, change] : disk_targets) {
            Snapshot snapshot;
            if (!read_snapshot(path, snapshot)) {
                restore(nested_snapshots);
                return false;
            }
            disk_snapshots.push_back(std::move(snapshot));
        }
        size_t applied = 0;
        int rebuild_progress = 80;
        for (auto& [path, change] : disk_targets) {
            progress_update(rebuild_progress, 100, "Rewriting " +
                std::filesystem::path(path).filename().string());
            rebuild_progress = std::min(rebuild_progress + 6, 92);
            BnkCache::invalidate(path);
            if (!BnkWriter::RebuildWithChanges(
                    path, change.replacements, change.additions, err)) {
                err = std::filesystem::path(path).filename().string() +
                      ": " + err;
                break;
            }
            ++applied;
        }
        if (applied != disk_targets.size()) {
            restore(disk_snapshots);
            restore(nested_snapshots);
            return false;
        }
        for (const auto& [path, change] : disk_targets) {
            BnkCache::invalidate(path);
        }
        return true;
    }

    // Stage every rebuilt bank next to its live file, then commit with
    // renames. No whole-bank snapshot reads: until the commit renames run,
    // the live banks are untouched, and the .f2ab_prev copies make the
    // commit itself reversible.
    struct StagedBank { std::string live, staged, prev; };
    std::vector<StagedBank> staged_banks;
    int stage_progress = 76;
    bool staged_ok = true;
    for (auto& [path, change] : disk_targets) {
        progress_update(stage_progress, 100, "Rebuilding " +
            std::filesystem::path(path).filename().string() +
            " (large banks take a while)");
        stage_progress = std::min(stage_progress + 5, 90);
        StagedBank bank{path, path + ".f2ab_staged", path + ".f2ab_prev"};
        std::error_code ec;
        std::filesystem::remove(bank.staged, ec);
        if (!BnkWriter::RebuildWithChangesStaged(
                path, change.replacements, change.additions, bank.staged,
                err)) {
            err = std::filesystem::path(path).filename().string() + ": " +
                  err;
            staged_ok = false;
            break;
        }
        staged_banks.push_back(std::move(bank));
    }
    if (!staged_ok) {
        std::error_code ec;
        for (const StagedBank& bank : staged_banks) {
            std::filesystem::remove(bank.staged, ec);
        }
        restore(nested_snapshots);
        return false;
    }

    progress_update(93, 100, "Committing rebuilt banks");
    size_t swapped = 0;
    for (; swapped < staged_banks.size(); ++swapped) {
        const StagedBank& bank = staged_banks[swapped];
        std::error_code ec;
        BnkCache::invalidate(bank.live);
        std::filesystem::remove(bank.prev, ec);
        ec.clear();
        std::filesystem::rename(bank.live, bank.prev, ec);
        if (ec) {
            err = "could not move " + bank.live + " aside: " + ec.message();
            break;
        }
        std::filesystem::rename(bank.staged, bank.live, ec);
        if (ec) {
            err = "could not activate the rebuilt bank for " + bank.live +
                  ": " + ec.message();
            std::error_code undo;
            std::filesystem::rename(bank.prev, bank.live, undo);
            break;
        }
    }
    if (swapped != staged_banks.size()) {
        std::error_code ec;
        for (size_t k = 0; k < swapped; ++k) {
            const StagedBank& bank = staged_banks[k];
            std::filesystem::remove(bank.live, ec);
            std::filesystem::rename(bank.prev, bank.live, ec);
            BnkCache::invalidate(bank.live);
        }
        for (size_t k = swapped; k < staged_banks.size(); ++k) {
            std::filesystem::remove(staged_banks[k].staged, ec);
        }
        restore(nested_snapshots);
        return false;
    }
    for (const StagedBank& bank : staged_banks) {
        std::error_code ec;
        std::filesystem::remove(bank.prev, ec);
        BnkCache::invalidate(bank.live);
    }
    return true;
}
