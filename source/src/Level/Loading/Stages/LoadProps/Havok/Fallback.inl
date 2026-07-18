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
