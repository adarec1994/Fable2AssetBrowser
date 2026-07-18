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

