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

