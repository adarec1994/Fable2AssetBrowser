std::string resolve_streaming_bnk_path(const std::string& vfs_stream_path)
{
    std::string wanted_leaf =
        std::filesystem::path(vfs_stream_path).filename().string();
    std::transform(wanted_leaf.begin(), wanted_leaf.end(),
                   wanted_leaf.begin(), ::tolower);

    auto leaf_matches = [&](const std::string& mounted_leaf_lower) {
        if (mounted_leaf_lower == wanted_leaf) return true;
        if (mounted_leaf_lower.size() <= wanted_leaf.size() + 1) return false;
        const size_t off = mounted_leaf_lower.size() - wanted_leaf.size();
        if (mounted_leaf_lower.compare(off, wanted_leaf.size(),
                                       wanted_leaf) != 0) return false;
        return mounted_leaf_lower[off - 1] == '_';
    };

    if (auto resolved = find_bnk_by_virtual_path(vfs_stream_path)) {
        return *resolved;
    }
    for (const auto& p : S.bnk_paths) {
        std::string leaf = std::filesystem::path(p).filename().string();
        std::transform(leaf.begin(), leaf.end(), leaf.begin(), ::tolower);
        if (leaf_matches(leaf)) return p;
    }
    for (const auto& p : S.nested_bnk_paths) {
        std::string leaf = std::filesystem::path(p).filename().string();
        std::transform(leaf.begin(), leaf.end(), leaf.begin(), ::tolower);
        if (leaf_matches(leaf)) return p;
    }
    return {};
}

std::vector<StreamingModelCandidate>
collect_streaming_model_candidates(const std::vector<std::string>& streaming_bnks)
{
    std::unordered_map<std::string, const FlatAssetEntry*> mdl_by_path;
    mdl_by_path.reserve(S.all_mdl_files.size());
    std::unordered_map<std::string, std::vector<const FlatAssetEntry*>> mdl_by_key;
    mdl_by_key.reserve(S.all_mdl_files.size());
    for (const auto& e : S.all_mdl_files) {
        mdl_by_path.emplace(lower_slash(e.full_path), &e);
        mdl_by_key[compact_match_key(model_name_from_path(e.full_path))]
            .push_back(&e);
    }
    auto choose_global_model = [&](const std::string& hint_path) {
        const std::string hint_lower = lower_slash(hint_path);
        if (auto exact = mdl_by_path.find(hint_lower); exact != mdl_by_path.end()) {
            return exact->second;
        }
        for (const auto& kv : mdl_by_path) {
            const std::string& model_path = kv.first;
            if (model_path.size() >= hint_lower.size() &&
                model_path.compare(model_path.size() - hint_lower.size(),
                                   hint_lower.size(),
                                   hint_lower) == 0) {
                return kv.second;
            }
        }

        const std::string hint_key =
            compact_match_key(model_name_from_path(hint_path));
        if (hint_key.empty()) return static_cast<const FlatAssetEntry*>(nullptr);

        auto choose_best = [](const std::vector<const FlatAssetEntry*>& hits) {
            const FlatAssetEntry* best = nullptr;
            int best_score = INT_MIN;
            for (const FlatAssetEntry* e : hits) {
                if (!e) continue;
                int score = 0;
                const std::string lower = lower_slash(e->full_path);
                if (lower.find("/globals_models.bnk") == std::string::npos) {
                    score += 500;
                }
                if (e->from_nested) score += 250;
                score -= int(std::min<size_t>(e->full_path.size(), 240));
                if (!best || score > best_score) {
                    best = e;
                    best_score = score;
                }
            }
            return best;
        };

        if (auto it = mdl_by_key.find(hint_key); it != mdl_by_key.end()) {
            return choose_best(it->second);
        }

        std::vector<const FlatAssetEntry*> fuzzy;
        for (const auto& kv : mdl_by_key) {
            const std::string& model_key = kv.first;
            if (model_key.size() < 5) continue;
            const bool related =
                model_key.find(hint_key) != std::string::npos ||
                hint_key.find(model_key) != std::string::npos;
            if (!related) continue;
            fuzzy.insert(fuzzy.end(), kv.second.begin(), kv.second.end());
        }
        return choose_best(fuzzy);
    };

    std::vector<StreamingModelCandidate> out;
    std::unordered_set<std::string> seen;
    for (const auto& vfs_path : streaming_bnks) {
        const std::string mounted = resolve_streaming_bnk_path(vfs_path);
        if (mounted.empty()) continue;
        try {
            const BnkCache::Entry bnk = BnkCache::get(mounted);
            const auto& files = bnk.reader->list_files();
            auto add_candidate = [&](std::string hint,
                                      bool from_gmd,
                                      int gmd_index) {
                std::string norm = lower_slash(hint);
                auto [seen_it, inserted] = seen.insert(norm);
                if (!inserted) {
                    if (from_gmd) {
                        for (auto& existing : out) {
                            if (lower_slash(existing.hint_path) == norm) {
                                existing.from_gmd = true;
                                existing.gmd_bnk_path = mounted;
                                existing.gmd_file_index = gmd_index;
                                break;
                            }
                        }
                    }
                    return;
                }

                StreamingModelCandidate c;
                c.hint_path = std::move(hint);
                c.hint_lower = norm;
                c.display_name = model_name_from_path(c.hint_path);
                c.key = compact_match_key(c.display_name);
                c.from_gmd = from_gmd;
                if (from_gmd) {
                    c.gmd_bnk_path = mounted;
                    c.gmd_file_index = gmd_index;
                }
                c.entry = choose_global_model(c.hint_path);
                if (c.entry) {
                    c.resolved_path = c.entry->full_path;
                }
                c.resolved_lower = lower_slash(c.resolved_path);
                c.path_key =
                    compact_match_key(c.hint_path + " " + c.resolved_path);
                out.push_back(std::move(c));
            };

            for (size_t file_i = 0; file_i < files.size(); ++file_i) {
                const auto& f = files[file_i];
                std::string lower = lower_slash(f.name);
                if (lower.size() >= 8 &&
                    lower.compare(lower.size() - 8, 8, ".mdl.gmd") == 0) {
                    std::string mdl = f.name;
                    mdl.resize(mdl.size() - 4);
                    add_candidate(std::move(mdl), true, int(file_i));
                    continue;
                }
                if (lower.size() >= 4 &&
                    lower.compare(lower.size() - 4, 4, ".hkx") == 0) {
                    std::string mdl = f.name;
                    mdl.resize(mdl.size() - 4);
                    mdl += ".mdl";
                    add_candidate(std::move(mdl), false, -1);
                }
            }
        } catch (...) {
        }
    }
    return out;
}

