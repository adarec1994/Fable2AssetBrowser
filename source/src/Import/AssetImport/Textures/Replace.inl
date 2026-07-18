bool apply_texture_replacement(const TextureTargets& target,
                               const TexWriter::BuiltTex& built,
                               std::string& err) {
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
    replace(target.header, built.header);

    std::vector<uint8_t> body = built.body;
    if (target.mip0.path.empty()) {
        body.insert(body.begin(), built.mip0.begin(), built.mip0.end());
    } else if (target.mip0.index >= 0) {
        replace(target.mip0, built.mip0);
    } else if (!built.mip0.empty()) {
        TargetChange& change = targets[target.mip0.path];
        change.path = target.mip0.path;
        change.additions.push_back({target.virtual_path, built.mip0});
    }
    replace(target.body, body);

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

    std::vector<Snapshot> disk_snapshots;
    for (const auto& [path, change] : disk_targets) {
        Snapshot snapshot;
        if (!read_snapshot(path, snapshot)) return false;
        disk_snapshots.push_back(std::move(snapshot));
    }

    std::vector<Snapshot> nested_snapshots;
    for (const NestedChange& nested : nested_changes) {
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

    size_t applied = 0;
    for (auto& [path, change] : disk_targets) {
        BnkCache::invalidate(path);
        if (!BnkWriter::RebuildWithChanges(
                path, change.replacements, change.additions, err)) {
            err = std::filesystem::path(path).filename().string() + ": " +
                  err;
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
