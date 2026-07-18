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
                    if (is_bridge_path(hit->full_path) &&
                        !matched_model_path_hash) {
                        add_gdb_interest_row(
                            p, entity_key, gdb_interest_category,
                            "rejected_fuzzy_bridge_match", hit->full_path);
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
