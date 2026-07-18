                        g_pending_level_water_present = false;
                        g_pending_level_water_scene = Level::WaterScene{};
                        g_pending_level_water_theme = Gdb::WaterTheme{};
                        {
                            std::vector<std::string> water_candidates;
                            auto add_unique_water = [&](const std::string& path) {
                                if (path.empty()) return;
                                const std::string norm = norm_path(path);
                                for (const auto& existing : water_candidates) {
                                    if (norm_path(existing) == norm) return;
                                }
                                water_candidates.push_back(path);
                            };

                            const std::string main_base =
                                basename_no_ext(res.ehf_path.empty()
                                    ? res.ghf_path : res.ehf_path);
                            for (const auto& water_ref : all_water_refs) {
                                if (!main_base.empty() &&
                                    basename_no_ext(water_ref) == main_base) {
                                    add_unique_water(water_ref);
                                }
                            }
                            if (!res.ehf_path.empty()) {
                                add_unique_water(with_ext(res.ehf_path, ".water"));
                            }
                            if (!res.ghf_path.empty()) {
                                add_unique_water(with_ext(res.ghf_path, ".water"));
                            }
                            for (const auto& water_ref : all_water_refs) {
                                add_unique_water(water_ref);
                            }

                            bool found_water_file = false;
                            Level::WaterScene merged;
                            for (const auto& water_path : water_candidates) {
                                std::vector<uint8_t> water_bytes;
                                if (!load_text_sibling(water_path, water_bytes) ||
                                    water_bytes.empty()) {
                                    continue;
                                }

                                found_water_file = true;
                                Level::WaterScene scene;
                                if (Level::ParseWaterFile(water_bytes, scene)) {
                                    size_t total_tiles = 0;
                                    for (const auto& b : scene.bodies)
                                        total_tiles += b.tiles.size();
                                    OutputLog::success(
                                        ".water parsed: " +
                                        std::to_string(scene.bodies.size()) +
                                        " bodies, " +
                                        std::to_string(total_tiles) + " tiles from " +
                                        water_path);
                                    merged.version = scene.version;
                                    merged.tile_count += scene.tile_count;
                                    for (auto& b : scene.bodies) {
                                        merged.bodies.push_back(std::move(b));
                                    }
                                    continue;
                                }

                                
                                
                                
                                if (water_bytes.size() > 16) {
                                    OutputLog::warn(
                                        ".water sibling found but failed "
                                        "to parse: " + water_path);
                                }
                            }
                            if (!merged.bodies.empty()) {
                                merged.body_count =
                                    uint32_t(merged.bodies.size());
                                OutputLog::success(
                                    ".water merged scene: " +
                                    std::to_string(merged.bodies.size()) +
                                    " bodies, " +
                                    std::to_string(merged.tile_count) +
                                    " tiles across all heightfields");
                                g_pending_level_water_scene = std::move(merged);
                                g_pending_level_water_present = true;
                            }

                            if (!found_water_file && !water_candidates.empty()) {
                                OutputLog::info(
                                    ".water not found in level BNK; first tried " +
                                    water_candidates.front());
                            }
                        }
