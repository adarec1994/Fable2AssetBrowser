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

            constexpr bool emit_gdb_render_placements = true;
            constexpr bool emit_derived_render_placements = false;
            std::unordered_map<std::string, Level::PropBlock> blocks_by_path;
            std::unordered_set<std::string> emitted_prop_transform_keys;
            emitted_prop_transform_keys.reserve(level_prop_blocks.size() * 64);
            std::unordered_set<uint32_t> authored_non_prop_instances;
            authored_non_prop_instances.reserve(g_level_spawn_markers.size());
            for (const LevelSpawnMarker& marker : g_level_spawn_markers) {
                if (marker.entity_hash != 0 &&
                    (marker.kind == 1 || marker.kind == 2 || marker.kind == 3)) {
                    authored_non_prop_instances.insert(marker.entity_hash);
                }
            }

            std::unordered_map<std::string, std::array<uint32_t, 6>>
                gdb_dup_slot_offsets;
            struct GdbSlotLink {
                std::array<uint32_t, 6> slots{};
                uint32_t entity_hash = 0;
            };
            std::unordered_map<std::string, GdbSlotLink> gdb_pos_slot_links;
            auto gdb_pos_link_key = [](const std::string& model_path,
                                       float x, float y, float z) {
                auto q = [](float v) -> long long {
                    if (!std::isfinite(v)) return 0ll;
                    return (long long)std::llround(v * 100.0f);
                };
                std::ostringstream os;
                os << lower_slash(model_path) << '|'
                   << q(x) << ',' << q(y) << ',' << q(z);
                return os.str();
            };
            auto record_gdb_pos_link = [&](const std::string& model_path,
                                           const Gdb::Placement& p) {
                if (!p.pos_value_off[0] && !p.pos_value_off[1] &&
                    !p.pos_value_off[2]) {
                    return;
                }
                GdbSlotLink link;
                link.slots = {p.pos_value_off[0], p.pos_value_off[1],
                              p.pos_value_off[2], p.rot_value_off[0],
                              p.rot_value_off[1], p.rot_value_off[2]};
                link.entity_hash = p.hash_a;
                gdb_pos_slot_links.emplace(
                    gdb_pos_link_key(model_path, p.x, p.y, p.z), link);
            };
            auto record_gdb_dup_offsets =
                [&](const Level::PropInstance& inst,
                    const std::string& model_path) {
                    if (!inst.gdb_pos_off[0] && !inst.gdb_pos_off[1] &&
                        !inst.gdb_pos_off[2]) {
                        return;
                    }
                    gdb_dup_slot_offsets.emplace(
                        prop_instance_transform_key(inst, model_path),
                        std::array<uint32_t, 6>{
                            inst.gdb_pos_off[0], inst.gdb_pos_off[1],
                            inst.gdb_pos_off[2], inst.gdb_rot_off[0],
                            inst.gdb_rot_off[1], inst.gdb_rot_off[2]});
                    GdbSlotLink link;
                    link.slots = {inst.gdb_pos_off[0], inst.gdb_pos_off[1],
                                  inst.gdb_pos_off[2], inst.gdb_rot_off[0],
                                  inst.gdb_rot_off[1], inst.gdb_rot_off[2]};
                    link.entity_hash = inst.gdb_entity_hash;
                    gdb_pos_slot_links.emplace(
                        gdb_pos_link_key(model_path, inst.values[0],
                                         inst.values[1], inst.values[2]),
                        link);
                };
            for (const auto& block : level_prop_blocks) {
                if (block.model_path.empty()) continue;
                for (const auto& inst : block.instances) {
                    emitted_prop_transform_keys.insert(
                        prop_instance_transform_key(inst, block.model_path));
                }
            }
            auto append_prop_instance_for_model =
                [&](const FlatAssetEntry* model_hit,
                    const Level::PropInstance& inst) {
                    if (!model_hit || model_hit->full_path.empty()) {
                        return false;
                    }
                    if (is_gdb_static_prop_reject_model(model_hit->full_path)) {
                        return false;
                    }
                    if (!emitted_prop_transform_keys.insert(
                            prop_instance_transform_key(
                                inst, model_hit->full_path)).second)
                    {
                        record_gdb_dup_offsets(inst, model_hit->full_path);
                        return false;
                    }
                    auto& pb = blocks_by_path[model_hit->full_path];
                    if (pb.model_path.empty()) {
                        pb.type = 0xB1;
                        pb.model_path = model_hit->full_path;
                    }
                    pb.instances.push_back(inst);
                    return true;
                };
            auto append_prop_instance_for_model_path =
                [&](const std::string& lower_path,
                    const Level::PropInstance& inst) {
                    const FlatAssetEntry* model_hit =
                        resolve_model_by_lower_path(lower_path);
                    return append_prop_instance_for_model(model_hit, inst);
                };

            size_t authored_shop_companions_emitted = 0;
            size_t authored_shop_companion_misses = 0;
            std::unordered_map<std::string, size_t>
                authored_shop_companion_paths;
            for (const auto& block : level_prop_blocks) {
                std::string exterior_path =
                    shop_facade_companion_exterior_path(block.model_path);
                if (exterior_path.empty()) continue;

                std::array<std::string, 2> companions = {
                    exterior_path,
                    companion_interior_path(exterior_path),
                };
                for (const auto& inst : block.instances) {
                    for (const std::string& companion_path : companions) {
                        if (companion_path.empty()) continue;
                        const std::string lower_path =
                            lower_slash(companion_path);
                        if (append_prop_instance_for_model_path(
                                lower_path, inst))
                        {
                            ++authored_shop_companions_emitted;
                            ++authored_shop_companion_paths[lower_path];
                        } else if (!resolve_model_by_lower_path(lower_path)) {
                            ++authored_shop_companion_misses;
                        }
                    }
                }
            }
            if (authored_shop_companions_emitted > 0 ||
                authored_shop_companion_misses > 0)
            {
                OutputLog::info(
                    "authored shop companions: emitted " +
                    std::to_string(authored_shop_companions_emitted) +
                    " instance(s), missing-path " +
                    std::to_string(authored_shop_companion_misses));
                std::vector<std::pair<std::string, size_t>> paths(
                    authored_shop_companion_paths.begin(),
                    authored_shop_companion_paths.end());
                std::sort(paths.begin(), paths.end(),
                          [](const auto& a, const auto& b) {
                              return a.second > b.second;
                          });
                const size_t n = std::min<size_t>(paths.size(), 6);
                for (size_t i = 0; i < n; ++i) {
                    OutputLog::info(
                        "  authored shop companion: " +
                        std::to_string(paths[i].second) + "x  " +
                        paths[i].first);
                }
            }
            const bool is_bwsmarket_level =
                lower_slash(entry.full_path).find("bwsmarket") !=
                std::string::npos;
            const bool is_bwsslums_level =
                lower_slash(entry.full_path).find("bwsslums") !=
                std::string::npos;
            const uint32_t bwsmarket_clocktower_base_hash =
                fnv1_model_path_hash(
                    "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\"
                    "BS_Market_ClockTower\\BS_Market_ClockTower.mdl");
            bool bwsmarket_has_explicit_clocktower_base_record = false;
            if (is_bwsmarket_level) {
                for (const auto& p : info.placements) {
                    if (p.model_path_hash == bwsmarket_clocktower_base_hash) {
                        bwsmarket_has_explicit_clocktower_base_record = true;
                        break;
                    }
                    if (std::find(p.model_path_hashes.begin(),
                                  p.model_path_hashes.end(),
                                  bwsmarket_clocktower_base_hash) !=
                        p.model_path_hashes.end())
                    {
                        bwsmarket_has_explicit_clocktower_base_record = true;
                        break;
                    }
                }
            }

            size_t save_physics_instances_emitted = 0;
            if (emit_derived_render_placements) {
                for (const auto& p : save_physics_placements) {
                    if (p.entity_name.empty()) continue;
                    std::string tok = canonicalize_for_match(p.entity_name);
                    if (tok.empty()) continue;

                    const FlatAssetEntry* hit =
                        resolve_model_for_entity(p.entity_name);
                    if (!hit) continue;
                    if (is_gdb_static_prop_reject_model(hit->full_path)) {
                        continue;
                    }

                    auto& pb = blocks_by_path[hit->full_path];
                    if (pb.model_path.empty()) {
                        pb.type = 0xB2;
                        pb.model_path = hit->full_path;
                    }

                    Level::PropInstance pi;
                    pi.hash = p.hash;
                    pi.values[0] = p.x;
                    pi.values[1] = p.y;
                    pi.values[2] = p.z;
                    float qx = p.qx, qy = p.qy, qz = p.qz, qw = p.qw;
                    const float qmag =
                        std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
                    if (std::isfinite(qmag) && qmag > 1e-6f) {
                        qx /= qmag; qy /= qmag; qz /= qmag; qw /= qmag;
                        const float num = 2.0f * (qw * qz + qx * qy);
                        const float den = 1.0f - 2.0f * (qy * qy + qz * qz);
                        const float mag = std::sqrt(num * num + den * den);
                        if (std::isfinite(mag) && mag > 1e-6f) {
                            pi.values[6] = num / mag;
                            pi.values[7] = den / mag;
                        } else {
                            pi.values[6] = 0.0f;
                            pi.values[7] = 1.0f;
                        }
                    } else {
                        pi.values[6] = 0.0f;
                        pi.values[7] = 1.0f;
                    }
                    pi.values[9] = pi.values[10] = pi.values[11] = 1.0f;
                    pb.instances.push_back(pi);
                    ++save_physics_instances_emitted;
                }
            }
            if (save_physics_instances_emitted > 0) {
                OutputLog::success(
                    "save-derived placements: " +
                    std::to_string(save_physics_instances_emitted) +
                    " PhysicsData instance(s) appended to prop pipeline");
            }

            size_t resolved = 0;
            size_t gdb_instances_emitted = 0;
            size_t gdb_full_euler_rotations = 0;
            size_t gdb_yaw_only_rotations = 0;
            size_t gdb_identity_rotations = 0;
            size_t gdb_pi_pair_yaw_rotations = 0;
            size_t gdb_model_hash_hits = 0;
            size_t gdb_model_hash_misses = 0;
            size_t gdb_authored_shell_skipped = 0;
            size_t gdb_authored_shop_companion_skipped = 0;
            size_t gdb_duplicate_instances_skipped = 0;
            size_t gdb_companion_interiors_emitted = 0;
            size_t gdb_companion_exteriors_emitted = 0;
            std::unordered_map<std::string, size_t>
                gdb_authored_shell_skip_paths;
            std::unordered_map<std::string, std::vector<std::string>>
                gdb_authored_shell_skip_samples;
            std::unordered_map<std::string, size_t>
                gdb_emitted_shell_paths;
            std::unordered_map<std::string, std::vector<std::string>>
                gdb_emitted_shell_samples;
            std::unordered_map<std::string, size_t>
                gdb_companion_interior_paths;
            std::unordered_map<std::string, size_t>
                gdb_companion_exterior_paths;
            std::unordered_map<std::string, size_t>
                gdb_duplicate_skip_paths;
            size_t gdb_shell_bad_position_skipped = 0;
            std::unordered_map<std::string, size_t>
                gdb_shell_bad_position_paths;
            std::unordered_map<std::string, size_t>
                gdb_interest_category_counts;
            std::unordered_map<std::string, size_t>
                gdb_interest_status_counts;
            std::unordered_map<std::string,
                               std::unordered_map<std::string, size_t>>
                gdb_interest_category_status_counts;
            size_t gdb_clocktower_seen = 0;
            size_t gdb_clocktower_emitted = 0;
            size_t gdb_clocktower_companions_emitted = 0;
            std::vector<std::string> gdb_clocktower_audit_lines;
            size_t gdb_shop_seen = 0;
            size_t gdb_shop_emitted = 0;
            size_t gdb_shop_authored_skipped = 0;
            size_t gdb_shop_duplicates = 0;
            size_t gdb_shop_unresolved = 0;
            size_t gdb_shop_companions_emitted = 0;
            size_t gdb_shop_companion_misses = 0;
            size_t gdb_shop_companion_invalid_positions = 0;
            size_t gdb_nohash_shell_companions_emitted = 0;
            size_t gdb_nohash_shell_companion_misses = 0;
            size_t gdb_gmd_layout_children_emitted = 0;
            size_t gdb_gmd_layout_children_missing = 0;
            size_t gdb_gmd_layout_sidecars_loaded = 0;
            size_t gdb_gmd_layout_sidecars_missing = 0;
            std::unordered_map<std::string, size_t>
                gdb_gmd_layout_child_paths;
            std::unordered_map<std::string, size_t>
                gdb_gmd_layout_sidecar_sources;
            std::unordered_map<std::string, size_t>
                gdb_shop_companion_paths;
            std::unordered_map<std::string, size_t>
                gdb_nohash_shell_companion_paths;
            std::vector<std::string> gdb_shop_audit_lines;
            struct NoHashShellCandidate {
                Gdb::Placement placement;
                std::string entity_key;
                std::string category;
            };
            std::vector<NoHashShellCandidate> gdb_nohash_shell_candidates;
            std::vector<Level::PropInstance> gdb_generalshop_floor_anchors;
            std::vector<Level::PropInstance> gdb_tavern_pub_anchors;
            struct HouseCompanionAudit {
                size_t skipped = 0;
                size_t exterior_hits = 0;
                size_t exterior_misses = 0;
                size_t interior_hits = 0;
                size_t interior_misses = 0;
                std::string exterior_path;
                std::string interior_path;
                std::vector<std::string> samples;
            };
            std::unordered_map<std::string, HouseCompanionAudit>
                gdb_house_companion_audits;
            std::unordered_set<std::string> gdb_emitted_instance_keys;
            gdb_emitted_instance_keys.reserve(info.placements.size() * 2);
            auto is_market_shop_key = [](const std::string& key) {
                return key.find("largeshop") != std::string::npos ||
                       key.find("smallshop") != std::string::npos ||
                       key.find("generalshop") != std::string::npos ||
                       key.find("generalstore") != std::string::npos ||
                       key.find("tavern") != std::string::npos ||
                       key.find("openstall") != std::string::npos ||
                       key.find("marketstall") != std::string::npos ||
                       key.find("tarotstall") != std::string::npos ||
                       key.find("pub") != std::string::npos ||
                       key.find("inn") != std::string::npos;
            };
            auto is_market_shop_path = [](const std::string& model_path) {
                const std::string p = lower_slash(model_path);
                return p.find("bs_market_largeshop") != std::string::npos ||
                       p.find("bs_market_smallshop") != std::string::npos ||
                       p.find("bs_market_generalshop") != std::string::npos ||
                       p.find("bs_market_tavern") != std::string::npos ||
                       p.find("openstall") != std::string::npos ||
                       p.find("marketstall") != std::string::npos ||
                       p.find("tarotstall") != std::string::npos ||
                       p.find("tavern") != std::string::npos ||
                       p.find("/pub") != std::string::npos ||
                       p.find("_pub") != std::string::npos ||
                       p.find("/inn") != std::string::npos ||
                       p.find("_inn") != std::string::npos;
            };
            auto is_nohash_market_shell_key =
                [](const std::string& entity_key) {
                    return entity_key == "bsmarkettavern" ||
                           entity_key == "generalstore" ||
                           entity_key == "generalstore1";
                };
            auto has_worldish_gdb_position = [](const Gdb::Placement& p) {
                if (!std::isfinite(p.x) || !std::isfinite(p.y) ||
                    !std::isfinite(p.z))
                {
                    return false;
                }
                if (p.x < -64.0f || p.x > 512.0f ||
                    p.y < -64.0f || p.y > 512.0f ||
                    p.z < -64.0f || p.z > 256.0f)
                {
                    return false;
                }
                constexpr float kPiLocal = 3.14159265358979323846f;
                const bool looks_like_rotation_triplet =
                    std::fabs(p.x) < 10.0f &&
                    ((std::fabs(p.y) < 0.02f && std::fabs(p.z) < 0.02f) ||
                     (std::fabs(p.y + kPiLocal) < 0.02f &&
                      std::fabs(p.z + kPiLocal) < 0.02f));
                return !looks_like_rotation_triplet;
            };
            auto make_gdb_prop_instance_no_count =
                [](const Gdb::Placement& p) {
                    Level::PropInstance pi;
                    pi.hash = p.hash_a;
                    pi.gdb_entity_hash = p.hash_a;
                    pi.values[0] = p.x;
                    pi.values[1] = p.y;
                    pi.values[2] = p.z;
                    pi.gdb_pos_off[0] = p.pos_value_off[0];
                    pi.gdb_pos_off[1] = p.pos_value_off[1];
                    pi.gdb_pos_off[2] = p.pos_value_off[2];
                    pi.gdb_rot_off[0] = p.rot_value_off[0];
                    pi.gdb_rot_off[1] = p.rot_value_off[1];
                    pi.gdb_rot_off[2] = p.rot_value_off[2];
                    const float scale =
                        (std::isfinite(p.scale) && p.scale > 0.01f &&
                         p.scale < 100.0f)
                            ? p.scale : 1.0f;
                    if (p.has_rotation) {
                        fill_gdb_rotation_matrix(
                            pi, p.rot_x, p.rot_y, p.rot_z, scale);
                    } else {
                        const float s_yaw = std::sin(p.yaw);
                        const float c_yaw = std::cos(p.yaw);
                        if (std::isfinite(s_yaw) && std::isfinite(c_yaw)) {
                            pi.values[6] = s_yaw;
                            pi.values[7] = c_yaw;
                        } else {
                            pi.values[6] = 0.0f;
                            pi.values[7] = 1.0f;
                        }
                        pi.values[9] = pi.values[10] = pi.values[11] = scale;
                    }
                    return pi;
                };
            auto classify_gdb_interest =
                [](const std::string& entity_key,
                   const std::string& token,
                   const std::string* model_path) {
                    std::string text = entity_key + " " + token;
                    if (model_path) {
                        text += " ";
                        text += lower_slash(*model_path);
                    }
                    auto has = [&](const char* needle) {
                        return text.find(needle) != std::string::npos;
                    };
                    if (has("openstall") || has("marketstall") ||
                        has("tarotstall") || has("stall"))
                    {
                        return std::string("stall");
                    }
                    if (has("lamp") || has("lantern") ||
                        has("candleholder") || has("candle") ||
                        has("lightfixing") || has("lightceiling") ||
                        has("oillamp") || has("oillantern"))
                    {
                        return std::string("light");
                    }
                    if (has("caravan") || has("coachhouse") ||
                        has("coachouse") || has("coach"))
                    {
                        return std::string("caravan");
                    }
                    if (has("tavern") || has("pub") || has("inn")) {
                        return std::string("tavern");
                    }
                    if (has("generalshop") || has("generalstore") ||
                        has("largeshop") || has("smallshop") ||
                        has("clotheshop") || has("shop"))
                    {
                        return std::string("shop");
                    }
                    if (has("townhouse") || has("slumstreethouse") ||
                        has("shantie") || has("shanty") || has("house"))
                    {
                        return std::string("house");
                    }
                    return std::string();
                };
            auto add_gdb_interest_row =
                [&](const Gdb::Placement& p,
                    const std::string& entity_key,
                    std::string category,
                    const char* status,
                    const std::string& model_path) {
                    if (category.empty() && !model_path.empty()) {
                        const std::string token =
                            canonicalize_for_match(p.entity_name);
                        category = classify_gdb_interest(
                            entity_key, token, &model_path);
                    }
                    if (category.empty()) return;
                    ++gdb_interest_category_counts[category];
                    const std::string status_key = status ? status : "";
                    ++gdb_interest_status_counts[status_key];
                    ++gdb_interest_category_status_counts[category][status_key];
                };

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
            size_t gdb_render_poll = 0;
            for (const auto& p : info.placements) {
                if ((++gdb_render_poll & 0x7fu) == 0 &&
                    bail_if_cancelled("gdb render placement resolve"))
                {
                    return false;
                }
                if (!emit_gdb_render_placements) continue;
                if (authored_non_prop_instances.count(p.hash_a) != 0 ||
                    p.skeleton_file_hash != 0 ||
                    p.retarget_skeleton_file_hash != 0) {



                    continue;
                }
                const bool has_model_hash = p.model_path_hash != 0;
                if (p.entity_name.empty() && !has_model_hash) continue;
                std::string tok = canonicalize_for_match(p.entity_name);
                if (tok.empty() && !has_model_hash) continue;
                const std::string entity_key = gdb_entity_key(p.entity_name);
                std::string gdb_interest_category =
                    classify_gdb_interest(entity_key, tok, nullptr);
                const bool clocktower_audit =
                    p.parent_hash == 0xD55304DB ||
                    entity_key.find("clocktower") != std::string::npos ||
                    tok.find("clocktower") != std::string::npos;
                if (clocktower_audit) {
                    ++gdb_clocktower_seen;
                }
                bool shop_audit = is_market_shop_key(entity_key) ||
                                  is_market_shop_key(tok);
                if (shop_audit) {
                    ++gdb_shop_seen;
                }
                if (is_bwsmarket_level &&
                    p.model_path_hash == 0 &&
                    has_worldish_gdb_position(p) &&
                    is_nohash_market_shell_key(entity_key))
                {
                    gdb_nohash_shell_candidates.push_back(
                        {p, entity_key, gdb_interest_category});
                    add_gdb_interest_row(
                        p, entity_key, gdb_interest_category,
                        "candidate_nohash_shell",
                        std::string());
                    continue;
                }
                const FlatAssetEntry* hit = nullptr;
                bool matched_model = false;
                bool matched_model_path_hash = false;
                if (p.model_path_hash != 0) {
                    hit = resolve_model_by_path_hash(p.model_path_hash);
                    if (hit) {
                        matched_model = true;
                        matched_model_path_hash = true;
                        ++gdb_model_hash_hits;
                    } else {
                        ++gdb_model_hash_misses;
                    }
                }

                if (!matched_model) {
                    const char* curated_path =
                        GdbModelHashlist::LookupParentHash(p.parent_hash);
                    if (!curated_path) {
                        curated_path =
                            GdbModelHashlist::LookupEntityKey(entity_key);
                    }
                    if (curated_path && *curated_path) {
                        hit = resolve_model_by_lower_path(
                            lower_slash(curated_path));
                        if (hit) {
                            matched_model = true;
                        }
                    }
                }

                if (!matched_model &&
                    (entity_key == "bsmarkettavern" ||
                     tok.find("bsmarkettavern") != std::string::npos))
                {
                    if (has_worldish_gdb_position(p)) {
                        gdb_nohash_shell_candidates.push_back(
                            {p, "bsmarkettavern", gdb_interest_category});
                    }
                    add_gdb_interest_row(
                        p, entity_key, gdb_interest_category,
                        "skipped_unverified_tavern_shell_guess",
                        std::string());
                    continue;
                }
                if (!matched_model) {
                    hit = resolve_model_for_entity(p.entity_name);
                    if (!hit) {
                        if (is_bwsmarket_level && has_worldish_gdb_position(p) &&
                            (entity_key == "generalstore" ||
                             entity_key == "generalstore1"))
                        {
                            gdb_nohash_shell_candidates.push_back(
                                {p, entity_key, gdb_interest_category});
                        }
                        add_gdb_interest_row(
                            p, entity_key, gdb_interest_category,
                            "unresolved", std::string());
                        if (clocktower_audit &&
                            gdb_clocktower_audit_lines.size() < 8)
                        {
                            gdb_clocktower_audit_lines.push_back(
                                "clocktower audit unresolved: " +
                                gdb_shell_sample_text(p, "<no model>"));
                        }
                        if (shop_audit) {
                            ++gdb_shop_unresolved;
                            if (gdb_shop_audit_lines.size() < 12) {
                                gdb_shop_audit_lines.push_back(
                                    "shop audit unresolved: " +
                                    gdb_shell_sample_text(p, "<no model>"));
                            }
                        }
                        continue;
                    }
                    matched_model = true;
                }

                if (!matched_model) {
                    add_gdb_interest_row(
                        p, entity_key, gdb_interest_category,
                        "unmatched", std::string());
                    continue;
                }
                if (hit) {
                    if (is_bridge_debug_path(hit->full_path)) {
                        std::ostringstream bridge;
                        bridge << "GDB CANDIDATE entity=\"" << p.entity_name
                               << "\" entity_key=\"" << entity_key
                               << "\" entity_hash=0x" << std::hex << p.hash_a
                               << " parent_hash=0x" << p.parent_hash
                               << " model_path_hash=0x" << p.model_path_hash
                               << std::dec << " indexed=" << p.indexed_record
                               << " exact_model_hash="
                               << matched_model_path_hash
                               << " pos=(" << p.x << ", " << p.y << ", "
                               << p.z << ") scale=" << p.scale
                               << " resolved_model=" << hit->full_path;
                        bridge_debug_write(bridge.str());
                    }




                    if (is_bridge_debug_path(hit->full_path) &&
                        !matched_model_path_hash) {
                        add_gdb_interest_row(
                            p, entity_key, gdb_interest_category,
                            "rejected_fuzzy_bridge_match", hit->full_path);
                        bridge_debug_write(
                            "REJECTED FUZZY BRIDGE CANDIDATE entity=\"" +
                            p.entity_name + "\" model=" + hit->full_path);
                        continue;
                    }
                    const std::string path_category =
                        classify_gdb_interest(
                            entity_key, tok, &hit->full_path);
                    if (!path_category.empty()) {
                        gdb_interest_category = path_category;
                    }
                }
                if (hit && is_gdb_static_prop_reject_model(hit->full_path)) {
                    add_gdb_interest_row(
                        p, entity_key, gdb_interest_category,
                        "rejected_non_static_model", hit->full_path);
                    continue;
                }
                if (hit && !matched_model_path_hash &&
                    g_level_entity_contents.count(p.hash_a) != 0 &&
                    is_implausible_container_shell_model(hit->full_path)) {
                    add_gdb_interest_row(
                        p, entity_key, gdb_interest_category,
                        "rejected_container_shell_model", hit->full_path);
                    continue;
                }
                if (hit && !matched_model_path_hash &&
                    is_bad_market_helper_substitution(
                        entity_key, tok, hit->full_path))
                {
                    add_gdb_interest_row(
                        p, entity_key, gdb_interest_category,
                        "rejected_helper_substitute", hit->full_path);
                    if (shop_audit) {
                        ++gdb_shop_unresolved;
                        if (gdb_shop_audit_lines.size() < 12) {
                            gdb_shop_audit_lines.push_back(
                                "shop audit rejected helper substitute: " +
                                gdb_shell_sample_text(p, hit->full_path));
                        }
                    }
                    continue;
                }
                ++resolved;
                if (!shop_audit && hit && is_market_shop_path(hit->full_path)) {
                    shop_audit = true;
                    ++gdb_shop_seen;
                }
                if (!hit) {
                    add_gdb_interest_row(
                        p, entity_key, gdb_interest_category,
                        "unresolved_model_asset", std::string());
                    if (clocktower_audit &&
                        gdb_clocktower_audit_lines.size() < 8)
                    {
                        gdb_clocktower_audit_lines.push_back(
                            "clocktower audit unresolved model asset: " +
                            gdb_shell_sample_text(p, "<no model asset>"));
                    }
                    if (shop_audit) {
                        if (gdb_shop_audit_lines.size() < 12) {
                            gdb_shop_audit_lines.push_back(
                                "shop audit unresolved model asset: " +
                                gdb_shell_sample_text(p, "<no model asset>"));
                        }
                    }
                    continue;
                }
                const FlatAssetEntry* clocktower_platform_companion = nullptr;
                if (clocktower_audit) {
                    const std::string primary_path = lower_slash(hit->full_path);
                    if (!bwsmarket_has_explicit_clocktower_base_record &&
                        primary_path.find("bs_market_platform") !=
                            std::string::npos)
                    {
                        const char* tower_path =
                            GdbModelHashlist::LookupParentHash(0xD55304DB);
                        const FlatAssetEntry* tower_hit =
                            resolve_model_by_lower_path(
                                lower_slash(tower_path ? tower_path : ""));
                        if (tower_hit) {
                            clocktower_platform_companion = hit;
                            hit = tower_hit;
                        }
                    }
                }






                const bool unique_entity_shell =
                    is_gdb_unique_entity_shell_model(hit->full_path);
                if (unique_entity_shell && !has_worldish_gdb_position(p)) {
                    add_gdb_interest_row(
                        p, entity_key, gdb_interest_category,
                        "skipped_non_world_shell_position", hit->full_path);
                    ++gdb_shell_bad_position_skipped;
                    ++gdb_shell_bad_position_paths[hit->full_path];
                    if (clocktower_audit &&
                        gdb_clocktower_audit_lines.size() < 8)
                    {
                        gdb_clocktower_audit_lines.push_back(
                            "clocktower audit skipped non-world shell position: " +
                            gdb_shell_sample_text(p, hit->full_path));
                    }
                    if (shop_audit) {
                        ++gdb_shop_duplicates;
                        if (gdb_shop_audit_lines.size() < 12) {
                            gdb_shop_audit_lines.push_back(
                                "shop audit skipped non-world shell position: " +
                                gdb_shell_sample_text(p, hit->full_path));
                        }
                    }
                    continue;
                }

                const std::string instance_key =
                    gdb_instance_key(p, hit->full_path);
                if (!gdb_emitted_instance_keys.insert(instance_key).second) {
                    add_gdb_interest_row(
                        p, entity_key, gdb_interest_category,
                        "skipped_duplicate_gdb_record", hit->full_path);
                    ++gdb_duplicate_instances_skipped;
                    ++gdb_duplicate_skip_paths[hit->full_path];
                    if (clocktower_audit &&
                        gdb_clocktower_audit_lines.size() < 8)
                    {
                        gdb_clocktower_audit_lines.push_back(
                            "clocktower audit skipped duplicate gdb record: " +
                            gdb_shell_sample_text(p, hit->full_path));
                    }
                    if (shop_audit) {
                        ++gdb_shop_duplicates;
                        if (gdb_shop_audit_lines.size() < 12) {
                            gdb_shop_audit_lines.push_back(
                                "shop audit skipped duplicate gdb record: " +
                                gdb_shell_sample_text(p, hit->full_path));
                        }
                    }
                    continue;
                }

                auto& pb = blocks_by_path[hit->full_path];
                if (pb.model_path.empty()) {
                    pb.type = 0xB1;
                    pb.model_path = hit->full_path;
                }

                Level::PropInstance pi;
                pi.hash = p.hash_a;
                pi.gdb_entity_hash = p.hash_a;
                pi.values[0] = p.x;
                pi.values[1] = p.y;
                pi.values[2] = p.z;
                pi.gdb_pos_off[0] = p.pos_value_off[0];
                pi.gdb_pos_off[1] = p.pos_value_off[1];
                pi.gdb_pos_off[2] = p.pos_value_off[2];
                pi.gdb_rot_off[0] = p.rot_value_off[0];
                pi.gdb_rot_off[1] = p.rot_value_off[1];
                pi.gdb_rot_off[2] = p.rot_value_off[2];
                const float scale =
                    (std::isfinite(p.scale) && p.scale > 0.01f && p.scale < 100.0f)
                        ? p.scale : 1.0f;
                if (p.has_rotation) {
                    const bool pi_pair_yaw =
                        is_gdb_pi_pair_yaw_rotation(p.rot_y, p.rot_z);
                    if (pi_pair_yaw) {
                        ++gdb_pi_pair_yaw_rotations;
                    }
                    fill_gdb_rotation_matrix(pi, p.rot_x, p.rot_y, p.rot_z, scale);
                    if (pi_pair_yaw) {

                    } else if (std::fabs(p.rot_y) > 1e-4f ||
                               std::fabs(p.rot_z) > 1e-4f) {
                        ++gdb_full_euler_rotations;
                    } else if (std::fabs(p.rot_x) > 1e-4f) {
                        ++gdb_yaw_only_rotations;
                    } else {
                        ++gdb_identity_rotations;
                    }
                } else {
                    const float s_yaw = std::sin(p.yaw);
                    const float c_yaw = std::cos(p.yaw);
                    if (std::isfinite(s_yaw) && std::isfinite(c_yaw)) {
                        pi.values[6] = s_yaw;
                        pi.values[7] = c_yaw;
                    } else {
                        pi.values[6] = 0.0f;
                        pi.values[7] = 1.0f;
                    }
                    pi.values[9] = pi.values[10] = pi.values[11] = scale;
                    if (std::fabs(p.yaw) > 1e-4f) {
                        ++gdb_yaw_only_rotations;
                    } else {
                        ++gdb_identity_rotations;
                    }
                }
                if (!emitted_prop_transform_keys.insert(
                        prop_instance_transform_key(pi, hit->full_path)).second)
                {
                    record_gdb_dup_offsets(pi, hit->full_path);
                    add_gdb_interest_row(
                        p, entity_key, gdb_interest_category,
                        "skipped_existing_prop_transform", hit->full_path);
                    ++gdb_duplicate_instances_skipped;
                    ++gdb_duplicate_skip_paths[hit->full_path];
                    if (clocktower_audit &&
                        gdb_clocktower_audit_lines.size() < 8)
                    {
                        gdb_clocktower_audit_lines.push_back(
                            "clocktower audit skipped existing prop transform: " +
                            gdb_shell_sample_text(p, hit->full_path));
                    }
                    if (shop_audit) {
                        ++gdb_shop_duplicates;
                        if (gdb_shop_audit_lines.size() < 12) {
                            gdb_shop_audit_lines.push_back(
                                "shop audit skipped existing prop transform: " +
                                gdb_shell_sample_text(p, hit->full_path));
                        }
                    }
                    continue;
                }
                pb.instances.push_back(pi);
                emit_gmd_layout_children_for_model(hit, pi);
                add_gdb_interest_row(
                    p, entity_key, gdb_interest_category,
                    "emitted", hit->full_path);
                if (is_bwsmarket_level) {
                    const std::string emitted_path =
                        lower_slash(hit->full_path);
                    if (emitted_path.find(
                            "bs_market_generalshop_stairs_floor") !=
                        std::string::npos)
                    {
                        gdb_generalshop_floor_anchors.push_back(pi);
                    }
                    if (emitted_path.find("esa_table_tavern") !=
                        std::string::npos)
                    {
                        gdb_tavern_pub_anchors.push_back(pi);
                    }
                }
                if (clocktower_audit) {
                    ++gdb_clocktower_emitted;
                    if (gdb_clocktower_audit_lines.size() < 8) {
                        gdb_clocktower_audit_lines.push_back(
                            "clocktower audit emitted: " +
                            gdb_shell_sample_text(p, hit->full_path));
                    }
                    const std::string primary_path =
                        lower_slash(hit->full_path);
                    const bool primary_is_clocktower_base =
                        primary_path.find(
                            "bs_market_clocktower/"
                            "bs_market_clocktower.mdl") !=
                            std::string::npos;
                    const bool primary_is_platform =
                        primary_path.find(
                            "bs_market_platform/"
                            "bs_market_platform.mdl") !=
                            std::string::npos;
                    const bool clocktower_base_primary =
                        primary_is_clocktower_base ||
                        (primary_is_platform &&
                         !bwsmarket_has_explicit_clocktower_base_record);
                    if (clocktower_base_primary) {
                        if (clocktower_platform_companion) {
                            if (append_prop_instance_for_model(
                                    clocktower_platform_companion, pi))
                            {
                                ++gdb_clocktower_companions_emitted;
                                if (gdb_clocktower_audit_lines.size() < 8) {
                                    gdb_clocktower_audit_lines.push_back(
                                        "clocktower platform companion emitted: " +
                                        gdb_shell_sample_text(
                                            p,
                                            clocktower_platform_companion->full_path));
                                }
                            }
                        } else if (!bwsmarket_has_explicit_clocktower_base_record &&
                                   primary_path.find("bs_market_clocktower") ==
                                       std::string::npos)
                        {
                            const char* tower_path =
                                GdbModelHashlist::LookupParentHash(0xD55304DB);
                            const FlatAssetEntry* tower_hit =
                                resolve_model_by_lower_path(
                                    lower_slash(tower_path ? tower_path : ""));
                            if (append_prop_instance_for_model(tower_hit, pi)) {
                                ++gdb_clocktower_companions_emitted;
                                if (gdb_clocktower_audit_lines.size() < 8) {
                                    gdb_clocktower_audit_lines.push_back(
                                        "clocktower companion emitted: " +
                                        gdb_shell_sample_text(
                                            p, tower_hit->full_path));
                                }
                            } else if (!tower_hit &&
                                       gdb_clocktower_audit_lines.size() < 8)
                            {
                                gdb_clocktower_audit_lines.push_back(
                                    "clocktower companion unresolved: " +
                                    gdb_shell_sample_text(
                                        p, tower_path ? tower_path : "<no path>"));
                            }
                        }

                        const std::array<const char*, 3> clocktower_parts = {
                            "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_ClockTower_Cogs\\BS_Market_ClockTower_Cogs.mdl",
                            "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_ClockTower_HourHand\\BS_Market_ClockTower_HourHand.mdl",
                            "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_ClockTower_MinuteHand\\BS_Market_ClockTower_MinuteHand.mdl",
                        };
                        for (const char* part_path : clocktower_parts) {
                            const std::string part_lower =
                                lower_slash(part_path);
                            if (primary_path == part_lower) {
                                continue;
                            }
                            const FlatAssetEntry* part_hit =
                                resolve_model_by_lower_path(part_lower);
                            if (append_prop_instance_for_model(part_hit, pi)) {
                                ++gdb_clocktower_companions_emitted;
                                if (gdb_clocktower_audit_lines.size() < 8) {
                                    gdb_clocktower_audit_lines.push_back(
                                        "clocktower part emitted: " +
                                        gdb_shell_sample_text(
                                            p, part_hit->full_path));
                                }
                            } else if (!part_hit &&
                                       gdb_clocktower_audit_lines.size() < 8)
                            {
                                gdb_clocktower_audit_lines.push_back(
                                    "clocktower part unresolved: " +
                                    gdb_shell_sample_text(p, part_path));
                            }
                        }
                    }
                }
                if (shop_audit) {
                    ++gdb_shop_emitted;
                    if (gdb_shop_audit_lines.size() < 12) {
                        gdb_shop_audit_lines.push_back(
                            "shop audit emitted: " +
                            gdb_shell_sample_text(p, hit->full_path));
                    }
                }
                std::vector<std::pair<std::string, bool>>
                    shell_companion_paths = {
                        {companion_interior_path(hit->full_path), true},
                        {companion_exterior_path(hit->full_path), false},
                    };
                if (is_bwsslums_level) {
                    const std::string house_exterior =
                        house_facade_companion_exterior_path(hit->full_path);
                    if (!house_exterior.empty()) {
                        shell_companion_paths.push_back(
                            {house_exterior, false});
                        shell_companion_paths.push_back(
                            {companion_interior_path(house_exterior), true});
                    }
                }
                for (const auto& companion : shell_companion_paths) {
                    if (companion.first.empty()) continue;
                    const FlatAssetEntry* companion_hit =
                        resolve_model_by_lower_path(companion.first);
                    if (!companion_hit) continue;
                    auto& companion_pb =
                        blocks_by_path[companion_hit->full_path];
                    if (companion_pb.model_path.empty()) {
                        companion_pb.type = 0xB1;
                        companion_pb.model_path = companion_hit->full_path;
                    }
                    if (emitted_prop_transform_keys.insert(
                            prop_instance_transform_key(
                                pi, companion_hit->full_path)).second)
                    {
                        companion_pb.instances.push_back(pi);
                        emit_gmd_layout_children_for_model(companion_hit, pi);
                        if (companion.second) {
                            ++gdb_companion_interiors_emitted;
                            ++gdb_companion_interior_paths[
                                companion_hit->full_path];
                        } else {
                            ++gdb_companion_exteriors_emitted;
                            ++gdb_companion_exterior_paths[
                                companion_hit->full_path];
                        }
                    }
                }
                if (is_gdb_shell_audit_model(hit->full_path)) {
                    ++gdb_emitted_shell_paths[hit->full_path];
                    auto& samples = gdb_emitted_shell_samples[hit->full_path];
                    if (samples.size() < 4) {
                        samples.push_back(
                            gdb_shell_sample_text(p, hit->full_path));
                    }
                }
                ++gdb_instances_emitted;
            }

            if (is_bwsmarket_level) {
                struct WorldAnchor {
                    std::string model_path;
                    std::string key;
                    Xform3f xf;
                };
                std::unordered_map<std::string, std::vector<WorldAnchor>>
                    world_anchors_by_key;
                auto add_world_anchor =
                    [&](const std::string& model_path,
                        const Level::PropInstance& inst) {
                        const std::string key =
                            compact_match_key(model_name_from_path(model_path));
                        if (key.empty()) return;
                        world_anchors_by_key[key].push_back(
                            {model_path, key, prop_instance_xform(inst)});
                    };
                for (const auto& block : level_prop_blocks) {
                    for (const auto& inst : block.instances) {
                        add_world_anchor(block.model_path, inst);
                    }
                }
                for (const auto& kv : blocks_by_path) {
                    const auto& block = kv.second;
                    for (const auto& inst : block.instances) {
                        add_world_anchor(block.model_path, inst);
                    }
                }

                std::unordered_map<std::string,
                                   std::vector<const FlatAssetEntry*>>
                    mdl_by_asset_key;
                mdl_by_asset_key.reserve(S.all_mdl_files.size());
                for (const auto& m : S.all_mdl_files) {
                    const std::string key =
                        compact_match_key(model_name_from_path(m.full_path));
                    if (!key.empty()) {
                        mdl_by_asset_key[key].push_back(&m);
                    }
                }
                auto choose_model_for_gmd_asset =
                    [&](const std::string& key) {
                    if (key.empty()) {
                        return static_cast<const FlatAssetEntry*>(nullptr);
                    }
                    auto choose_best =
                        [](const std::vector<const FlatAssetEntry*>& hits) {
                        const FlatAssetEntry* best = nullptr;
                        int best_score = INT_MIN;
                        for (const FlatAssetEntry* e : hits) {
                            if (!e) continue;
                            int score = 0;
                            const std::string p = lower_slash(e->full_path);
                            if (p.find("/globals_models.bnk") ==
                                std::string::npos)
                            {
                                score += 500;
                            }
                            if (e->from_nested) score += 250;
                            if (p.find("/doors_windows/") !=
                                std::string::npos)
                            {
                                score += 200;
                            }
                            if (p.find("/props/") != std::string::npos) {
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
                    if (auto it = mdl_by_asset_key.find(key);
                        it != mdl_by_asset_key.end())
                    {
                        return choose_best(it->second);
                    }
                    std::vector<const FlatAssetEntry*> fuzzy;
                    for (const auto& kv : mdl_by_asset_key) {
                        const std::string& mk = kv.first;
                        if (mk.size() < 5) continue;
                        if (mk.find(key) == std::string::npos &&
                            key.find(mk) == std::string::npos)
                        {
                            continue;
                        }
                        fuzzy.insert(fuzzy.end(),
                                     kv.second.begin(),
                                     kv.second.end());
                    }
                    return choose_best(fuzzy);
                };

                auto shell_candidate_for_path =
                    [&](const char* exterior_path) {
                    const std::string target = lower_slash(exterior_path);
                    const StreamingModelCandidate* best = nullptr;
                    for (const auto& c : streaming_model_candidates) {
                        if (!c.from_gmd || c.gmd_file_index < 0 ||
                            c.gmd_bnk_path.empty())
                        {
                            continue;
                        }
                        if (c.hint_lower == target ||
                            c.resolved_lower == target)
                        {
                            return &c;
                        }
                        if (c.hint_lower.size() > target.size() &&
                            c.hint_lower.compare(
                                c.hint_lower.size() - target.size(),
                                target.size(), target) == 0)
                        {
                            best = &c;
                        }
                    }
                    return best;
                };

                struct GmdShellSolution {
                    Xform3f xf;
                    int matches = 0;
                    int distinct_matches = 0;
                    float error = 0.0f;
                    std::string seed;
                };
                auto is_distinct_anchor_key =
                    [&](const std::string& key) {
                    auto it = world_anchors_by_key.find(key);
                    const size_t count =
                        (it == world_anchors_by_key.end())
                            ? 0 : it->second.size();
                    return count <= 16 ||
                           key.find("sign") != std::string::npos ||
                           key.find("door") != std::string::npos ||
                           key.find("counter") != std::string::npos ||
                           key.find("stairs") != std::string::npos ||
                           key.find("tarot") != std::string::npos;
                };
                auto plausible_shell_xform =
                    [](const Xform3f& xf) {
                    return std::isfinite(xf.t.x) &&
                           std::isfinite(xf.t.y) &&
                           std::isfinite(xf.t.z) &&
                           xf.t.x >= -96.0f && xf.t.x <= 512.0f &&
                           xf.t.z >= -96.0f && xf.t.z <= 512.0f &&
                           xf.t.y >= -96.0f && xf.t.y <= 256.0f;
                };
                auto score_gmd_shell_seed =
                    [&](const Xform3f& seed,
                        const std::vector<GmdLayoutChild>& children,
                        const std::string& seed_label) {
                    GmdShellSolution sol;
                    sol.xf = seed;
                    sol.seed = seed_label;
                    if (!plausible_shell_xform(seed)) {
                        sol.error = std::numeric_limits<float>::infinity();
                        return sol;
                    }
                    for (const auto& child : children) {
                        if (child.resolved_key.empty()) continue;
                        auto it =
                            world_anchors_by_key.find(child.resolved_key);
                        if (it == world_anchors_by_key.end() ||
                            it->second.empty())
                        {
                            continue;
                        }
                        const Vec3f predicted =
                            xform_apply_point(seed, child.local.t);
                        float best_d2 =
                            std::numeric_limits<float>::infinity();
                        for (const auto& anchor : it->second) {
                            const float d2 =
                                vec3_len2(vec3_sub(predicted, anchor.xf.t));
                            if (d2 < best_d2) best_d2 = d2;
                        }
                        if (!std::isfinite(best_d2)) continue;
                        const float d = std::sqrt(best_d2);
                        if (d <= 2.5f) {
                            ++sol.matches;
                            sol.error += d;
                            if (is_distinct_anchor_key(child.resolved_key)) {
                                ++sol.distinct_matches;
                            }
                        }
                    }
                    return sol;
                };
                auto solve_gmd_shell =
                    [&](const char* exterior_path,
                        const char* label,
                        size_t max_solutions) {
                    std::vector<GmdShellSolution> selected;
                    const StreamingModelCandidate* cand =
                        shell_candidate_for_path(exterior_path);
                    if (!cand) {
                        return selected;
                    }

                    std::vector<uint8_t> bytes;
                    try {
                        bytes = BnkCache::extract_bytes(
                            cand->gmd_bnk_path, cand->gmd_file_index);
                    } catch (...) {
                    }
                    std::vector<GmdLayoutChild> children =
                        parse_gmd_layout_children(bytes);
                    for (auto& child : children) {
                        if (const FlatAssetEntry* hit =
                                choose_model_for_gmd_asset(child.asset_key))
                        {
                            child.resolved_path = hit->full_path;
                            child.resolved_key =
                                compact_match_key(
                                    model_name_from_path(hit->full_path));
                        }
                    }

                    std::vector<GmdShellSolution> scored;
                    for (const auto& child : children) {
                        if (child.resolved_key.empty()) continue;
                        auto it =
                            world_anchors_by_key.find(child.resolved_key);
                        if (it == world_anchors_by_key.end() ||
                            it->second.empty())
                        {
                            continue;
                        }
                        Xform3f local_inv;
                        if (!xform_inverse(child.local, local_inv)) {
                            continue;
                        }
                        const bool distinct =
                            is_distinct_anchor_key(child.resolved_key);
                        const size_t max_seed_count = distinct ? 512 : 96;
                        const size_t stride =
                            (it->second.size() > max_seed_count)
                                ? std::max<size_t>(
                                      1, it->second.size() / max_seed_count)
                                : 1;
                        size_t used = 0;
                        for (size_t i = 0; i < it->second.size();
                             i += stride)
                        {
                            if (used++ >= max_seed_count) break;
                            const auto& anchor = it->second[i];
                            const Xform3f seed =
                                xform_compose(anchor.xf, local_inv);
                            const std::string seed_label =
                                child.resolved_key + " -> " +
                                anchor.model_path;
                            GmdShellSolution sol =
                                score_gmd_shell_seed(
                                    seed, children, seed_label);
                            if (sol.matches <= 0) continue;
                            scored.push_back(std::move(sol));
                        }
                    }

                    std::sort(scored.begin(), scored.end(),
                              [](const auto& a, const auto& b) {
                                  const int as = a.distinct_matches * 2000 +
                                                 a.matches * 1000;
                                  const int bs = b.distinct_matches * 2000 +
                                                 b.matches * 1000;
                                  if (as != bs) return as > bs;
                                  return a.error < b.error;
                              });

                    for (const auto& sol : scored) {
                        if (sol.matches < 2 && sol.distinct_matches < 1) {
                            continue;
                        }
                        bool duplicate = false;
                        for (const auto& prev : selected) {
                            const Vec3f d =
                                vec3_sub(sol.xf.t, prev.xf.t);
                            if (d.x * d.x + d.z * d.z < 16.0f &&
                                std::fabs(d.y) < 6.0f)
                            {
                                duplicate = true;
                                break;
                            }
                        }
                        if (duplicate) continue;
                        selected.push_back(sol);
                        if (selected.size() >= max_solutions) break;
                    }

                    return selected;
                };

                auto emit_gmd_solved_shells =
                    [&](const char* exterior_path,
                        const char* label,
                        size_t max_solutions) {
                    size_t emitted = 0;
                    std::vector<GmdShellSolution> solutions =
                        solve_gmd_shell(exterior_path, label, max_solutions);
                    const std::string exterior_lower =
                        lower_slash(exterior_path);
                    const std::array<std::string, 2> shell_paths = {
                        exterior_lower,
                        companion_interior_path(exterior_lower),
                    };
                    for (const auto& sol : solutions) {
                        const Level::PropInstance shell_pi =
                            prop_instance_from_xform(sol.xf);
                        bool emitted_any = false;
                        for (const std::string& shell_path : shell_paths) {
                            if (shell_path.empty()) continue;
                            if (append_prop_instance_for_model_path(
                                    shell_path, shell_pi))
                            {
                                emitted_any = true;
                                ++gdb_nohash_shell_companions_emitted;
                                ++gdb_nohash_shell_companion_paths[shell_path];
                            } else if (!resolve_model_by_lower_path(
                                           shell_path))
                            {
                                ++gdb_nohash_shell_companion_misses;
                            }
                        }
                        if (emitted_any) {
                            ++emitted;
                            ++gdb_instances_emitted;
                        }
                    }
                    return emitted;
                };

                constexpr bool enable_gmd_shell_solve = false;
                const size_t solved_general =
                    enable_gmd_shell_solve
                        ? emit_gmd_solved_shells(
                              "art/environment/regions/bowerstone/buildings/"
                              "dotxsi/bs_market_generalshop/"
                              "bs_market_generalshop/exterior.mdl",
                              "generalshop",
                              2)
                        : 0;
                const size_t solved_tavern =
                    enable_gmd_shell_solve
                        ? emit_gmd_solved_shells(
                              "art/environment/regions/bowerstone/buildings/"
                              "dotxsi/bs_market_tavern/"
                              "bs_market_tavern/exterior.mdl",
                              "tavern",
                              1)
                        : 0;
                if (solved_general > 0 || solved_tavern > 0) {
                    OutputLog::info(
                        "GDB .gmd shell solve: generalshop=" +
                        std::to_string(solved_general) +
                        ", tavern=" +
                        std::to_string(solved_tavern));
                }

            }

            const bool allow_legacy_nohash_shell_anchor = true;
            if (allow_legacy_nohash_shell_anchor &&
                is_bwsmarket_level && !gdb_nohash_shell_candidates.empty()) {
                std::unordered_set<size_t> used_nohash_shell_candidates;
                auto find_nearest_nohash_shell =
                    [&](const std::initializer_list<const char*> entity_keys,
                        float x,
                        float y,
                        float z,
                        float max_dist,
                        float max_dz,
                        bool prefer_parent_backed) -> size_t {
                        size_t best = static_cast<size_t>(-1);
                        float best_score =
                            std::numeric_limits<float>::infinity();
                        auto consider = [&](bool require_parent_backed) {
                            for (size_t i = 0;
                                 i < gdb_nohash_shell_candidates.size(); ++i)
                            {
                                if (used_nohash_shell_candidates.find(i) !=
                                    used_nohash_shell_candidates.end())
                                {
                                    continue;
                                }
                                const NoHashShellCandidate& c =
                                    gdb_nohash_shell_candidates[i];
                                if (require_parent_backed &&
                                    c.placement.parent_hash == 0)
                                {
                                    continue;
                                }
                                bool key_match = false;
                                for (const char* key : entity_keys) {
                                    if (c.entity_key == key) {
                                        key_match = true;
                                        break;
                                    }
                                }
                                if (!key_match) continue;
                                const float dx = c.placement.x - x;
                                const float dy = c.placement.y - y;
                                const float dz = c.placement.z - z;
                                if (std::fabs(dz) > max_dz) continue;
                                const float dxy2 = dx * dx + dy * dy;
                                if (dxy2 > max_dist * max_dist) continue;
                                const float score = dxy2 + dz * dz * 9.0f;
                                if (score < best_score) {
                                    best_score = score;
                                    best = i;
                                }
                            }
                        };
                        if (prefer_parent_backed) {
                            consider(true);
                        }
                        if (best == static_cast<size_t>(-1)) {
                            consider(false);
                        }
                        return best;
                    };
                auto nohash_shell_placement_with_rotation =
                    [&](const NoHashShellCandidate& candidate) {
                        Gdb::Placement placement = candidate.placement;
                        if (placement.has_rotation) return placement;

                        float best_d2 = 64.0f;
                        const NoHashShellCandidate* best = nullptr;
                        for (const NoHashShellCandidate& other :
                             gdb_nohash_shell_candidates)
                        {
                            if (other.entity_key != candidate.entity_key ||
                                !other.placement.has_rotation)
                            {
                                continue;
                            }
                            const float dx =
                                other.placement.x - placement.x;
                            const float dy =
                                other.placement.y - placement.y;
                            const float d2 = dx * dx + dy * dy;
                            if (d2 < best_d2) {
                                best_d2 = d2;
                                best = &other;
                            }
                        }
                        if (best) {
                            placement.rot_x = best->placement.rot_x;
                            placement.rot_y = best->placement.rot_y;
                            placement.rot_z = best->placement.rot_z;
                            placement.yaw = best->placement.yaw;
                            placement.has_rotation = true;
                        }
                        return placement;
                    };
                auto emit_nohash_shell_pair =
                    [&](const NoHashShellCandidate& candidate,
                        const char* exterior_path) {
                        if (!exterior_path || !*exterior_path) return;
                        const Gdb::Placement shell_placement =
                            nohash_shell_placement_with_rotation(candidate);
                        const Level::PropInstance shell_pi =
                            make_gdb_prop_instance_no_count(
                                shell_placement);
                        const std::string exterior_lower =
                            lower_slash(exterior_path);
                        std::array<std::string, 2> shell_paths = {
                            exterior_lower,
                            companion_interior_path(exterior_lower),
                        };
                        bool emitted_any = false;
                        for (const std::string& shell_path : shell_paths) {
                            if (shell_path.empty()) continue;
                            const FlatAssetEntry* shell_hit =
                                resolve_model_by_lower_path(shell_path);
                            if (append_prop_instance_for_model(
                                    shell_hit, shell_pi))
                            {
                                emitted_any = true;
                                emit_gmd_layout_children_for_model(
                                    shell_hit, shell_pi);
                                ++gdb_nohash_shell_companions_emitted;
                                ++gdb_nohash_shell_companion_paths[shell_path];
                                add_gdb_interest_row(
                                    shell_placement,
                                    candidate.entity_key,
                                    candidate.category,
                                    "emitted_nohash_shell_companion",
                                    shell_path);
                            } else if (!shell_hit)
                            {
                                ++gdb_nohash_shell_companion_misses;
                            }
                        }
                        if (emitted_any) {
                            ++gdb_instances_emitted;
                        }
                    };





                for (size_t i = 0; i < gdb_nohash_shell_candidates.size(); ++i) {
                    const NoHashShellCandidate& candidate =
                        gdb_nohash_shell_candidates[i];
                    const char* exterior = nullptr;
                    if (candidate.entity_key == "bsmarkettavern") {
                        exterior =
                            "art/environment/regions/bowerstone/buildings/"
                            "dotxsi/bs_market_tavern/"
                            "bs_market_tavern/exterior.mdl";
                    } else if (candidate.entity_key == "generalstore" ||
                               candidate.entity_key == "generalstore1") {
                        exterior =
                            "art/environment/regions/bowerstone/buildings/"
                            "dotxsi/bs_market_generalshop/"
                            "bs_market_generalshop/exterior.mdl";
                    }
                    if (!exterior) continue;
                    used_nohash_shell_candidates.insert(i);
                    emit_nohash_shell_pair(candidate, exterior);
                }

                for (const auto& anchor : gdb_generalshop_floor_anchors) {
                    const size_t idx = find_nearest_nohash_shell(
                        {"generalstore", "generalstore1"},
                        anchor.values[0], anchor.values[1], anchor.values[2],
                        18.0f, 5.0f, true);
                    if (idx == static_cast<size_t>(-1)) continue;
                    used_nohash_shell_candidates.insert(idx);
                    emit_nohash_shell_pair(
                        gdb_nohash_shell_candidates[idx],
                        "art/environment/regions/bowerstone/buildings/"
                        "dotxsi/bs_market_generalshop/"
                        "bs_market_generalshop/exterior.mdl");
                }

                if (!gdb_tavern_pub_anchors.empty()) {
                    float tavern_x = 0.0f;
                    float tavern_y = 0.0f;
                    float tavern_z = 0.0f;
                    size_t tavern_anchor_count = 0;
                    for (const auto& anchor : gdb_tavern_pub_anchors) {
                        if (anchor.values[2] > 43.0f) continue;
                        tavern_x += anchor.values[0];
                        tavern_y += anchor.values[1];
                        tavern_z += anchor.values[2];
                        ++tavern_anchor_count;
                    }
                    if (tavern_anchor_count == 0) {
                        for (const auto& anchor : gdb_tavern_pub_anchors) {
                            tavern_x += anchor.values[0];
                            tavern_y += anchor.values[1];
                            tavern_z += anchor.values[2];
                            ++tavern_anchor_count;
                        }
                    }
                    tavern_x /= static_cast<float>(tavern_anchor_count);
                    tavern_y /= static_cast<float>(tavern_anchor_count);
                    tavern_z /= static_cast<float>(tavern_anchor_count);
                    const size_t idx = find_nearest_nohash_shell(
                        {"bsmarkettavern"}, tavern_x, tavern_y, tavern_z,
                        25.0f, 4.0f, false);
                    if (idx != static_cast<size_t>(-1)) {
                        used_nohash_shell_candidates.insert(idx);
                        emit_nohash_shell_pair(
                            gdb_nohash_shell_candidates[idx],
                            "art/environment/regions/bowerstone/buildings/"
                            "dotxsi/bs_market_tavern/"
                            "bs_market_tavern/exterior.mdl");
                    }
                }
            }

            std::ostringstream os3;
            os3 << "gdb-derived placements: "
                << resolved << " entities matched a model";
            if (gdb_instances_emitted > 0) {
                os3 << ", emitted " << gdb_instances_emitted
                    << " instance(s)";
                OutputLog::success(os3.str());
                OutputLog::info(
                    "gdb-derived rotations: full-euler=" +
                    std::to_string(gdb_full_euler_rotations) +
                    ", yaw-only=" +
                    std::to_string(gdb_yaw_only_rotations) +
                    ", identity=" +
                    std::to_string(gdb_identity_rotations) +
                    ", pi-pair-full=" +
                    std::to_string(gdb_pi_pair_yaw_rotations));
                if (gdb_model_hash_hits > 0 || gdb_model_hash_misses > 0) {
                    OutputLog::info(
                        "gdb-derived model path hashes: hit=" +
                        std::to_string(gdb_model_hash_hits) +
                        ", miss=" +
                        std::to_string(gdb_model_hash_misses));
                }
                if (gdb_shop_companions_emitted > 0) {
                    OutputLog::info(
                        "gdb shop companions: " +
                        std::to_string(gdb_shop_companions_emitted) +
                        " instance(s) across " +
                        std::to_string(gdb_shop_companion_paths.size()) +
                        " model(s)");
                    std::vector<std::pair<std::string, size_t>> shop_paths(
                        gdb_shop_companion_paths.begin(),
                        gdb_shop_companion_paths.end());
                    std::sort(shop_paths.begin(), shop_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(shop_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  shop companion: " +
                            std::to_string(shop_paths[i].second) +
                            "x  " + shop_paths[i].first);
                    }
                }
                if (gdb_nohash_shell_companions_emitted > 0 ||
                    gdb_nohash_shell_companion_misses > 0)
                {
                    OutputLog::info(
                        "gdb nohash shell companions: emitted " +
                        std::to_string(
                            gdb_nohash_shell_companions_emitted) +
                        " instance(s) across " +
                        std::to_string(
                            gdb_nohash_shell_companion_paths.size()) +
                        " model(s), missing-path " +
                        std::to_string(
                            gdb_nohash_shell_companion_misses));
                    std::vector<std::pair<std::string, size_t>> nohash_paths(
                        gdb_nohash_shell_companion_paths.begin(),
                        gdb_nohash_shell_companion_paths.end());
                    std::sort(nohash_paths.begin(), nohash_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(nohash_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  nohash shell companion: " +
                            std::to_string(nohash_paths[i].second) +
                            "x  " + nohash_paths[i].first);
                    }
                }
                if (gdb_gmd_layout_children_emitted > 0 ||
                    gdb_gmd_layout_children_missing > 0)
                {
                    OutputLog::info(
                        "gdb .gmd layout children: emitted " +
                        std::to_string(gdb_gmd_layout_children_emitted) +
                        " instance(s) across " +
                        std::to_string(gdb_gmd_layout_child_paths.size()) +
                        " model(s), unresolved " +
                        std::to_string(gdb_gmd_layout_children_missing));
                    std::vector<std::pair<std::string, size_t>> child_paths(
                        gdb_gmd_layout_child_paths.begin(),
                        gdb_gmd_layout_child_paths.end());
                    std::sort(child_paths.begin(), child_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(child_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  .gmd child: " +
                            std::to_string(child_paths[i].second) +
                            "x  " + child_paths[i].first);
                    }
                }
                if (gdb_gmd_layout_sidecars_loaded > 0 ||
                    gdb_gmd_layout_sidecars_missing > 0)
                {
                    OutputLog::info(
                        "gdb .gmd sidecars: loaded " +
                        std::to_string(gdb_gmd_layout_sidecars_loaded) +
                        ", missing " +
                        std::to_string(gdb_gmd_layout_sidecars_missing));
                    if (global_gmd_sidecar_index_built) {
                        OutputLog::info(
                            "gdb .gmd global index: " +
                            std::to_string(
                                global_gmd_sidecar_index.size()) +
                            " exact sidecar path(s) across " +
                            std::to_string(
                                global_gmd_sidecar_index_bnks) +
                            " streaming BNK(s)");
                    }
                    std::vector<std::pair<std::string, size_t>> sources(
                        gdb_gmd_layout_sidecar_sources.begin(),
                        gdb_gmd_layout_sidecar_sources.end());
                    std::sort(sources.begin(), sources.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n = std::min<size_t>(sources.size(), 6);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  .gmd source: " +
                            std::to_string(sources[i].second) +
                            "x  " + sources[i].first);
                    }
                }
                if (gdb_shell_bad_position_skipped > 0) {
                    OutputLog::info(
                        "gdb-derived skipped non-world shell positions: " +
                        std::to_string(gdb_shell_bad_position_skipped));
                    std::vector<std::pair<std::string, size_t>> bad_paths(
                        gdb_shell_bad_position_paths.begin(),
                        gdb_shell_bad_position_paths.end());
                    std::sort(bad_paths.begin(), bad_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(bad_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  skip non-world shell: " +
                            std::to_string(bad_paths[i].second) +
                            "x  " + bad_paths[i].first);
                    }
                }
                if (gdb_duplicate_instances_skipped > 0) {
                    OutputLog::info(
                        "gdb-derived skipped exact duplicate records: " +
                        std::to_string(gdb_duplicate_instances_skipped));
                    std::vector<std::pair<std::string, size_t>> dup_paths(
                        gdb_duplicate_skip_paths.begin(),
                        gdb_duplicate_skip_paths.end());
                    std::sort(dup_paths.begin(), dup_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(dup_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  skip duplicate: " +
                            std::to_string(dup_paths[i].second) +
                            "x  " + dup_paths[i].first);
                    }
                }

                if (!gdb_dup_slot_offsets.empty() ||
                    !gdb_pos_slot_links.empty()) {
                    size_t linked = 0;
                    auto link_block_instances = [&](Level::PropBlock& block) {
                        if (block.model_path.empty()) return;
                        for (auto& inst : block.instances) {
                            auto pos_hit = gdb_pos_slot_links.find(
                                gdb_pos_link_key(block.model_path,
                                                 inst.values[0],
                                                 inst.values[1],
                                                 inst.values[2]));
                            const bool have_pos_link =
                                pos_hit != gdb_pos_slot_links.end();
                            if (inst.gdb_pos_off[0] || inst.gdb_pos_off[1] ||
                                inst.gdb_pos_off[2]) {
                                if (have_pos_link &&
                                    inst.gdb_entity_hash == 0) {
                                    inst.gdb_entity_hash =
                                        pos_hit->second.entity_hash;
                                }
                                continue;
                            }
                            auto hit = gdb_dup_slot_offsets.find(
                                prop_instance_transform_key(
                                    inst, block.model_path));
                            if (hit != gdb_dup_slot_offsets.end()) {
                                inst.gdb_pos_off[0] = hit->second[0];
                                inst.gdb_pos_off[1] = hit->second[1];
                                inst.gdb_pos_off[2] = hit->second[2];
                                inst.gdb_rot_off[0] = hit->second[3];
                                inst.gdb_rot_off[1] = hit->second[4];
                                inst.gdb_rot_off[2] = hit->second[5];
                            } else if (have_pos_link) {
                                inst.gdb_pos_off[0] = pos_hit->second.slots[0];
                                inst.gdb_pos_off[1] = pos_hit->second.slots[1];
                                inst.gdb_pos_off[2] = pos_hit->second.slots[2];
                                inst.gdb_rot_off[0] = pos_hit->second.slots[3];
                                inst.gdb_rot_off[1] = pos_hit->second.slots[4];
                                inst.gdb_rot_off[2] = pos_hit->second.slots[5];
                            } else {
                                continue;
                            }
                            if (have_pos_link && inst.gdb_entity_hash == 0) {
                                inst.gdb_entity_hash =
                                    pos_hit->second.entity_hash;
                            }
                            ++linked;
                        }
                    };
                    for (auto& block : level_prop_blocks) {
                        link_block_instances(block);
                    }
                    for (auto& kv : blocks_by_path) {
                        link_block_instances(kv.second);
                    }
                    if (linked > 0) {
                        OutputLog::info(
                            "gdb-derived: linked " + std::to_string(linked) +
                            " prop instance(s) to their GDB entity "
                            "transform slots (edits move collision too)");
                    }
                }
                if (gdb_authored_shell_skipped > 0) {
                    OutputLog::info(
                        "gdb-derived skipped exact authored building/structure duplicates: " +
                        std::to_string(gdb_authored_shell_skipped) +
                        " instance(s) across " +
                        std::to_string(gdb_authored_shell_skip_paths.size()) +
                        " model(s)");
                    std::vector<std::pair<std::string, size_t>> skipped_paths(
                        gdb_authored_shell_skip_paths.begin(),
                        gdb_authored_shell_skip_paths.end());
                    std::sort(skipped_paths.begin(), skipped_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(skipped_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  skip exact authored: " +
                            std::to_string(skipped_paths[i].second) +
                            "x  " + skipped_paths[i].first);
                        auto sample_it =
                            gdb_authored_shell_skip_samples.find(
                                skipped_paths[i].first);
                        if (sample_it !=
                            gdb_authored_shell_skip_samples.end())
                        {
                            for (const auto& sample : sample_it->second) {
                                OutputLog::info("    e.g. " + sample);
                            }
                        }
                    }
                }
                if (gdb_companion_interiors_emitted > 0) {
                    OutputLog::info(
                        "gdb-derived companion interiors: " +
                        std::to_string(gdb_companion_interiors_emitted) +
                        " instance(s) across " +
                        std::to_string(gdb_companion_interior_paths.size()) +
                        " model(s)");
                    std::vector<std::pair<std::string, size_t>> interior_paths(
                        gdb_companion_interior_paths.begin(),
                        gdb_companion_interior_paths.end());
                    std::sort(interior_paths.begin(), interior_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(interior_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  companion interior: " +
                            std::to_string(interior_paths[i].second) +
                            "x  " + interior_paths[i].first);
                    }
                }
                if (gdb_companion_exteriors_emitted > 0) {
                    OutputLog::info(
                        "gdb-derived companion exteriors: " +
                        std::to_string(gdb_companion_exteriors_emitted) +
                        " instance(s) across " +
                        std::to_string(gdb_companion_exterior_paths.size()) +
                        " model(s)");
                    std::vector<std::pair<std::string, size_t>> exterior_paths(
                        gdb_companion_exterior_paths.begin(),
                        gdb_companion_exterior_paths.end());
                    std::sort(exterior_paths.begin(), exterior_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(exterior_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  companion exterior: " +
                            std::to_string(exterior_paths[i].second) +
                            "x  " + exterior_paths[i].first);
                    }
                }
                if (!gdb_emitted_shell_paths.empty()) {
                    size_t total_shells = 0;
                    for (const auto& kv : gdb_emitted_shell_paths) {
                        total_shells += kv.second;
                    }
                    OutputLog::info(
                        "gdb-derived emitted building/structure audit: " +
                        std::to_string(total_shells) +
                        " instance(s) across " +
                        std::to_string(gdb_emitted_shell_paths.size()) +
                        " model(s)");
                    std::vector<std::pair<std::string, size_t>> emitted_paths(
                        gdb_emitted_shell_paths.begin(),
                        gdb_emitted_shell_paths.end());
                    std::sort(emitted_paths.begin(), emitted_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(emitted_paths.size(), 12);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  emit shell: " +
                            std::to_string(emitted_paths[i].second) +
                            "x  " + emitted_paths[i].first);
                        auto sample_it =
                            gdb_emitted_shell_samples.find(
                                emitted_paths[i].first);
                        if (sample_it != gdb_emitted_shell_samples.end()) {
                            for (const auto& sample : sample_it->second) {
                                OutputLog::info("    e.g. " + sample);
                            }
                        }
                    }
                }
            } else {
                os3 << " (not emitted: GDB has entity names, not model paths)";
                OutputLog::warn(os3.str());
            }

            std::vector<uint8_t> hk_scan_bytes;
            const std::string hk_scan_path = sibling_with_ext(".havok_scenario");
            if (!emit_derived_render_placements) {
                OutputLog::info(
                    "derived render placements disabled");
            } else if (save_physics_instances_emitted > 0) {
                OutputLog::info(
                    "havok entity-scan: skipped render placement fallback; using .save PhysicsData transforms");
            } else if (load_text_sibling(hk_scan_path, hk_scan_bytes)) {
                auto be_f32 = [&](size_t off) -> float {
                    if (off + 4 > hk_scan_bytes.size())
                        return std::numeric_limits<float>::quiet_NaN();
                    uint32_t u =
                        (uint32_t(hk_scan_bytes[off    ]) << 24) |
                        (uint32_t(hk_scan_bytes[off + 1]) << 16) |
                        (uint32_t(hk_scan_bytes[off + 2]) <<  8) |
                         uint32_t(hk_scan_bytes[off + 3]);
                    float f; std::memcpy(&f, &u, 4); return f;
                };

                std::unordered_map<uint32_t, std::string> hash_to_name;
                hash_to_name.reserve(save_hash_to_name.size());
                for (const auto& kv : save_hash_to_name) {
                    hash_to_name.emplace(kv.first, kv.second);
                }

                size_t found = 0;
                size_t resolved_hk = 0;
                size_t in_terrain = 0;

                auto looks_pos = [](float x, float y, float z) {
                    if (!std::isfinite(x) || !std::isfinite(y) ||
                        !std::isfinite(z)) return false;
                    if (x < -100 || x > 500) return false;
                    if (y < -100 || y > 500) return false;
                    if (z < -100 || z > 500) return false;
                    int nonzero = 0;
                    if (std::fabs(x) > 0.5f) ++nonzero;
                    if (std::fabs(y) > 0.5f) ++nonzero;
                    if (std::fabs(z) > 0.5f) ++nonzero;
                    return nonzero >= 3;
                };
                auto in_main_terrain = [](float x, float y, float z) {
                    return (x >= 0 && x <= 290) &&
                           (y >= 0 && y <= 390) &&
                           (z >= -10 && z <= 250);
                };

                for (size_t i = 0; i + 4 <= hk_scan_bytes.size(); i += 4) {
                    uint32_t v =
                        (uint32_t(hk_scan_bytes[i    ]) << 24) |
                        (uint32_t(hk_scan_bytes[i + 1]) << 16) |
                        (uint32_t(hk_scan_bytes[i + 2]) <<  8) |
                         uint32_t(hk_scan_bytes[i + 3]);
                    auto it = hash_to_name.find(v);
                    if (it == hash_to_name.end()) continue;
                    ++found;

                    float best_x = 0, best_y = 0, best_z = 0;
                    int   best_dist = INT_MAX;
                    bool  best_in_terrain = false;
                    bool  found_any = false;

                    const size_t lo = (i >= 128) ? i - 128 : 0;
                    const size_t hi = std::min(hk_scan_bytes.size() - 12, i + 64);
                    for (size_t q = lo; q <= hi; q += 4) {
                        float x = be_f32(q);
                        float y = be_f32(q + 4);
                        float z = be_f32(q + 8);
                        if (!looks_pos(x, y, z)) continue;
                        const bool inT = in_main_terrain(x, y, z);
                        int dist = (int)(q > i ? q - i : i - q);
                        bool better = false;
                        if (!found_any) better = true;
                        else if (inT && !best_in_terrain) better = true;
                        else if (inT == best_in_terrain && dist < best_dist) {
                            better = true;
                        }
                        if (better) {
                            best_x = x; best_y = y; best_z = z;
                            best_dist = dist;
                            best_in_terrain = inT;
                            found_any = true;
                        }
                    }
                    if (!found_any) continue;
                    ++resolved_hk;
                    if (best_in_terrain) ++in_terrain;

                    std::string tok = canonicalize_for_match(it->second);
                    if (tok.empty()) continue;
                    const FlatAssetEntry* hit =
                        resolve_model_for_entity(it->second);
                    if (!hit) continue;

                    auto& pb = blocks_by_path[hit->full_path];
                    if (pb.model_path.empty()) {
                        pb.type = 0xB2;
                        pb.model_path = hit->full_path;
                    }
                    Level::PropInstance pi;
                    pi.values[0]  = best_x;
                    pi.values[1]  = best_y;
                    pi.values[2]  = best_z;
                    pi.values[6]  = 0.0f;
                    pi.values[7]  = 1.0f;
                    pi.values[9]  = pi.values[10] = pi.values[11] = 1.0f;
                    pb.instances.push_back(pi);
                }

                std::ostringstream hos;
                hos << "havok entity-scan: " << found
                    << " save hashes matched in havok_scenario, "
                    << resolved_hk << " got positions ("
                    << in_terrain << " in main terrain bounds)";
                if (resolved_hk > 0) OutputLog::success(hos.str());
                else                  OutputLog::warn(hos.str());

            } else {
                OutputLog::warn("havok entity-scan skipped: no .havok_scenario");
            }
            size_t extra_blocks = 0, extra_insts = 0;
            {
                std::vector<Level::PropBlock> derived_bridge_blocks;
                for (const auto& kv : blocks_by_path) {
                    if (is_bridge_debug_path(kv.second.model_path) ||
                        is_bridge_debug_path(kv.second.lod_model_path) ||
                        is_bridge_debug_path(kv.second.shadow_model_path) ||
                        is_bridge_debug_path(kv.second.extra_model_path)) {
                        derived_bridge_blocks.push_back(kv.second);
                    }
                }
                bridge_debug_dump_blocks(
                    "DERIVED BLOCKS BEFORE PROP PIPELINE",
                    derived_bridge_blocks);
            }
            for (auto& kv : blocks_by_path) {
                if (kv.second.instances.empty()) continue;
                ++extra_blocks;
                extra_insts += kv.second.instances.size();
                g_pending_level_prop_blocks.push_back(std::move(kv.second));
            }
            std::ostringstream eos;
            eos << "derived placements: "
                << extra_blocks << " unique models / "
                << extra_insts << " instances appended to prop pipeline";
            if (extra_insts > 0) OutputLog::success(eos.str());
            else                 OutputLog::warn(eos.str());
        } else {
            OutputLog::warn("no .gdb sibling in BNK");
        }
