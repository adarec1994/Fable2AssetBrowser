bool apply_texture_replacement(const TextureTargets& target,
                               const TexWriter::BuiltTex& built,
                               std::string& err) {
    DebugTrace::log(
        "texture-replace: apply begin header_bytes=%zu mip_bytes=%zu body_bytes=%zu",
        built.header.size(), built.mip0.size(), built.body.size());
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
        DebugTrace::log(
            "texture-replace: nested bank='%s' parent='%s' parent_index=%d replacements=%zu additions=%zu",
            path.c_str(), parent_it->second.c_str(), parent_index,
            change.replacements.size(), change.additions.size());
    }

    std::vector<std::string> paths;
    for (const auto& [path, change] : disk_targets) paths.push_back(path);
    DebugTrace::log("texture-replace: backup check targets=%zu", paths.size());
    if (!GameBackup::EnsureFilesCovered(paths, err)) {
        DebugTrace::log("texture-replace: backup failed error='%s'", err.c_str());
        return false;
    }
    DebugTrace::log("texture-replace: backup ready");

    struct Snapshot {
        std::string path;
        std::vector<uint8_t> bytes;
    };
    auto read_snapshot = [&](const std::string& path,
                             Snapshot& snapshot) {
        std::error_code size_error;
        const uintmax_t size = std::filesystem::file_size(path, size_error);
        DebugTrace::log(
            "texture-replace: snapshot begin path='%s' bytes=%llu size_error=%d",
            path.c_str(), (unsigned long long)(size_error ? 0 : size),
            size_error ? 1 : 0);
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            err = "Could not read " + path;
            return false;
        }
        snapshot.path = path;
        snapshot.bytes.assign(std::istreambuf_iterator<char>(input),
                              std::istreambuf_iterator<char>());
        const bool ok = input.good() || input.eof();
        DebugTrace::log(
            "texture-replace: snapshot end path='%s' captured=%zu ok=%d",
            path.c_str(), snapshot.bytes.size(), ok ? 1 : 0);
        return ok;
    };
    auto restore = [](const std::vector<Snapshot>& snapshots) {
        DebugTrace::log("texture-replace: rollback begin files=%zu",
                        snapshots.size());
        for (const Snapshot& snapshot : snapshots) {
            std::ofstream output(snapshot.path,
                                 std::ios::binary | std::ios::trunc);
            if (output) {
                output.write((const char*)snapshot.bytes.data(),
                             (std::streamsize)snapshot.bytes.size());
            }
            BnkCache::invalidate(snapshot.path);
            DebugTrace::log(
                "texture-replace: rollback file='%s' bytes=%zu",
                snapshot.path.c_str(), snapshot.bytes.size());
        }
        DebugTrace::log("texture-replace: rollback end");
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
        DebugTrace::log(
            "texture-replace: nested rebuild begin path='%s' replacements=%zu additions=%zu",
            nested.path.c_str(), change.replacements.size(),
            change.additions.size());
        if (!BnkWriter::RebuildWithChanges(
                nested.path, change.replacements, change.additions, err)) {
            err = std::filesystem::path(nested.path).filename().string() +
                  ": " + err;
            restore(nested_snapshots);
            return false;
        }
        DebugTrace::log("texture-replace: nested rebuild end path='%s'",
                        nested.path.c_str());
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
        size_t replacement_bytes = 0;
        for (const auto& replacement : change.replacements) {
            replacement_bytes += replacement.payload.size();
        }
        DebugTrace::log(
            "texture-replace: parent rebuild begin path='%s' replacements=%zu replacement_bytes=%zu additions=%zu",
            path.c_str(), change.replacements.size(), replacement_bytes,
            change.additions.size());
        if (!BnkWriter::RebuildWithChanges(
                path, change.replacements, change.additions, err)) {
            err = std::filesystem::path(path).filename().string() + ": " +
                  err;
            break;
        }
        DebugTrace::log("texture-replace: parent rebuild end path='%s'",
                        path.c_str());
        ++applied;
    }
    if (applied != disk_targets.size()) {
        DebugTrace::log(
            "texture-replace: parent rebuild failed applied=%zu targets=%zu error='%s'",
            applied, disk_targets.size(), err.c_str());
        restore(disk_snapshots);
        restore(nested_snapshots);
        return false;
    }
    for (const auto& [path, change] : disk_targets) {
        BnkCache::invalidate(path);
    }
    DebugTrace::log("texture-replace: apply success");
    return true;
}
