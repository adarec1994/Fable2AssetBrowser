            std::ostringstream os3;
            os3 << "gdb-derived placements: "
                << resolved << " entities matched a model";
            if (gdb_instances_emitted > 0) {
                os3 << ", emitted " << gdb_instances_emitted
                    << " instance(s)";
                OutputLog::success(os3.str());
                OutputLog::info(
                    "gdb-derived rotations: full-euler=" +
                    std::to_string(gdb_full_euler_rotations) +
                    ", yaw-only=" +
                    std::to_string(gdb_yaw_only_rotations) +
                    ", identity=" +
                    std::to_string(gdb_identity_rotations) +
                    ", pi-pair-full=" +
                    std::to_string(gdb_pi_pair_yaw_rotations));
                if (gdb_model_hash_hits > 0 || gdb_model_hash_misses > 0) {
                    OutputLog::info(
                        "gdb-derived model path hashes: hit=" +
                        std::to_string(gdb_model_hash_hits) +
                        ", miss=" +
                        std::to_string(gdb_model_hash_misses));
                }
                if (gdb_shop_companions_emitted > 0) {
                    OutputLog::info(
                        "gdb shop companions: " +
                        std::to_string(gdb_shop_companions_emitted) +
                        " instance(s) across " +
                        std::to_string(gdb_shop_companion_paths.size()) +
                        " model(s)");
                    std::vector<std::pair<std::string, size_t>> shop_paths(
                        gdb_shop_companion_paths.begin(),
                        gdb_shop_companion_paths.end());
                    std::sort(shop_paths.begin(), shop_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(shop_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  shop companion: " +
                            std::to_string(shop_paths[i].second) +
                            "x  " + shop_paths[i].first);
                    }
                }
                if (gdb_nohash_shell_companions_emitted > 0 ||
                    gdb_nohash_shell_companion_misses > 0)
                {
                    OutputLog::info(
                        "gdb nohash shell companions: emitted " +
                        std::to_string(
                            gdb_nohash_shell_companions_emitted) +
                        " instance(s) across " +
                        std::to_string(
                            gdb_nohash_shell_companion_paths.size()) +
                        " model(s), missing-path " +
                        std::to_string(
                            gdb_nohash_shell_companion_misses));
                    std::vector<std::pair<std::string, size_t>> nohash_paths(
                        gdb_nohash_shell_companion_paths.begin(),
                        gdb_nohash_shell_companion_paths.end());
                    std::sort(nohash_paths.begin(), nohash_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(nohash_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  nohash shell companion: " +
                            std::to_string(nohash_paths[i].second) +
                            "x  " + nohash_paths[i].first);
                    }
                }
                if (gdb_gmd_layout_children_emitted > 0 ||
                    gdb_gmd_layout_children_missing > 0)
                {
                    OutputLog::info(
                        "gdb .gmd layout children: emitted " +
                        std::to_string(gdb_gmd_layout_children_emitted) +
                        " instance(s) across " +
                        std::to_string(gdb_gmd_layout_child_paths.size()) +
                        " model(s), unresolved " +
                        std::to_string(gdb_gmd_layout_children_missing));
                    std::vector<std::pair<std::string, size_t>> child_paths(
                        gdb_gmd_layout_child_paths.begin(),
                        gdb_gmd_layout_child_paths.end());
                    std::sort(child_paths.begin(), child_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(child_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  .gmd child: " +
                            std::to_string(child_paths[i].second) +
                            "x  " + child_paths[i].first);
                    }
                }
                if (gdb_gmd_layout_sidecars_loaded > 0 ||
                    gdb_gmd_layout_sidecars_missing > 0)
                {
                    OutputLog::info(
                        "gdb .gmd sidecars: loaded " +
                        std::to_string(gdb_gmd_layout_sidecars_loaded) +
                        ", missing " +
                        std::to_string(gdb_gmd_layout_sidecars_missing));
                    if (global_gmd_sidecar_index_built) {
                        OutputLog::info(
                            "gdb .gmd global index: " +
                            std::to_string(
                                global_gmd_sidecar_index.size()) +
                            " exact sidecar path(s) across " +
                            std::to_string(
                                global_gmd_sidecar_index_bnks) +
                            " streaming BNK(s)");
                    }
                    std::vector<std::pair<std::string, size_t>> sources(
                        gdb_gmd_layout_sidecar_sources.begin(),
                        gdb_gmd_layout_sidecar_sources.end());
                    std::sort(sources.begin(), sources.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n = std::min<size_t>(sources.size(), 6);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  .gmd source: " +
                            std::to_string(sources[i].second) +
                            "x  " + sources[i].first);
                    }
                }
                if (gdb_shell_bad_position_skipped > 0) {
                    OutputLog::info(
                        "gdb-derived skipped non-world shell positions: " +
                        std::to_string(gdb_shell_bad_position_skipped));
                    std::vector<std::pair<std::string, size_t>> bad_paths(
                        gdb_shell_bad_position_paths.begin(),
                        gdb_shell_bad_position_paths.end());
                    std::sort(bad_paths.begin(), bad_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(bad_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  skip non-world shell: " +
                            std::to_string(bad_paths[i].second) +
                            "x  " + bad_paths[i].first);
                    }
                }
                if (gdb_duplicate_instances_skipped > 0) {
                    OutputLog::info(
                        "gdb-derived skipped exact duplicate records: " +
                        std::to_string(gdb_duplicate_instances_skipped));
                    std::vector<std::pair<std::string, size_t>> dup_paths(
                        gdb_duplicate_skip_paths.begin(),
                        gdb_duplicate_skip_paths.end());
                    std::sort(dup_paths.begin(), dup_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(dup_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  skip duplicate: " +
                            std::to_string(dup_paths[i].second) +
                            "x  " + dup_paths[i].first);
                    }
                }

                if (!gdb_dup_slot_offsets.empty() ||
                    !gdb_pos_slot_links.empty()) {
                    size_t linked = 0;
                    auto link_block_instances = [&](Level::PropBlock& block) {
                        if (block.model_path.empty()) return;
                        for (auto& inst : block.instances) {
                            auto pos_hit = gdb_pos_slot_links.find(
                                gdb_pos_link_key(block.model_path,
                                                 inst.values[0],
                                                 inst.values[1],
                                                 inst.values[2]));
                            const bool have_pos_link =
                                pos_hit != gdb_pos_slot_links.end();
                            if (inst.gdb_pos_off[0] || inst.gdb_pos_off[1] ||
                                inst.gdb_pos_off[2]) {
                                if (have_pos_link &&
                                    inst.gdb_entity_hash == 0) {
                                    inst.gdb_entity_hash =
                                        pos_hit->second.entity_hash;
                                }
                                continue;
                            }
                            auto hit = gdb_dup_slot_offsets.find(
                                prop_instance_transform_key(
                                    inst, block.model_path));
                            if (hit != gdb_dup_slot_offsets.end()) {
                                inst.gdb_pos_off[0] = hit->second[0];
                                inst.gdb_pos_off[1] = hit->second[1];
                                inst.gdb_pos_off[2] = hit->second[2];
                                inst.gdb_rot_off[0] = hit->second[3];
                                inst.gdb_rot_off[1] = hit->second[4];
                                inst.gdb_rot_off[2] = hit->second[5];
                            } else if (have_pos_link) {
                                inst.gdb_pos_off[0] = pos_hit->second.slots[0];
                                inst.gdb_pos_off[1] = pos_hit->second.slots[1];
                                inst.gdb_pos_off[2] = pos_hit->second.slots[2];
                                inst.gdb_rot_off[0] = pos_hit->second.slots[3];
                                inst.gdb_rot_off[1] = pos_hit->second.slots[4];
                                inst.gdb_rot_off[2] = pos_hit->second.slots[5];
                            } else {
                                continue;
                            }
                            if (have_pos_link && inst.gdb_entity_hash == 0) {
                                inst.gdb_entity_hash =
                                    pos_hit->second.entity_hash;
                            }
                            ++linked;
                        }
                    };
                    for (auto& block : level_prop_blocks) {
                        link_block_instances(block);
                    }
                    for (auto& kv : blocks_by_path) {
                        link_block_instances(kv.second);
                    }
                    if (linked > 0) {
                        OutputLog::info(
                            "gdb-derived: linked " + std::to_string(linked) +
                            " prop instance(s) to their GDB entity "
                            "transform slots (edits move collision too)");
                    }
                }
                if (gdb_authored_shell_skipped > 0) {
                    OutputLog::info(
                        "gdb-derived skipped exact authored building/structure duplicates: " +
                        std::to_string(gdb_authored_shell_skipped) +
                        " instance(s) across " +
                        std::to_string(gdb_authored_shell_skip_paths.size()) +
                        " model(s)");
                    std::vector<std::pair<std::string, size_t>> skipped_paths(
                        gdb_authored_shell_skip_paths.begin(),
                        gdb_authored_shell_skip_paths.end());
                    std::sort(skipped_paths.begin(), skipped_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(skipped_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  skip exact authored: " +
                            std::to_string(skipped_paths[i].second) +
                            "x  " + skipped_paths[i].first);
                        auto sample_it =
                            gdb_authored_shell_skip_samples.find(
                                skipped_paths[i].first);
                        if (sample_it !=
                            gdb_authored_shell_skip_samples.end())
                        {
                            for (const auto& sample : sample_it->second) {
                                OutputLog::info("    e.g. " + sample);
                            }
                        }
                    }
                }
                if (gdb_companion_interiors_emitted > 0) {
                    OutputLog::info(
                        "gdb-derived companion interiors: " +
                        std::to_string(gdb_companion_interiors_emitted) +
                        " instance(s) across " +
                        std::to_string(gdb_companion_interior_paths.size()) +
                        " model(s)");
                    std::vector<std::pair<std::string, size_t>> interior_paths(
                        gdb_companion_interior_paths.begin(),
                        gdb_companion_interior_paths.end());
                    std::sort(interior_paths.begin(), interior_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(interior_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  companion interior: " +
                            std::to_string(interior_paths[i].second) +
                            "x  " + interior_paths[i].first);
                    }
                }
                if (gdb_companion_exteriors_emitted > 0) {
                    OutputLog::info(
                        "gdb-derived companion exteriors: " +
                        std::to_string(gdb_companion_exteriors_emitted) +
                        " instance(s) across " +
                        std::to_string(gdb_companion_exterior_paths.size()) +
                        " model(s)");
                    std::vector<std::pair<std::string, size_t>> exterior_paths(
                        gdb_companion_exterior_paths.begin(),
                        gdb_companion_exterior_paths.end());
                    std::sort(exterior_paths.begin(), exterior_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(exterior_paths.size(), 8);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  companion exterior: " +
                            std::to_string(exterior_paths[i].second) +
                            "x  " + exterior_paths[i].first);
                    }
                }
                if (!gdb_emitted_shell_paths.empty()) {
                    size_t total_shells = 0;
                    for (const auto& kv : gdb_emitted_shell_paths) {
                        total_shells += kv.second;
                    }
                    OutputLog::info(
                        "gdb-derived emitted building/structure audit: " +
                        std::to_string(total_shells) +
                        " instance(s) across " +
                        std::to_string(gdb_emitted_shell_paths.size()) +
                        " model(s)");
                    std::vector<std::pair<std::string, size_t>> emitted_paths(
                        gdb_emitted_shell_paths.begin(),
                        gdb_emitted_shell_paths.end());
                    std::sort(emitted_paths.begin(), emitted_paths.end(),
                              [](const auto& a, const auto& b) {
                                  if (a.second != b.second) {
                                      return a.second > b.second;
                                  }
                                  return a.first < b.first;
                              });
                    const size_t n =
                        std::min<size_t>(emitted_paths.size(), 12);
                    for (size_t i = 0; i < n; ++i) {
                        OutputLog::info(
                            "  emit shell: " +
                            std::to_string(emitted_paths[i].second) +
                            "x  " + emitted_paths[i].first);
                        auto sample_it =
                            gdb_emitted_shell_samples.find(
                                emitted_paths[i].first);
                        if (sample_it != gdb_emitted_shell_samples.end()) {
                            for (const auto& sample : sample_it->second) {
                                OutputLog::info("    e.g. " + sample);
                            }
                        }
                    }
                }
            } else {
                os3 << " (not emitted: GDB has entity names, not model paths)";
                OutputLog::warn(os3.str());
            }

