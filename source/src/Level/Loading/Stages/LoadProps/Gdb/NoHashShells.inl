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

