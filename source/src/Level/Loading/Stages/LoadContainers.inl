                    std::unordered_map<uint32_t, std::string> nm;
                    nm.reserve(save_hash_to_name.size());
                    for (const auto& kv : save_hash_to_name) {
                        nm.emplace(kv.first, kv.second);
                    }
                    g_level_spawn_markers.clear();
                    std::unordered_set<uint32_t> emitted_spawns;
                    for (const auto& kv : spawn_ents) {
                        if (!kv.second.has_pos) continue;
                        LevelSpawnMarker m;
                        m.x = kv.second.x;
                        m.y = kv.second.y;
                        m.z = kv.second.z;
                        m.has_rotation = kv.second.has_rotation;
                        m.rot_x = kv.second.rot_x;
                        m.rot_y = kv.second.rot_y;
                        m.rot_z = kv.second.rot_z;
                        m.kind = kv.second.kind;
                        m.entity_hash = kv.first;
                        for (int k = 0; k < 3; ++k) {
                            m.pos_off[k] = kv.second.pos_off[k];
                            m.rot_off[k] = kv.second.rot_off[k];
                        }
                        m.spawn_points_record =
                            kv.second.spawn_points_record;
                        m.spawn_point_entities =
                            kv.second.spawn_point_entities;
                        m.creature_name = kv.second.creature_name;
                        m.creature_entity_hash =
                            kv.second.creature_entity_hash;
                        m.model_hashes = kv.second.model_hashes;
                        auto contents_it =
                            g_level_entity_contents.find(kv.first);
                        m.is_container =
                            m.kind != 1 && m.kind != 2 && m.kind != 3 &&
                            contents_it != g_level_entity_contents.end() &&
                            (contents_it->second.has_inventory_component ||
                             contents_it->second.has_chest_component);
                        auto nit = nm.find(kv.first);
                        if (nit != nm.end()) m.name = nit->second;
                        emitted_spawns.insert(kv.first);
                        g_level_spawn_markers.push_back(std::move(m));
                    }
                    for (const auto& p : info.placements) {
                        if (emitted_spawns.count(p.hash_a)) continue;
                        const auto placed_name_it = nm.find(p.hash_a);
                        const std::string placed_name =
                            placed_name_it != nm.end()
                                ? placed_name_it->second : p.entity_name;
                        uint8_t kind = 0;
                        auto contents_it =
                            g_level_entity_contents.find(p.hash_a);
                        const bool is_container =
                            contents_it != g_level_entity_contents.end() &&
                            (contents_it->second.has_inventory_component ||
                             contents_it->second.has_chest_component);
                        if (contents_it != g_level_entity_contents.end() &&
                            contents_it->second.is_dig_spot) {
                            kind = 4;
                        } else {
                            auto it = spawn_ents.find(p.hash_a);
                            if (it != spawn_ents.end()) {
                                kind = it->second.kind;
                            } else if (p.skeleton_file_hash != 0 ||
                                       p.retarget_skeleton_file_hash != 0) {
                                kind = 3;
                            } else if (
                                placed_name.rfind("F2AB_Static_", 0) == 0) {
                                kind = 6;
                            }
                        }
                        if (!kind && is_container) kind = 5;
                        
                        
                        
                        const bool placed_player_start = [&] {
                            std::string low = placed_name;
                            std::transform(low.begin(), low.end(),
                                           low.begin(),
                                           [](unsigned char c) {
                                               return (char)std::tolower(c);
                                           });
                            return low.rfind("startfrom", 0) == 0 ||
                                   low.rfind("teleportto", 0) == 0;
                        }();
                        if (!kind && !placed_player_start) continue;
                        LevelSpawnMarker m;
                        m.x = p.x;
                        m.y = p.y;
                        m.z = p.z;
                        m.has_rotation = p.has_rotation;
                        m.rot_x = p.rot_x;
                        m.rot_y = p.rot_y;
                        m.rot_z = p.rot_z;
                        m.kind = kind;
                        m.is_container = is_container && kind != 1 &&
                                         kind != 2 && kind != 3;
                        m.entity_hash = p.hash_a;
                        m.model_hashes = p.model_path_hashes;
                        if (m.model_hashes.empty() && p.model_path_hash) {
                            m.model_hashes.push_back(p.model_path_hash);
                        }
                        for (int k = 0; k < 3; ++k) {
                            m.pos_off[k] = p.pos_value_off[k];
                            m.rot_off[k] = p.rot_value_off[k];
                        }
                        m.name = placed_name;
                        emitted_spawns.insert(p.hash_a);
                        g_level_spawn_markers.push_back(std::move(m));
                    }
                    for (const auto& [entity_hash, contents] :
                         g_level_entity_contents) {
                        if ((!contents.has_inventory_component &&
                             !contents.has_chest_component) ||
                            contents.is_dig_spot || !contents.has_pos ||
                            emitted_spawns.count(entity_hash) ||
                            g_level_entity_gameplay.count(entity_hash)) {
                            continue;
                        }
                        LevelSpawnMarker m;
                        m.x = contents.x;
                        m.y = contents.y;
                        m.z = contents.z;
                        m.kind = 5;
                        m.is_container = true;
                        m.entity_hash = entity_hash;
                        for (int k = 0; k < 3; ++k) {
                            m.pos_off[k] = contents.pos_off[k];
                            m.rot_off[k] = contents.rot_off[k];
                        }
                        m.name = contents.entity_name;
                        emitted_spawns.insert(entity_hash);
                        g_level_spawn_markers.push_back(std::move(m));
                    }
                    if (!g_level_spawn_markers.empty()) {
                        size_t gen = 0, sp = 0, cre = 0, dig = 0;
                        size_t static_props = 0;
                        size_t containers = 0;
                        for (const auto& m : g_level_spawn_markers) {
                            if (m.kind == 1) ++gen;
                            else if (m.kind == 2) ++sp;
                            else if (m.kind == 4) ++dig;
                            else if (m.kind == 5) {}
                            else if (m.kind == 6) ++static_props;
                            else ++cre;
                            if (m.is_container && m.kind != 4) {
                                ++containers;
                            }
                        }
                        OutputLog::info(
                            "gdb spawns: " +
                            std::to_string(g_level_spawn_markers.size()) +
                            " marker(s) (" + std::to_string(gen) +
                            " generator(s), " + std::to_string(sp) +
                            " spawn point(s), " + std::to_string(cre) +
                            " creature(s)/NPC(s), " +
                            std::to_string(static_props) +
                            " static prop(s), " +
                            std::to_string(dig) + " dig spot(s), " +
                            std::to_string(containers) +
                            " non-dig container(s))");
                    }
                }
