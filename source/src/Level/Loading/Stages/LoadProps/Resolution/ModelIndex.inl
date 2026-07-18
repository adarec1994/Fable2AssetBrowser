            auto strip_suffix = [](std::string s, const char* suf) {
                size_t n = std::strlen(suf);
                if (s.size() > n && s.compare(s.size() - n, n, suf) == 0) {
                    s.resize(s.size() - n);
                }
                return s;
            };
            auto canonicalize_for_match = [&strip_suffix](std::string s) {
                size_t us = s.find_last_of('_');
                if (us != std::string::npos && us + 1 < s.size()) {
                    bool all_digits = true;
                    for (size_t k = us + 1; k < s.size(); ++k) {
                        if (s[k] < '0' || s[k] > '9') { all_digits = false; break; }
                    }
                    if (all_digits) s.resize(us);
                }
                static const char* prefixes[] = {
                    "NewObjectBuilding", "ObjectBuilding",
                    "NewObjectFurniture", "ObjectFurniture",
                    "NewObjectStatic", "ObjectStatic",
                    "NewObject", "Object",
                    "Static", "New"
                };
                bool stripped_prefix = true;
                while (stripped_prefix) {
                    stripped_prefix = false;
                    for (const char* pfx : prefixes) {
                        size_t pn = std::strlen(pfx);
                        if (s.size() > pn && s.compare(0, pn, pfx) == 0) {
                            s = s.substr(pn);
                            stripped_prefix = true;
                            break;
                        }
                    }
                }
                std::string out;
                out.reserve(s.size());
                for (char c : s) {
                    if (c == '_') continue;
                    out.push_back(char(std::tolower(static_cast<unsigned char>(c))));
                }
                out = strip_suffix(out, "facademid");
                out = strip_suffix(out, "facade");
                out = strip_suffix(out, "lod1");
                out = strip_suffix(out, "lod0");
                out = strip_suffix(out, "mid");
                return out;
            };

            std::vector<std::string> preferred_model_bnks;
            auto add_preferred_model_bnk = [&](const std::string& bnk) {
                if (bnk.empty()) return;
                std::string norm = bnk;
                std::transform(norm.begin(), norm.end(), norm.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                std::replace(norm.begin(), norm.end(), '\\', '/');
                if (std::find(preferred_model_bnks.begin(),
                              preferred_model_bnks.end(),
                              norm) == preferred_model_bnks.end()) {
                    preferred_model_bnks.push_back(std::move(norm));
                }
            };
            auto resolve_preferred_model_bnk = [&](const std::string& vpath) {
                if (vpath.empty()) return;
                if (auto found = find_bnk_by_virtual_path(vpath)) {
                    add_preferred_model_bnk(*found);
                    return;
                }
                size_t slash = vpath.find_last_of("/\\");
                std::string leaf = (slash == std::string::npos)
                    ? vpath : vpath.substr(slash + 1);
                std::transform(leaf.begin(), leaf.end(), leaf.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                if (auto found = find_bnk_by_filename(leaf)) {
                    add_preferred_model_bnk(*found);
                }
            };
            resolve_preferred_model_bnk(res.model_body_bnk);
            for (const auto& bnk : g_level_vfs_model_bnks) {
                resolve_preferred_model_bnk(bnk);
            }

            std::unordered_map<std::string, std::vector<const FlatAssetEntry*>> mdl_by_token;
            mdl_by_token.reserve(S.all_mdl_files.size() * 2);
            for (const auto& m : S.all_mdl_files) {
                std::string base = m.name;
                size_t dot = base.find_last_of('.');
                if (dot != std::string::npos) base.resize(dot);
                std::string lc;
                lc.reserve(base.size());
                for (char c : base) {
                    if (c == '_') continue;
                    lc.push_back(char(std::tolower(static_cast<unsigned char>(c))));
                }
                lc = strip_suffix(lc, "facademid");
                lc = strip_suffix(lc, "facade");
                lc = strip_suffix(lc, "lod1");
                lc = strip_suffix(lc, "lod0");
                lc = strip_suffix(lc, "mid");
                if (!lc.empty()) {
                    mdl_by_token[lc].push_back(&m);
                }
            }
            auto normalized_path = [](std::string s) {
                std::transform(s.begin(), s.end(), s.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                std::replace(s.begin(), s.end(), '\\', '/');
                return s;
            };
            auto model_bank_score = [&](const FlatAssetEntry* e) {
                if (!e) return 0;
                const std::string bnk = normalized_path(e->bnk_path);
                for (size_t i = 0; i < preferred_model_bnks.size(); ++i) {
                    if (bnk == preferred_model_bnks[i]) {
                        return 4000 - int(i);
                    }
                }
                if (bnk.find("/globals_models.bnk") != std::string::npos ||
                    bnk == "globals_models.bnk") {
                    return 100;
                }
                return 0;
            };
            auto choose_model_candidate =
                [&](const std::vector<const FlatAssetEntry*>& candidates) {
                    const FlatAssetEntry* best = nullptr;
                    int best_score = INT_MIN;
                    for (const FlatAssetEntry* e : candidates) {
                        int score = model_bank_score(e);
                        if (e && e->from_nested) score += 250;
                        score -= int(std::min<size_t>(e ? e->full_path.size() : 0, 200));
                        if (!best || score > best_score) {
                            best = e;
                            best_score = score;
                        }
                    }
                    return best;
                };

            std::unordered_map<uint32_t, std::vector<const FlatAssetEntry*>>
                mdl_by_model_path_hash;
            mdl_by_model_path_hash.reserve(S.all_mdl_files.size() * 2);
            std::unordered_map<std::string, std::vector<const FlatAssetEntry*>>
                mdl_by_lower_path;
            mdl_by_lower_path.reserve(S.all_mdl_files.size() * 2);
            for (const auto& m : S.all_mdl_files) {
                if (m.full_path.empty()) continue;
                mdl_by_model_path_hash[fnv1_model_path_hash(m.full_path)]
                    .push_back(&m);
                mdl_by_lower_path[lower_slash(m.full_path)].push_back(&m);
            }
            auto path_suffix_matches_local =
                [](const std::string& path, const std::string& target) {
                    if (path.empty() || target.empty()) return false;
                    if (path == target) return true;
                    return path.size() > target.size() &&
                           path.compare(path.size() - target.size(),
                                        target.size(), target) == 0 &&
                           path[path.size() - target.size() - 1] == '/';
                };
            std::unordered_map<uint32_t, const FlatAssetEntry*>
                model_path_hash_cache;
            model_path_hash_cache.reserve(info.placements.size());
            auto resolve_model_by_path_hash = [&](uint32_t model_path_hash) {
                if (model_path_hash == 0) {
                    return static_cast<const FlatAssetEntry*>(nullptr);
                }
                auto cached = model_path_hash_cache.find(model_path_hash);
                if (cached != model_path_hash_cache.end()) {
                    return cached->second;
                }
                const FlatAssetEntry* hit = nullptr;
                auto it = mdl_by_model_path_hash.find(model_path_hash);
                if (it != mdl_by_model_path_hash.end()) {
                    hit = choose_model_candidate(it->second);
                }
                model_path_hash_cache.emplace(model_path_hash, hit);
                return hit;
            };
            std::unordered_map<std::string, const FlatAssetEntry*>
                lower_path_model_cache;
            auto resolve_model_by_lower_path =
                [&](const std::string& lower_path) {
                    if (lower_path.empty()) {
                        return static_cast<const FlatAssetEntry*>(nullptr);
                    }
                    auto cached = lower_path_model_cache.find(lower_path);
                    if (cached != lower_path_model_cache.end()) {
                        return cached->second;
                    }
                    const FlatAssetEntry* hit = nullptr;
                    auto it = mdl_by_lower_path.find(lower_path);
                    if (it != mdl_by_lower_path.end()) {
                        hit = choose_model_candidate(it->second);
                    }
                    if (!hit) {
                        std::vector<const FlatAssetEntry*> suffix_hits;
                        for (const auto& kv : mdl_by_lower_path) {
                            if (path_suffix_matches_local(kv.first,
                                                          lower_path)) {
                                suffix_hits.insert(suffix_hits.end(),
                                                   kv.second.begin(),
                                                   kv.second.end());
                            }
                        }
                        if (!suffix_hits.empty()) {
                            hit = choose_model_candidate(suffix_hits);
                        }
                    }
                    if (!hit) {
                        const std::string leaf_key =
                            canonicalize_for_match(
                                model_name_from_path(lower_path));
                        if (!leaf_key.empty() &&
                            leaf_key != "interior" &&
                            leaf_key != "exterior")
                        {
                            auto tok = mdl_by_token.find(leaf_key);
                            if (tok != mdl_by_token.end()) {
                                hit = choose_model_candidate(tok->second);
                            }
                        }
                    }
                    lower_path_model_cache.emplace(lower_path, hit);
                    return hit;
                };
            std::unordered_map<std::string, const FlatAssetEntry*>
                entity_model_cache;
            auto resolve_model_for_entity = [&](const std::string& entity_name) {
                std::string tok = canonicalize_for_match(entity_name);
                if (tok.empty()) return static_cast<const FlatAssetEntry*>(nullptr);
                auto token_is_or_numbered = [&](const char* base) {
                    const size_t n = std::strlen(base);
                    if (tok == base) return true;
                    if (tok.size() <= n || tok.compare(0, n, base) != 0) {
                        return false;
                    }
                    for (size_t i = n; i < tok.size(); ++i) {
                        if (!std::isdigit(static_cast<unsigned char>(tok[i]))) {
                            return false;
                        }
                    }
                    return true;
                };
                const bool general_store_building =
                    token_is_or_numbered("generalstore");
                auto is_bad_general_store_fallback =
                    [&](const std::string& model_key,
                        const FlatAssetEntry* candidate) {
                        if (!general_store_building) return false;
                        if (model_key.find("signgeneralstore") !=
                            std::string::npos)
                        {
                            return true;
                        }
                        return candidate &&
                               compact_match_key(candidate->full_path).find(
                                   "signgeneralstore") != std::string::npos;
                    };

                auto cached = entity_model_cache.find(tok);
                if (cached != entity_model_cache.end()) {
                    return cached->second;
                }

                const FlatAssetEntry* best = nullptr;
                const std::string entity_lookup_key =
                    gdb_entity_key(entity_name);
                auto exact = mdl_by_token.find(tok);
                if (exact != mdl_by_token.end()) {
                    best = choose_model_candidate(exact->second);
                    if (is_bad_general_store_fallback(tok, best) ||
                        (best && is_bad_market_helper_substitution(
                            entity_lookup_key, tok, best->full_path)))
                    {
                        best = nullptr;
                    }
                    entity_model_cache.emplace(tok, best);
                    return best;
                }






                entity_model_cache.emplace(tok, best);
                return best;
            };

