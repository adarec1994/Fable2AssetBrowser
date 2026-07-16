            g_pending_level_fx.clear();

            if (!g_particle_bank_loaded) {
                std::vector<uint8_t> bank_bytes;
                if (load_text_sibling(
                        "data\\art\\particles\\particle_bank.bnk",
                        bank_bytes) && !bank_bytes.empty()) {
                    if (Fx::ParseParticleBank(bank_bytes, g_particle_bank)) {
                        g_particle_bank_loaded = true;
                        OutputLog::success("fx: particle_bank.bnk parsed (" +
                            std::to_string(g_particle_bank.emitters.size()) +
                            " emitters, " +
                            std::to_string(g_particle_bank.systems.size()) +
                            " systems, " +
                            std::to_string(g_particle_bank.effects.size()) +
                            " effects, " +
                            std::to_string(g_particle_bank.textures.size()) +
                            " textures)");
                    } else {
                        OutputLog::warn("fx: particle_bank.bnk parse failed: " +
                                        g_particle_bank.error);
                    }
                } else {
                    OutputLog::warn("fx: particle_bank.bnk not found in data");
                }
            }

            std::unordered_map<uint32_t, std::vector<Gdb::ParticleFxBinding>>
                fx_bindings;
            std::unordered_map<uint32_t,
                std::vector<Gdb::ParticleAttachmentBinding>> fx_attach_bindings;
            {
                std::vector<const std::vector<uint8_t>*> fx_gdbs;
                fx_gdbs.push_back(&gdb_bytes);
                for (const auto& db : supplemental_gdbs) fx_gdbs.push_back(&db.bytes);
                fx_bindings = Gdb::ExtractParticleFxBindings(fx_gdbs);
                fx_attach_bindings =
                    Gdb::ExtractParticleAttachmentBindings(fx_gdbs);
                if (!fx_bindings.empty()) {
                    OutputLog::info("fx: " + std::to_string(fx_bindings.size()) +
                        " GDB record(s) carry a ParticleEffect binding");
                }
                if (!fx_attach_bindings.empty()) {
                    OutputLog::info("fx: " +
                        std::to_string(fx_attach_bindings.size()) +
                        " GDB record(s) carry a ParticleAttacher binding");
                }
            }

            auto name_hits_bank = [&](const std::string& n) -> bool {
                return g_particle_bank_loaded &&
                       g_particle_bank.find_by_hash(Fx::Fnv1Lower(n)) != nullptr;
            };

            auto push_fx_dedup = [&](Fx::Placement&& fx) -> size_t {
                for (const auto& e : g_pending_level_fx) {
                    if (e.effect_hash != fx.effect_hash) continue;
                    const float dx = e.pos[0] - fx.pos[0];
                    const float dy = e.pos[1] - fx.pos[1];
                    const float dz = e.pos[2] - fx.pos[2];
                    if (dx * dx + dy * dy + dz * dz < 2.5f * 2.5f)
                        return SIZE_MAX;
                }
                g_pending_level_fx.push_back(std::move(fx));
                return g_pending_level_fx.size() - 1;
            };

            std::vector<std::array<float, 3>> authored_fx_pos;
            {
                std::vector<const std::vector<uint8_t>*> fx_gdbs_all;
                fx_gdbs_all.push_back(&gdb_bytes);
                for (const auto& db : supplemental_gdbs)
                    fx_gdbs_all.push_back(&db.bytes);
                std::vector<Gdb::FxEntityPlacement> fx_entities =
                    Gdb::ExtractFxEntityPlacements(gdb_bytes, fx_gdbs_all,
                                                   save_hash_to_name);
                size_t authored = 0;
                const size_t fire_markers = 0;
                for (const auto& fe : fx_entities) {

                    uint32_t hash = 0;
                    std::string name;
                    if (fe.fx_hash && g_particle_bank_loaded &&
                        g_particle_bank.find_by_hash(fe.fx_hash)) {
                        hash = fe.fx_hash;
                        name = fe.name;
                        ++authored;
                    } else {
                        continue;
                    }
                    Fx::Placement fx;
                    fx.pos[0] = fe.x; fx.pos[1] = fe.y; fx.pos[2] = fe.z;
                    if (fe.has_rotation) {
                        fx_game_rotation_matrix(fe.rot_x, fe.rot_y, fe.rot_z,
                                                fx.rot);
                        fx.has_rot = true;
                    }
                    fx.effect_name = name;
                    fx.effect_hash = hash;
                    const float ax = fe.x, ay = fe.y, az = fe.z;
                    if (push_fx_dedup(std::move(fx)) != SIZE_MAX)
                        authored_fx_pos.push_back({ ax, ay, az });
                }
                if (authored || fire_markers) {
                    OutputLog::success(
                        "fx: " + std::to_string(authored) +
                        " authored FX entit(ies) + " +
                        std::to_string(fire_markers) +
                        " fire marker(s) from level records");
                }
            }

            auto near_authored_fx = [&](float x, float y, float z) {
                for (const auto& a : authored_fx_pos) {
                    const float dx = a[0] - x, dy = a[1] - y, dz = a[2] - z;
                    if (dx * dx + dy * dy + dz * dz < 5.0f * 5.0f) return true;
                }
                return false;
            };
            auto entity_is_fx = [](const std::string& n) -> bool {
                std::string s;
                s.reserve(n.size());
                for (char c : n) s.push_back((char)std::tolower((unsigned char)c));
                static const char* kFx[] = {
                    "fxenv", "waterfall", "water_fall", "chimney", "smoke",
                    "steam", "fountain", "watermill", "falls", "hot_pool",
                    "fx_water_flow", "brazier", "campfire", "bonfire", "torch"
                };
                for (const char* k : kFx)
                    if (s.find(k) != std::string::npos) return true;

                if (s.rfind("fx_", 0) == 0 || s.rfind("fx ", 0) == 0)
                    return s.find("hit") == std::string::npos &&
                           s.find("orb") == std::string::npos &&
                           s.find("recoil") == std::string::npos;
                return false;
            };

            auto map_object_to_effect = [&](const std::string& n) -> std::string {
                std::string s;
                for (char c : n) s.push_back((char)std::tolower((unsigned char)c));
                auto has = [&](const char* k) {
                    return s.find(k) != std::string::npos;
                };
                struct Rule { const char* key; const char* effect; };
                static const Rule kRules[] = {

                    { "waterfall_medium", "fxenv_waterfall_medium_01" },
                    { "waterfall_thin",   "fxenv_waterfall_thin_01" },
                    { "waterfall_long",   "fxenv_waterfall_long_01" },
                    { "waterfall",        "fxenv_water_fall_main" },
                    { "water_fall",       "fxenv_water_fall_main" },
                    { "falls",            "fxenv_water_fall_main" },
                    { "watermill",        "fxenv_watermill_splash" },
                    { "fountain",         "fxenv_fountain_top" },
                    { "water_flow",       "fx_water_flow" },

                    { "brazier_evil",     "FXENV_Brazier_Evil_01" },
                    { "brazier",          "FXENV_Medium_Brazier_Fire_01" },
                    { "banditcampfire",   "FX_Camp_Fire_01" },
                    { "campfire",         "FX_Camp_Fire_01" },
                    { "camp_fire",        "FX_Camp_Fire_01" },
                    { "bonfire",          "FX_Camp_Fire_01" },
                    { "firepit",          "FX_Camp_Fire_01" },
                    { "torch",            "fxenv_torch_fire_03" },

                    { "chimney",          "FXENV_Drifting_Smoke_01" },
                    { "smoke",            "FXENV_Drifting_Smoke_01" },
                    { "steam",            "fxenv_steam_grating" },
                };
                for (const auto& r : kRules) {
                    if (!has(r.key)) continue;
                    if (g_particle_bank_loaded &&
                        g_particle_bank.find_by_hash(Fx::Fnv1Lower(r.effect)))
                        return r.effect;
                }
                return {};
            };

            struct FxBindingChoice {
                uint32_t hash = 0;
                std::string name;
            };
            auto binding_for = [&](uint32_t rec_hash, uint32_t parent_hash,
                                   FxBindingChoice& out_choice) -> bool {
                for (uint32_t key : { rec_hash, parent_hash }) {
                    if (!key) continue;
                    auto it = fx_bindings.find(key);
                    if (it == fx_bindings.end()) continue;
                    const Gdb::ParticleFxBinding* best = nullptr;
                    for (const auto& b : it->second) {
                        if (g_particle_bank_loaded &&
                            g_particle_bank.find_by_hash(b.fx_hash_lower)) {
                            best = &b; break;
                        }
                        if (!best) best = &b;
                    }
                    if (best) {
                        out_choice.hash = best->fx_hash_lower;
                        out_choice.name = best->fx_name;
                        return true;
                    }
                }
                return false;
            };

            struct FxSocketJob {
                size_t fx_index;
                std::string entity_name;
                uint32_t parent_hash;
                uint32_t model_path_hash = 0;
                bool attached = false;
                std::string dummy_name;
                float offset[3] = {0, 0, 0};
            };
            std::vector<FxSocketJob> fx_socket_jobs;

            auto add_attached_fx_for = [&](const Gdb::Placement& p) -> bool {
                bool added_any = false;
                for (uint32_t key : { p.hash_a, p.parent_hash }) {
                    if (!key) continue;
                    auto ait = fx_attach_bindings.find(key);
                    if (ait == fx_attach_bindings.end()) continue;
                    for (const auto& b : ait->second) {
                        if (g_particle_bank_loaded &&
                            !g_particle_bank.find_by_hash(b.fx_hash_lower))
                            continue;

                        Fx::Placement fx;
                        fx.pos[0] = p.x; fx.pos[1] = p.y; fx.pos[2] = p.z;
                        fx.yaw = p.yaw;
                        fx.scale = (p.scale > 0.0001f) ? p.scale : 1.0f;
                        if (p.has_rotation) {
                            fx_game_rotation_matrix(p.rot_x, p.rot_y, p.rot_z,
                                                    fx.rot);
                            fx.has_rot = true;
                        }
                        fx.effect_name = !b.fx_name.empty()
                            ? b.fx_name
                            : p.entity_name;
                        fx.effect_hash = b.fx_hash_lower;
                        const size_t fx_idx = push_fx_dedup(std::move(fx));
                        if (fx_idx == SIZE_MAX) { added_any = true; continue; }

                        if (!p.entity_name.empty()) {
                            FxSocketJob job;
                            job.fx_index = fx_idx;
                            job.entity_name = p.entity_name;
                            job.parent_hash = p.parent_hash;
                            job.model_path_hash = p.model_path_hash;
                            job.attached = true;
                            job.dummy_name = b.dummy_name;
                            job.offset[0] = b.offset[0];
                            job.offset[1] = b.offset[1];
                            job.offset[2] = b.offset[2];
                            fx_socket_jobs.push_back(std::move(job));
                        }
                        added_any = true;
                    }
                    if (added_any) break;
                }
                return added_any;
            };

            size_t fixed_count = 0, var_count = 0, named_count = 0;
            size_t model_hash_count = 0;
            size_t placement_poll = 0;
            size_t fx_by_name = 0, fx_by_binding = 0, fx_by_mapping = 0,
                   fx_by_heuristic = 0;
            for (const auto& p : info.placements) {
                if ((++placement_poll & 0xffu) == 0 &&
                    bail_if_cancelled("placement table build"))
                {
                    return false;
                }

                bool make_fx = false;
                bool from_object = false;
                std::string fx_name = p.entity_name;
                uint32_t fx_hash = 0;
                if (!p.entity_name.empty() && name_hits_bank(p.entity_name)) {
                    make_fx = true;
                    fx_hash = Fx::Fnv1Lower(p.entity_name);
                    ++fx_by_name;
                } else {
                    if (add_attached_fx_for(p)) {
                        ++fx_by_binding;
                    } else {
                        FxBindingChoice choice;
                        std::string mapped = p.entity_name.empty()
                            ? std::string()
                            : map_object_to_effect(p.entity_name);
                        if (binding_for(p.hash_a, p.parent_hash, choice)) {
                            make_fx = true;
                            fx_hash = choice.hash;
                            if (!choice.name.empty()) fx_name = choice.name;
                            from_object = true;
                            ++fx_by_binding;
                        } else if (!mapped.empty() &&
                                   !near_authored_fx(p.x, p.y, p.z)) {
                            make_fx = true;
                            fx_name = mapped;
                            fx_hash = Fx::Fnv1Lower(mapped);
                            from_object = true;
                            ++fx_by_mapping;
                        } else if (!p.entity_name.empty() &&
                                   entity_is_fx(p.entity_name) &&
                                   !near_authored_fx(p.x, p.y, p.z)) {
                            make_fx = true;
                            fx_hash = Fx::Fnv1Lower(p.entity_name);
                            from_object = true;
                            ++fx_by_heuristic;
                        }
                    }
                }
                if (make_fx) {
                    Fx::Placement fx;
                    fx.pos[0] = p.x; fx.pos[1] = p.y; fx.pos[2] = p.z;
                    fx.yaw = p.yaw;
                    fx.scale = (p.scale > 0.0001f) ? p.scale : 1.0f;
                    if (p.has_rotation) {
                        fx_game_rotation_matrix(p.rot_x, p.rot_y, p.rot_z,
                                                fx.rot);
                        fx.has_rot = true;
                    }
                    fx.effect_name = fx_name;
                    fx.effect_hash = fx_hash;
                    const size_t fx_idx = push_fx_dedup(std::move(fx));
                    if (fx_idx != SIZE_MAX && from_object &&
                        !p.entity_name.empty()) {
                        FxSocketJob job;
                        job.fx_index = fx_idx;
                        job.entity_name = p.entity_name;
                        job.parent_hash = p.parent_hash;
                        job.model_path_hash = p.model_path_hash;
                        fx_socket_jobs.push_back(std::move(job));
                    }
                }
                GdbWorldPlacement gp;
                gp.x      = p.x;
                gp.y      = p.y;
                gp.z      = p.z;
                gp.yaw    = p.yaw;
                gp.rot_x  = p.rot_x;
                gp.rot_y  = p.rot_y;
                gp.rot_z  = p.rot_z;
                gp.scale  = p.scale;
                gp.hash   = p.hash_a;
                gp.parent_hash = p.parent_hash;
                gp.model_path_hash = p.model_path_hash;
                gp.marker = p.marker;
                g_level_gdb_placements.push_back(gp);
                if (p.marker == 0x00004B40) ++fixed_count;
                else                         ++var_count;
                if (!p.entity_name.empty())  ++named_count;
                if (p.model_path_hash != 0)  ++model_hash_count;
            }
            std::ostringstream os;
            os << "gdb: " << g_level_gdb_placements.size()
               << " placements (" << fixed_count << " fixed + "
               << var_count << " variable, "
               << named_count << " resolved to .save entity names, "
               << model_hash_count << " with model path hashes)";
            OutputLog::success(os.str());
            if (!g_pending_level_fx.empty()) {
                OutputLog::success("fx: " +
                    std::to_string(g_pending_level_fx.size()) +
                    " particle effect placement(s) in level (" +
                    std::to_string(fx_by_name) + " by name, " +
                    std::to_string(fx_by_binding) + " by GDB binding, " +
                    std::to_string(fx_by_mapping) + " by object-map, " +
                    std::to_string(fx_by_heuristic) + " by heuristic)");
            }

            struct GdbStreamingChoiceCacheValue {
                const StreamingModelCandidate* hit = nullptr;
                int score = INT_MIN;
            };
            std::unordered_map<std::string, GdbStreamingChoiceCacheValue>
                gdb_streaming_choice_cache;
            auto choose_streaming_cached =
                [&](const std::string& entity_name,
                    uint32_t parent_hash,
                    int* out_score) -> const StreamingModelCandidate* {
                    if (streaming_model_candidates.empty()) {
                        if (out_score) *out_score = INT_MIN;
                        return nullptr;
                    }
                    std::string cache_key = std::to_string(parent_hash);
                    cache_key.push_back('|');
                    cache_key += entity_name;
                    auto cached =
                        gdb_streaming_choice_cache.find(cache_key);
                    if (cached != gdb_streaming_choice_cache.end()) {
                        if (out_score) *out_score = cached->second.score;
                        return cached->second.hit;
                    }

                    int score = INT_MIN;
                    const StreamingModelCandidate* hit =
                        choose_streaming_model_for_gdb(
                            entity_name, streaming_model_candidates,
                            &score, parent_hash);
                    gdb_streaming_choice_cache.emplace(
                        std::move(cache_key),
                        GdbStreamingChoiceCacheValue{hit, score});
                    if (out_score) *out_score = score;
                    return hit;
                };

            std::unordered_map<size_t, std::string> fx_socket_diag;
            if (!fx_socket_jobs.empty()) {
                struct SocketCache {
                    bool has = false;
                    float p[3] = {0,0,0};
                    float q[4] = {0,0,0,1};
                };
                std::unordered_map<std::string, SocketCache> socket_cache;
                size_t resolved_sockets = 0;
                size_t attached_sockets = 0;
                size_t missing_attached_sockets = 0;
                auto rotate_by_quat = [](const float q[4],
                                         const float v[3],
                                         float out[3]) {
                    const float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
                    const float ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
                    const float tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
                    out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
                    out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
                    out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
                };

                std::unordered_map<uint32_t, const FlatAssetEntry*> mdl_by_hash;
                mdl_by_hash.reserve(S.all_mdl_files.size() * 2);
                for (const auto& m : S.all_mdl_files) {
                    if (m.full_path.empty()) continue;
                    mdl_by_hash.emplace(fnv1_model_path_hash(m.full_path), &m);
                }

                std::unordered_map<std::string, std::pair<std::string, int>>
                    gmd_by_mdl;
                for (const auto& c : streaming_model_candidates) {
                    if (c.from_gmd && c.gmd_file_index >= 0 &&
                        !c.gmd_bnk_path.empty()) {
                        gmd_by_mdl.emplace(
                            c.hint_lower,
                            std::make_pair(c.gmd_bnk_path, c.gmd_file_index));
                    }
                }
                OutputLog::info("fx: gmd index: " +
                                std::to_string(gmd_by_mdl.size()) +
                                " model(s) with .gmd scene data");
                std::unordered_map<std::string, std::vector<GmdFxAttachment>>
                    gmd_cache;
                auto gmd_attachments_for =
                    [&](const FlatAssetEntry* he_,
                        const StreamingModelCandidate* cand_,
                        std::string* why)
                    -> const std::vector<GmdFxAttachment>* {
                    std::string bnk;
                    int idx = -1;
                    if (cand_ && cand_->from_gmd &&
                        cand_->gmd_file_index >= 0) {
                        bnk = cand_->gmd_bnk_path;
                        idx = cand_->gmd_file_index;
                    } else if (he_) {
                        auto git = gmd_by_mdl.find(lower_slash(he_->full_path));
                        if (git != gmd_by_mdl.end()) {
                            bnk = git->second.first;
                            idx = git->second.second;
                        } else if (why) {
                            *why = " gmd-miss('" +
                                   lower_slash(he_->full_path) + "')";
                        }
                    } else if (why) {
                        *why = " gmd-nosrc";
                    }
                    if (idx < 0 || bnk.empty()) return nullptr;
                    std::string key = bnk + "#" + std::to_string(idx);
                    auto cit = gmd_cache.find(key);
                    if (cit == gmd_cache.end()) {
                        std::vector<GmdFxAttachment> atts;
                        try {
                            std::vector<uint8_t> gbytes =
                                BnkCache::extract_bytes(bnk, idx);
                            ParseGmdFxAttachments(gbytes, atts);
                        } catch (...) {
                        }
                        cit = gmd_cache.emplace(std::move(key),
                                                std::move(atts)).first;
                    }
                    return cit->second.empty() ? nullptr : &cit->second;
                };

                std::vector<Fx::Placement> gmd_extra_fx;
                size_t gmd_fx_applied = 0;
                for (const auto& job : fx_socket_jobs) {
                    if (job.fx_index >= g_pending_level_fx.size()) continue;
                    std::string& diag = fx_socket_diag[job.fx_index];
                    diag = "ent='" + job.entity_name + "' ";

                    const FlatAssetEntry* he = nullptr;
                    if (job.model_path_hash) {
                        auto hit = mdl_by_hash.find(job.model_path_hash);
                        if (hit != mdl_by_hash.end()) he = hit->second;
                    }
                    const StreamingModelCandidate* cand = he
                        ? nullptr
                        : choose_streaming_cached(job.entity_name,
                                                  job.parent_hash, nullptr);
                    if (he) {
                        diag += "byhash '" + he->full_path + "' @'" +
                                he->bnk_path + "'#" +
                                std::to_string(he->file_index);
                    } else if (!cand) {
                        diag += "cand=NULL (hash=" +
                                std::to_string(job.model_path_hash) + ")";
                    } else if (cand->from_gmd) {
                        diag += "cand gmd='" + cand->gmd_bnk_path + "'#" +
                                std::to_string(cand->gmd_file_index);
                    } else if (cand->entry) {
                        diag += "cand bnk='" + cand->entry->bnk_path + "'#" +
                                std::to_string(cand->entry->file_index);
                    } else {
                        diag += "cand (no entry/gmd)";
                    }

                    if (!job.attached) {
                        std::string gmd_why;
                        const std::vector<GmdFxAttachment>* atts =
                            gmd_attachments_for(he, cand, &gmd_why);
                        if (!atts && !gmd_why.empty()) diag += gmd_why;
                        else if (!atts) diag += " gmd-empty";
                        if (atts) {
                            bool applied = false;
                            Fx::Placement base_copy =
                                g_pending_level_fx[job.fx_index];
                            for (const auto& att : *atts) {
                                const uint32_t ah = Fx::Fnv1Lower(att.effect);
                                if (!g_particle_bank_loaded ||
                                    !g_particle_bank.find_by_hash(ah))
                                    continue;
                                if (!applied) {
                                    auto& fx =
                                        g_pending_level_fx[job.fx_index];
                                    fx.effect_name = att.effect;
                                    fx.effect_hash = ah;
                                    fx.socket[0] = att.pos[0];
                                    fx.socket[1] = att.pos[1];
                                    fx.socket[2] = att.pos[2];
                                    applied = true;
                                } else {
                                    Fx::Placement extra = base_copy;
                                    extra.effect_name = att.effect;
                                    extra.effect_hash = ah;
                                    extra.socket[0] = att.pos[0];
                                    extra.socket[1] = att.pos[1];
                                    extra.socket[2] = att.pos[2];
                                    gmd_extra_fx.push_back(std::move(extra));
                                }
                                diag += " gmd-fx('" + att.effect + "' @" +
                                        std::to_string(att.pos[0]) + "," +
                                        std::to_string(att.pos[1]) + "," +
                                        std::to_string(att.pos[2]) + ")";
                            }
                            if (applied) {
                                ++gmd_fx_applied;
                                ++resolved_sockets;
                                continue;
                            }
                        }
                    }
                    SocketCache sc;
                    if (job.attached && job.dummy_name.empty()) {
                        sc.has = true;
                    } else if (he || cand) {
                        std::string cache_key = he
                            ? (he->bnk_path + "#" +
                               std::to_string(he->file_index))
                            : std::to_string(
                                  reinterpret_cast<uintptr_t>(cand));
                        cache_key.push_back('|');
                        cache_key += job.attached ? job.dummy_name
                                                  : std::string();
                        auto cached = socket_cache.find(cache_key);
                        if (cached != socket_cache.end()) {
                            sc = cached->second;
                        } else {
                            std::vector<uint8_t> mbytes;
                            try {
                                if (he) {

                                    build_mdl_buffer_for_name_with_body(
                                        he->full_path, he->bnk_path, mbytes);
                                } else if (cand->from_gmd &&
                                    cand->gmd_file_index >= 0 &&
                                    !cand->gmd_bnk_path.empty()) {
                                    mbytes = BnkCache::extract_bytes(
                                        cand->gmd_bnk_path,
                                        cand->gmd_file_index);
                                } else if (cand->entry) {
                                    mbytes = BnkCache::extract_bytes(
                                        cand->entry->bnk_path,
                                        cand->entry->file_index);
                                }
                            } catch (...) {
                            }
                            diag += " mbytes=" + std::to_string(mbytes.size());
                            if (mbytes.size() >= 16) {
                                char hx[80];
                                std::snprintf(hx, sizeof hx,
                                    " magic=%c%c%c%c hs=%u b32=%u",
                                    mbytes[0], mbytes[1], mbytes[2], mbytes[3],
                                    (unsigned)((mbytes[12] << 24) |
                                        (mbytes[13] << 16) |
                                        (mbytes[14] << 8) | mbytes[15]),
                                    mbytes.size() >= 36 ? (unsigned)(
                                        (mbytes[32] << 24) | (mbytes[33] << 16) |
                                        (mbytes[34] << 8) | mbytes[35]) : 0u);
                                diag += hx;
                            }
                            if (!mbytes.empty()) {
                                if (job.attached) {
                                    if (ReadMdlSocket(mbytes, job.dummy_name,
                                                      false, sc.p, sc.q))
                                        sc.has = true;
                                    diag += sc.has ? " ReadMdlSocket=OK"
                                                   : " ReadMdlSocket=FAIL";
                                } else if (ReadMdlFxSocket(mbytes, sc.p)) {
                                    sc.has = true;
                                    diag += " ReadMdlFxSocket=OK";
                                } else {
                                    diag += " ReadMdlFxSocket=FAIL";
                                }
                            }
                            socket_cache.emplace(std::move(cache_key), sc);
                        }
                    }
                    float socket[3] = { sc.p[0], sc.p[1], sc.p[2] };
                    if (sc.has && job.attached) {
                        float off[3] = {
                            job.offset[0], job.offset[1], job.offset[2]
                        };
                        float roff[3] = {0, 0, 0};
                        rotate_by_quat(sc.q, off, roff);
                        socket[0] += roff[0];
                        socket[1] += roff[1];
                        socket[2] += roff[2];
                    }
                    if (sc.has) {
                        auto& fx = g_pending_level_fx[job.fx_index];
                        fx.socket[0] = socket[0];
                        fx.socket[1] = socket[1];
                        fx.socket[2] = socket[2];
                        if (job.attached) ++attached_sockets;
                        else ++resolved_sockets;
                    } else if (job.attached) {
                        ++missing_attached_sockets;
                    }
                }

                for (auto& extra : gmd_extra_fx) {
                    bool dup = false;
                    for (const auto& e : g_pending_level_fx) {
                        if (e.effect_hash != extra.effect_hash) continue;

                        const float dx = (e.pos[0] + e.socket[0]) -
                                         (extra.pos[0] + extra.socket[0]);
                        const float dy = (e.pos[1] + e.socket[1]) -
                                         (extra.pos[1] + extra.socket[1]);
                        const float dz = (e.pos[2] + e.socket[2]) -
                                         (extra.pos[2] + extra.socket[2]);
                        if (dx * dx + dy * dy + dz * dz < 0.25f) {
                            dup = true;
                            break;
                        }
                    }
                    if (!dup) g_pending_level_fx.push_back(std::move(extra));
                }
                if (gmd_fx_applied || !gmd_extra_fx.empty()) {
                    OutputLog::success("fx: " +
                        std::to_string(gmd_fx_applied) +
                        " prop FX from authored .gmd attachments (+" +
                        std::to_string(gmd_extra_fx.size()) +
                        " extra attachment(s))");
                }
                if (resolved_sockets > 0 || attached_sockets > 0 ||
                    missing_attached_sockets > 0) {
                    OutputLog::info("fx: resolved " +
                        std::to_string(resolved_sockets) + "/" +
                        std::to_string(fx_socket_jobs.size()) +
                        " generic FX socket(s), " +
                        std::to_string(attached_sockets) +
                        " ParticleAttacher dummy socket(s), " +
                        std::to_string(missing_attached_sockets) +
                        " missing authored dummy socket(s)");
                }
            }
