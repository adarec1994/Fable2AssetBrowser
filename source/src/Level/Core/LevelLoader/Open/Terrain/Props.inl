                        {
                            std::vector<LevelEdit::Addition> adds;
                            LevelEdit::GetAdditions(adds);
                            for (size_t ai = 0; ai < adds.size(); ++ai) {
                                const auto& a = adds[ai];
                                if (a.removed) continue;
                                if (a.model_path.empty()) {
                                    if (a.entity_kind ==
                                        LevelEdit::AdditionEntityKind::Chest) {
                                        LevelSpawnMarker marker;
                                        marker.x = a.pos[0];
                                        marker.y = a.pos[1];
                                        marker.z = a.pos[2];
                                        marker.kind = a.is_dig_spot ? 4 : 5;
                                        marker.is_container = true;
                                        marker.pending_addition_index =
                                            int(ai);
                                        marker.name = a.entity_name.empty()
                                            ? (a.is_dig_spot
                                                   ? "New dig spot"
                                                   : "New container")
                                            : a.entity_name;
                                        g_level_spawn_markers.push_back(
                                            std::move(marker));
                                    }
                                    continue;
                                }
                                if (a.entity_kind ==
                                        LevelEdit::AdditionEntityKind::
                                            GenericProp &&
                                    !a.entity_name.empty()) {
                                    const bool already_marked =
                                        std::any_of(
                                            g_level_spawn_markers.begin(),
                                            g_level_spawn_markers.end(),
                                            [&](const LevelSpawnMarker&
                                                    existing) {
                                                return existing
                                                           .pending_addition_index ==
                                                       int(ai);
                                            });
                                    if (!already_marked) {
                                        LevelSpawnMarker marker;
                                        marker.x = a.pos[0];
                                        marker.y = a.pos[1];
                                        marker.z = a.pos[2];
                                        marker.kind = 6;
                                        marker.pending_addition_index =
                                            int(ai);
                                        marker.name = a.entity_name;
                                        std::string model_path =
                                            a.model_path;
                                        std::transform(
                                            model_path.begin(),
                                            model_path.end(),
                                            model_path.begin(),
                                            [](unsigned char c) {
                                                return static_cast<char>(
                                                    std::tolower(c));
                                            });
                                        std::replace(model_path.begin(),
                                                     model_path.end(), '/',
                                                     '\\');
                                        uint32_t model_hash =
                                            0x811C9DC5u;
                                        for (unsigned char c :
                                             model_path) {
                                            model_hash *= 0x01000193u;
                                            model_hash ^= uint32_t(c);
                                        }
                                        marker.model_hashes.push_back(
                                            model_hash);
                                        g_level_spawn_markers.push_back(
                                            std::move(marker));
                                    }
                                }
                                Level::PropBlock pb;
                                pb.type = 0xB3;
                                pb.model_path = a.model_path;
                                Level::PropInstance pi;
                                pi.hash = 0xADD0000000000000ull + ai;
                                pi.values[0] = a.pos[0];
                                pi.values[1] = a.pos[1];
                                pi.values[2] = a.pos[2];
                                const float yaw =
                                    a.yaw_deg * 0.01745329252f;
                                pi.values[6] = std::sin(yaw);
                                pi.values[7] = std::cos(yaw);
                                pi.values[9] = pi.values[10] =
                                    pi.values[11] = 1.0f;
                                pi.lev_rec_kind = 5;
                                pi.pos_file_offset = (uint32_t)ai + 1;
                                pb.instances.push_back(pi);
                                g_pending_level_prop_blocks.push_back(
                                    std::move(pb));
                            }
                            if (!adds.empty()) {
                                OutputLog::success(
                                    "level edit: injected " +
                                    std::to_string(adds.size()) +
                                    " placed model(s)");
                            }
                        }

                        bridge_debug_dump_blocks(
                            "FINAL RENDER PROP PIPELINE",
                            g_pending_level_prop_blocks);
