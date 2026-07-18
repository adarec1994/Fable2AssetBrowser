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

