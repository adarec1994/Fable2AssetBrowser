            std::unordered_map<std::string,
                               std::vector<const FlatAssetEntry*>>
                mdl_by_gmd_asset_key;
            mdl_by_gmd_asset_key.reserve(S.all_mdl_files.size());
            for (const auto& m : S.all_mdl_files) {
                const std::string key =
                    compact_match_key(model_name_from_path(m.full_path));
                if (!key.empty()) {
                    mdl_by_gmd_asset_key[key].push_back(&m);
                }
            }
            auto choose_gmd_layout_child_model =
                [&](const std::string& asset_key) {
                    if (asset_key.empty()) {
                        return static_cast<const FlatAssetEntry*>(nullptr);
                    }
                    if (const char* curated =
                            GdbModelHashlist::LookupEntityKey(asset_key))
                    {
                        if (const FlatAssetEntry* hit =
                                resolve_model_by_lower_path(
                                    lower_slash(curated)))
                        {
                            return hit;
                        }
                    }
                    auto choose_best =
                        [&](const std::vector<const FlatAssetEntry*>& hits) {
                            const FlatAssetEntry* best = nullptr;
                            int best_score = INT_MIN;
                            for (const FlatAssetEntry* e : hits) {
                                if (!e) continue;
                                const std::string p =
                                    lower_slash(e->full_path);
                                int score = model_bank_score(e);
                                if (e->from_nested) score += 250;
                                if (p.find("/doors_windows/") !=
                                    std::string::npos)
                                {
                                    score += 400;
                                }
                                if (p.find("/props/") != std::string::npos) {
                                    score += 200;
                                }
                                if (p.find("/buildings/") !=
                                    std::string::npos)
                                {
                                    score += 100;
                                }
                                score -= int(std::min<size_t>(
                                    e->full_path.size(), 240));
                                if (!best || score > best_score) {
                                    best = e;
                                    best_score = score;
                                }
                            }
                            return best;
                        };
                    if (auto it = mdl_by_gmd_asset_key.find(asset_key);
                        it != mdl_by_gmd_asset_key.end())
                    {
                        return choose_best(it->second);
                    }
                    std::vector<const FlatAssetEntry*> fuzzy;
                    for (const auto& kv : mdl_by_gmd_asset_key) {
                        const std::string& model_key = kv.first;
                        if (model_key.size() < 5) continue;
                        if (model_key.find(asset_key) == std::string::npos &&
                            asset_key.find(model_key) == std::string::npos)
                        {
                            continue;
                        }
                        fuzzy.insert(fuzzy.end(),
                                     kv.second.begin(),
                                     kv.second.end());
                    }
                    return choose_best(fuzzy);
                };

            using GmdSidecarHit = std::pair<std::string, int>;
            std::unordered_map<std::string, std::vector<GmdSidecarHit>>
                global_gmd_sidecar_index;
            bool global_gmd_sidecar_index_built = false;
            size_t global_gmd_sidecar_index_bnks = 0;
            auto build_global_gmd_sidecar_index = [&]() {
                if (global_gmd_sidecar_index_built) return;
                global_gmd_sidecar_index_built = true;

                std::vector<std::string> candidate_bnks;
                auto add_unique_bnk = [&](const std::string& path) {
                    if (path.empty()) return;
                    const std::string norm = lower_slash(path);
                    for (const auto& existing : candidate_bnks) {
                        if (lower_slash(existing) == norm) return;
                    }
                    candidate_bnks.push_back(path);
                };
                auto is_streaming_bnk = [](const std::string& path) {
                    std::string p = lower_slash(path);
                    const size_t slash = p.find_last_of('/');
                    const std::string leaf = slash == std::string::npos
                        ? p
                        : p.substr(slash + 1);
                    return leaf.find("streaming") != std::string::npos;
                };
                for (const auto& p : S.bnk_paths) {
                    if (is_streaming_bnk(p)) add_unique_bnk(p);
                }
                for (const auto& p : S.nested_bnk_paths) {
                    if (is_streaming_bnk(p)) add_unique_bnk(p);
                }

                for (const auto& bnk_path : candidate_bnks) {
                    try {
                        const BnkCache::Entry bnk = BnkCache::get(bnk_path);
                        const auto& files = bnk.reader->list_files();
                        bool had_gmd = false;
                        for (size_t i = 0; i < files.size(); ++i) {
                            std::string key = lower_slash(files[i].name);
                            if (key.size() < 8 ||
                                key.compare(key.size() - 8, 8,
                                            ".mdl.gmd") != 0)
                            {
                                continue;
                            }
                            global_gmd_sidecar_index[key].push_back(
                                {bnk_path, static_cast<int>(i)});
                            had_gmd = true;
                        }
                        if (had_gmd) ++global_gmd_sidecar_index_bnks;
                    } catch (...) {
                    }
                }
            };
            auto find_global_gmd_sidecar =
                [&](const std::string& key,
                    const std::string& preferred_model_bnk)
                    -> const GmdSidecarHit* {
                    build_global_gmd_sidecar_index();
                    auto it = global_gmd_sidecar_index.find(lower_slash(key));
                    if (it == global_gmd_sidecar_index.end() ||
                        it->second.empty())
                    {
                        return nullptr;
                    }
                    const std::string preferred =
                        lower_slash(preferred_model_bnk);
                    const bool prefer_globals =
                        preferred.find("/globals/") != std::string::npos ||
                        preferred.find("globals_models.bnk") !=
                            std::string::npos;
                    if (prefer_globals) {
                        for (const auto& hit : it->second) {
                            const std::string bnk = lower_slash(hit.first);
                            if (bnk.find("/globals/") != std::string::npos ||
                                bnk.find("globals_streaming.bnk") !=
                                    std::string::npos)
                            {
                                return &hit;
                            }
                        }
                    }
                    return &it->second.front();
                };

            std::unordered_map<std::string, std::vector<GmdLayoutChild>>
                gmd_layout_child_cache;
            std::unordered_set<std::string> gmd_layout_child_missing;
            auto load_gmd_layout_children_for_model =
                [&](const FlatAssetEntry* model_hit)
                    -> const std::vector<GmdLayoutChild>* {
                    if (!model_hit || model_hit->full_path.empty()) {
                        return static_cast<const std::vector<GmdLayoutChild>*>(
                            nullptr);
                    }
                    const std::string model_lower =
                        lower_slash(model_hit->full_path);
                    if (auto it = gmd_layout_child_cache.find(model_lower);
                        it != gmd_layout_child_cache.end())
                    {
                        return &it->second;
                    }
                    if (gmd_layout_child_missing.find(model_lower) !=
                        gmd_layout_child_missing.end())
                    {
                        return static_cast<const std::vector<GmdLayoutChild>*>(
                            nullptr);
                    }

                    std::vector<uint8_t> bytes;
                    std::string gmd_source_bnk;
                    auto leaf_of_lower_slash_path =
                        [](const std::string& path) {
                            std::string p = lower_slash(path);
                            const size_t slash = p.find_last_of('/');
                            return slash == std::string::npos
                                ? p
                                : p.substr(slash + 1);
                        };
                    auto sibling_with_leaf =
                        [](const std::string& path,
                           const std::string& leaf) {
                            if (path.empty() || leaf.empty()) return std::string();
                            std::string p = path;
                            std::replace(p.begin(), p.end(), '\\', '/');
                            const size_t slash = p.find_last_of('/');
                            if (slash == std::string::npos) return leaf;
                            return p.substr(0, slash + 1) + leaf;
                        };
                    auto add_unique_bnk =
                        [](std::vector<std::string>& out,
                           const std::string& path) {
                            if (path.empty()) return;
                            const std::string norm = lower_slash(path);
                            for (const auto& existing : out) {
                                if (lower_slash(existing) == norm) return;
                            }
                            out.push_back(path);
                        };
                    auto add_virtual_match =
                        [&](std::vector<std::string>& out,
                            const std::string& path) {
                            if (path.empty()) return;
                            std::string p = path;
                            std::replace(p.begin(), p.end(), '\\', '/');
                            std::string low = lower_slash(p);
                            const size_t data_pos = low.find("data/");
                            if (data_pos == std::string::npos) return;
                            if (auto found = find_bnk_by_virtual_path(
                                    p.substr(data_pos)))
                            {
                                add_unique_bnk(out, *found);
                            }
                        };
                    auto derived_streaming_leaf_for_model_bnk =
                        [](const std::string& bnk_path) {
                            const std::string leaf =
                                [&]() {
                                    std::string p = lower_slash(bnk_path);
                                    const size_t slash = p.find_last_of('/');
                                    return slash == std::string::npos
                                        ? p
                                        : p.substr(slash + 1);
                                }();
                            if (leaf == "globals_models.bnk") {
                                return std::string("globals_streaming.bnk");
                            }
                            static constexpr const char* suffix =
                                "_models.bnk";
                            const size_t n = std::strlen(suffix);
                            if (leaf.size() > n &&
                                leaf.compare(leaf.size() - n, n, suffix) == 0)
                            {
                                return leaf.substr(0, leaf.size() - n) +
                                       "_streaming.bnk";
                            }
                            return std::string();
                        };
                    auto add_model_streaming_sidecars =
                        [&](std::vector<std::string>& out,
                            const std::string& model_bnk_path) {
                            const std::string stream_leaf =
                                derived_streaming_leaf_for_model_bnk(
                                    model_bnk_path);
                            if (stream_leaf.empty()) return;

                            const std::string sibling =
                                sibling_with_leaf(
                                    model_bnk_path, stream_leaf);
                            add_unique_bnk(out, sibling);
                            add_virtual_match(out, sibling);

                            auto leaf_matches =
                                [&](const std::string& candidate_path) {
                                    const std::string leaf =
                                        leaf_of_lower_slash_path(
                                            candidate_path);
                                    if (leaf == stream_leaf) return true;
                                    if (leaf.size() <= stream_leaf.size() + 1) {
                                        return false;
                                    }
                                    const size_t off =
                                        leaf.size() - stream_leaf.size();
                                    return leaf.compare(
                                               off,
                                               stream_leaf.size(),
                                               stream_leaf) == 0 &&
                                           leaf[off - 1] == '_';
                                };
                            for (const auto& p : S.bnk_paths) {
                                if (leaf_matches(p)) add_unique_bnk(out, p);
                            }
                            for (const auto& p : S.nested_bnk_paths) {
                                if (leaf_matches(p)) add_unique_bnk(out, p);
                            }
                        };
                    auto try_extract =
                        [&](const std::string& bnk_path,
                            const std::string& key,
                            bool allow_leaf_match) {
                            if (!bytes.empty() || bnk_path.empty() ||
                                key.empty())
                            {
                                return;
                            }
                            int idx = BnkCache::find_index(bnk_path, key);
                            if (idx < 0 && allow_leaf_match) {
                                idx = BnkCache::find_index(
                                    bnk_path, leaf_of_lower_slash_path(key));
                            }
                            if (idx < 0) return;
                            try {
                                bytes = BnkCache::extract_bytes(bnk_path, idx);
                                if (!bytes.empty()) {
                                    gmd_source_bnk = bnk_path;
                                }
                            } catch (...) {
                                bytes.clear();
                                gmd_source_bnk.clear();
                            }
                        };

                    const std::string gmd_key = model_lower + ".gmd";
                    const std::string gmd_leaf =
                        leaf_of_lower_slash_path(gmd_key);
                    const bool generic_leaf =
                        gmd_leaf == "exterior.mdl.gmd" ||
                        gmd_leaf == "interior.mdl.gmd";
                    std::vector<std::string> gmd_sidecar_bnks;
                    add_unique_bnk(gmd_sidecar_bnks, model_hit->bnk_path);
                    add_model_streaming_sidecars(
                        gmd_sidecar_bnks, model_hit->bnk_path);
                    if (auto nested_it =
                            S.nested_bnk_virtual_paths.find(
                                model_hit->bnk_path);
                        nested_it != S.nested_bnk_virtual_paths.end())
                    {
                        add_model_streaming_sidecars(
                            gmd_sidecar_bnks, nested_it->second);
                    }
                    for (const auto& p : g_level_vfs_streaming_bnks) {
                        add_unique_bnk(
                            gmd_sidecar_bnks,
                            resolve_streaming_bnk_path(p));
                    }

                    for (const auto& bnk_path : gmd_sidecar_bnks) {
                        try_extract(bnk_path, gmd_key, false);
                        if (!bytes.empty()) break;
                    }
                    for (const auto& c : streaming_model_candidates) {
                        if (!bytes.empty()) break;
                        if (!c.from_gmd || c.gmd_file_index < 0 ||
                            c.gmd_bnk_path.empty())
                        {
                            continue;
                        }
                        if (c.resolved_lower == model_lower ||
                            c.hint_lower == model_lower)
                        {
                            try {
                                bytes = BnkCache::extract_bytes(
                                    c.gmd_bnk_path, c.gmd_file_index);
                                if (!bytes.empty()) {
                                    gmd_source_bnk = c.gmd_bnk_path;
                                }
                            } catch (...) {
                                bytes.clear();
                                gmd_source_bnk.clear();
                            }
                        }
                    }
                    if (bytes.empty()) {
                        if (const GmdSidecarHit* global_hit =
                                find_global_gmd_sidecar(
                                    gmd_key, model_hit->bnk_path))
                        {
                            try {
                                bytes = BnkCache::extract_bytes(
                                    global_hit->first, global_hit->second);
                                if (!bytes.empty()) {
                                    gmd_source_bnk = global_hit->first;
                                }
                            } catch (...) {
                                bytes.clear();
                                gmd_source_bnk.clear();
                            }
                        }
                    }
                    if (bytes.empty() && !generic_leaf) {
                        for (const auto& bnk_path : gmd_sidecar_bnks) {
                            try_extract(bnk_path, gmd_key, true);
                            if (!bytes.empty()) break;
                        }
                    }

                    if (bytes.empty()) {
                        gmd_layout_child_missing.insert(model_lower);
                        ++gdb_gmd_layout_sidecars_missing;
                        return static_cast<const std::vector<GmdLayoutChild>*>(
                            nullptr);
                    }
                    ++gdb_gmd_layout_sidecars_loaded;
                    if (!gmd_source_bnk.empty()) {
                        ++gdb_gmd_layout_sidecar_sources[gmd_source_bnk];
                    }
                    std::vector<GmdLayoutChild> children =
                        parse_gmd_layout_children(bytes);
                    for (auto& child : children) {
                        if (const FlatAssetEntry* hit =
                                choose_gmd_layout_child_model(
                                    child.asset_key))
                        {
                            child.resolved_path = hit->full_path;
                            child.resolved_key =
                                compact_match_key(
                                    model_name_from_path(hit->full_path));
                        }
                    }
                    auto [it, _] = gmd_layout_child_cache.emplace(
                        model_lower, std::move(children));
                    return &it->second;
                };

            auto should_emit_gmd_layout_child =
                [](const GmdLayoutChild& child) {




                    std::string text =
                        lower_slash(child.asset_key + " " +
                                    child.resolved_path);
                    return text.find("door") != std::string::npos ||
                           text.find("window") != std::string::npos ||
                           text.find("win_") != std::string::npos ||
                           text.find("_win") != std::string::npos ||
                           text.find("sign") != std::string::npos ||
                           text.find("lamp") != std::string::npos ||
                           text.find("lantern") != std::string::npos ||
                           text.find("candle") != std::string::npos ||
                           text.find("light") != std::string::npos;
                };
            auto emit_gmd_layout_children_for_model =
                [&](const FlatAssetEntry* parent_model,
                    const Level::PropInstance& parent_inst) {
                    if (!parent_model) return size_t(0);
                    const std::string parent_lower =
                        lower_slash(parent_model->full_path);
                    const bool shell_parent =
                        parent_lower.find("/exterior.mdl") !=
                            std::string::npos ||
                        parent_lower.find("/interior.mdl") !=
                            std::string::npos ||
                        parent_lower.find("bs_market_tarotstall/") !=
                            std::string::npos;
                    if (!shell_parent) return size_t(0);

                    const std::vector<GmdLayoutChild>* children =
                        load_gmd_layout_children_for_model(parent_model);
                    if (!children || children->empty()) return size_t(0);

                    size_t emitted = 0;
                    const Xform3f parent_xf =
                        prop_instance_xform(parent_inst);
                    for (const auto& child : *children) {
                        if (!should_emit_gmd_layout_child(child)) continue;
                        const FlatAssetEntry* child_model =
                            choose_gmd_layout_child_model(child.asset_key);
                        if (!child_model) {
                            ++gdb_gmd_layout_children_missing;
                            continue;
                        }
                        const Xform3f child_world =
                            xform_compose(parent_xf, child.local);
                        Level::PropInstance child_inst =
                            prop_instance_from_xform(
                                child_world, parent_inst.hash);
                        if (append_prop_instance_for_model(
                                child_model, child_inst))
                        {
                            ++emitted;
                            ++gdb_gmd_layout_children_emitted;
                            ++gdb_gmd_layout_child_paths[
                                child_model->full_path];
                        }
                    }
                    return emitted;
                };
