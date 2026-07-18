                        if (!g_pending_terrain_ghf_heights.empty() &&
                            g_pending_terrain_ghf_width > 0 &&
                            g_pending_terrain_ghf_height > 0)
                        {
                            const int   gw = g_pending_terrain_ghf_width;
                            const int   gh = g_pending_terrain_ghf_height;
                            const float tile =
                                g_pending_terrain_ghf_tile_size > 0.0f
                                    ? g_pending_terrain_ghf_tile_size : 0.5f;
                            const auto& heights = g_pending_terrain_ghf_heights;
                            auto sample_h = [&](float wx, float wy) -> float {
                                float gx = wx / tile;
                                float gy = wy / tile;
                                int ix = int(gx); int iy = int(gy);
                                if (ix < 0) ix = 0; else if (ix >= gw) ix = gw - 1;
                                if (iy < 0) iy = 0; else if (iy >= gh) iy = gh - 1;
                                return heights[size_t(iy) * size_t(gw) + size_t(ix)];
                            };
                            size_t authored_z_count = 0;
                            size_t terrain_delta_count = 0;
                            float max_abs_delta = 0.0f;
                            for (auto& pb : g_pending_level_prop_blocks) {
                                if (pb.type != 0xB1) continue;
                                for (auto& inst : pb.instances) {
                                    const float terrain_z =
                                        sample_h(inst.values[0], inst.values[1]);
                                    const float delta = inst.values[2] - terrain_z;
                                    if (std::isfinite(delta)) {
                                        max_abs_delta =
                                            std::max(max_abs_delta,
                                                     std::fabs(delta));
                                        if (std::fabs(delta) > 0.25f) {
                                            ++terrain_delta_count;
                                        }
                                    }
                                    ++authored_z_count;
                                }
                            }
                            std::ostringstream gs;
                            gs << "preserved authored Z for "
                               << authored_z_count
                               << " GDB-derived placements";
                            if (authored_z_count > 0) {
                                gs << " ("
                                   << terrain_delta_count
                                   << " differ from terrain by >0.25m, max="
                                   << max_abs_delta << ")";
                            }
                            OutputLog::info(gs.str());
                        }
