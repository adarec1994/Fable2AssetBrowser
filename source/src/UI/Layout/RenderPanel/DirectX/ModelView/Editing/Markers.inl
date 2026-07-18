    if (S.terrain_mode) {
        draw_gdb_placements_overlay(origin, region);
    }
    draw_spawn_markers_overlay(origin, region, hovered);
    if (g_marker_clear_selection) {
        g_marker_clear_selection = false;
        DetailsPanel::ClearSelection();
        ::g_selected_level_pick_id = 0;
        ::g_selected_level_mesh_idx = -1;
        ::g_selected_level_hash = 0;
    }

    if (DetailsPanel::WaterSelected()) {
        g_sel_spawn_marker = -1;
        g_sel_pending_sp = -1;
        g_sel_pending_gen = -1;
    }

    if (g_sel_spawn_marker >= 0 &&
        g_sel_spawn_marker < (int)g_level_spawn_markers.size() &&
        level_marker_visible(
            g_level_spawn_markers[(size_t)g_sel_spawn_marker])) {
        const auto& mk =
            g_level_spawn_markers[(size_t)g_sel_spawn_marker];
        const Gdb::EntityGameplayDetails* marker_gameplay = nullptr;
        const uint32_t gameplay_hash = mk.creature_entity_hash != 0
            ? mk.creature_entity_hash : mk.entity_hash;
        auto gameplay_it = g_level_entity_gameplay.find(gameplay_hash);
        if (gameplay_it != g_level_entity_gameplay.end()) {
            marker_gameplay = &gameplay_it->second;
        }
        uint32_t spawn_owner_entity = 0;
        uint32_t spawn_owner_list = 0;
        if (mk.kind == 2) {
            for (const auto& candidate : g_level_spawn_markers) {
                if (candidate.kind != 1 ||
                    !candidate.spawn_points_record) {
                    continue;
                }
                if (std::find(candidate.spawn_point_entities.begin(),
                              candidate.spawn_point_entities.end(),
                              mk.entity_hash) !=
                    candidate.spawn_point_entities.end()) {
                    spawn_owner_entity = candidate.entity_hash;
                    spawn_owner_list = candidate.spawn_points_record;
                    break;
                }
            }
        }
        const uint32_t edit_id = 0x70000000u |
                                 uint32_t(g_sel_spawn_marker);
        const bool pending_addition = mk.pending_addition_index >= 0;
        const bool editable = LevelEdit::Enabled() &&
                              !LevelEdit::Saving() &&
                              (pending_addition || mk.pos_off[0] ||
                               mk.pos_off[1] ||
                               mk.pos_off[2]);
        const bool spawn_deletable =
            LevelEdit::Enabled() && !LevelEdit::Saving() &&
            mk.kind == 2 && spawn_owner_list != 0;
        const bool entity_deletable =
            LevelEdit::Enabled() && !LevelEdit::Saving() &&
            mk.kind != 2 && !is_player_start_marker(mk) &&
            (mk.entity_hash != 0 || pending_addition);
        auto queue_entity_delete = [&]() {
            LevelEdit::InstInfo info;
            const float orig[3] = {mk.x, mk.y, mk.z};
            info.orig_pos = orig;
            info.gdb_off = mk.pos_off;
            info.gdb_rot_off = mk.rot_off;
            info.gdb_entity_hash = mk.entity_hash;
            if (pending_addition) {
                info.lev_off = uint32_t(mk.pending_addition_index + 1);
                info.lev_kind = 5;
            }
            LevelEdit::PushUndoSnapshot({edit_id});
            LevelEdit::SetDeleted(edit_id, info);
            OutputLog::info("level edit: entity deletion queued");
            if (pending_addition) {
                g_level_spawn_markers[size_t(g_sel_spawn_marker)].kind = 0;
            }
            g_sel_spawn_marker = -1;
        };
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            g_sel_spawn_marker = -1;
        } else if (!ImGui::GetIO().WantTextInput &&
                   ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            if (spawn_deletable) {
                LevelEdit::RemoveSpawnPointFromExisting(
                    spawn_owner_entity, spawn_owner_list,
                    mk.entity_hash);
                OutputLog::info(
                    "level edit: spawn point deletion queued");
                g_sel_spawn_marker = -1;
            } else if (entity_deletable) {
                queue_entity_delete();
            }
        }
        const float kMarkerW =
            (mk.is_container || marker_gameplay) ? 320.0f : 220.0f;
        ImGui::SetNextWindowPos(
            ImVec2(origin.x + region.x - kMarkerW - 8.0f,
                   origin.y + 6.0f));
        ImGui::SetNextWindowSize(ImVec2(kMarkerW, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.78f);
        ImGuiWindowFlags mfl = ImGuiWindowFlags_NoTitleBar |
                               ImGuiWindowFlags_NoResize |
                               ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoCollapse |
                               ImGuiWindowFlags_NoSavedSettings |
                               ImGuiWindowFlags_AlwaysAutoResize;
        if (g_sel_spawn_marker >= 0 &&
            ImGui::Begin("##spawn_marker_editor", nullptr, mfl)) {
            float mpos[3] = {mk.x, mk.y, mk.z};
            {
                float d_pos[3], d_rot[3];
                if (LevelEdit::EditFor(edit_id, d_pos, d_rot)) {
                    mpos[0] += d_pos[0];
                    mpos[1] += d_pos[1];
                    mpos[2] += d_pos[2];
                }
            }
            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                               "Position:");
            static const char* kMAxis[3] = {"X##mpos", "Y##mpos",
                                            "Z##mpos"};
            float edit_pos[3] = {mpos[0], mpos[1], mpos[2]};
            bool commit = false;
            if (editable) {
                for (int a = 0; a < 3; ++a) {
                    ImGui::SetNextItemWidth(120.0f);
                    ImGui::InputFloat(kMAxis[a], &edit_pos[a], 0.0f,
                                      0.0f, "%.3f");
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        commit = true;
                    }
                }
            } else {
                ImGui::Text("X: %.3f", mpos[0]);
                ImGui::Text("Y: %.3f", mpos[1]);
                ImGui::Text("Z: %.3f", mpos[2]);
            }
            if (commit) {
                const float step[3] = {edit_pos[0] - mpos[0],
                                       edit_pos[1] - mpos[1],
                                       edit_pos[2] - mpos[2]};
                if (step[0] != 0 || step[1] != 0 || step[2] != 0) {
                    LevelEdit::InstInfo info;
                    const float orig[3] = {mk.x, mk.y, mk.z};
                    info.orig_pos = orig;
                    info.gdb_off = mk.pos_off;
                    info.gdb_rot_off = mk.rot_off;
                    info.gdb_entity_hash = mk.entity_hash;
                    if (pending_addition) {
                        info.lev_off =
                            uint32_t(mk.pending_addition_index + 1);
                        info.lev_kind = 5;
                    }
                    LevelEdit::AddMove(edit_id, step, info);
                }
            }

            ImGui::Spacing();
            const char* kind_name =
                mk.kind == 1 ? "Creature generator"
                : mk.kind == 2 ? "Spawn point"
                : mk.kind == 4 ? "Dig spot"
                : mk.kind == 5 ? "Container"
                : mk.kind == 6 ? "Static prop"
                               : "NPC / creature";
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.55f, 1.0f), "%s",
                               kind_name);
            if (!mk.name.empty()) {
                ImGui::TextUnformatted(mk.name.c_str());
            }
            if (mk.kind == 1 && !mk.creature_name.empty()) {
                ImGui::TextDisabled("spawns: %s",
                                    mk.creature_name.c_str());
            }
            if (marker_gameplay) {
                draw_entity_gameplay_details(*marker_gameplay);
            }
            if (pending_addition) {
                draw_addition_container_details(
                    mk.pending_addition_index);
            } else if (mk.kind == 4 || mk.is_container) {
                draw_level_container_details(mk.entity_hash);
            }
            if (mk.kind == 2 && spawn_deletable &&
                ImGui::Button("Delete spawn point")) {
                LevelEdit::RemoveSpawnPointFromExisting(
                    spawn_owner_entity, spawn_owner_list,
                    mk.entity_hash);
                OutputLog::info(
                    "level edit: spawn point deletion queued");
                g_sel_spawn_marker = -1;
            }
            if (mk.kind != 2 && entity_deletable &&
                ImGui::Button(mk.kind == 1 ? "Delete generator"
                                           : pending_addition
                                               ? (mk.kind == 4
                                                      ? "Delete dig spot"
                                                      : mk.kind == 3
                                                            ? "Delete NPC"
                                                            : mk.kind == 6
                                                                  ? "Delete static prop"
                                                            : "Delete container")
                                               : "Delete entity")) {
                queue_entity_delete();
            }
            if (mk.kind == 1) {
                const bool can_add_sp =
                    LevelEdit::Enabled() && !LevelEdit::Saving() &&
                    mk.spawn_points_record != 0;
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Spawn points (%zu):",
                                   mk.spawn_point_entities.size());
                for (size_t si = 0;
                     si < mk.spawn_point_entities.size(); ++si) {
                    const uint32_t sph = mk.spawn_point_entities[si];
                    if (LevelEdit::SpawnPointRemovalPending(sph)) {
                        continue;
                    }
                    int target = -1;
                    for (size_t mj = 0;
                         mj < g_level_spawn_markers.size(); ++mj) {
                        if (g_level_spawn_markers[mj].entity_hash ==
                            sph) {
                            target = int(mj);
                            break;
                        }
                    }
                    ImGui::PushID(int(si) + 0x3000);
                    char lbl[64];
                    std::snprintf(lbl, sizeof(lbl), "0x%08X%s", sph,
                                  target < 0 ? " (no marker)" : "");
                    if (ImGui::Selectable(
                            lbl, target == g_sel_spawn_marker) &&
                        target >= 0) {
                        g_sel_spawn_marker = target;
                    }
                    if (can_add_sp) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Delete")) {
                            LevelEdit::RemoveSpawnPointFromExisting(
                                mk.entity_hash, mk.spawn_points_record,
                                sph);
                            if (target == g_sel_spawn_marker) {
                                g_sel_spawn_marker = -1;
                            }
                            OutputLog::info(
                                "level edit: spawn point deletion "
                                "queued");
                        }
                    }
                    ImGui::PopID();
                }
                if (can_add_sp &&
                    ImGui::SmallButton("+ Add spawn point")) {
                    const float n =
                        float(mk.spawn_point_entities.size() +
                              LevelEdit::PendingSpawnPointCount());
                    const float ang = n * 1.0471976f;
                    const float rad = 1.5f + 0.3f * n;
                    const float sp_pos[3] = {
                        mpos[0] + std::cos(ang) * rad,
                        mpos[1] + std::sin(ang) * rad,
                        mpos[2]};
                    LevelEdit::AddSpawnPointToExisting(
                        mk.entity_hash, mk.spawn_points_record,
                        sp_pos);
                    OutputLog::success(
                        "level edit: spawn point queued next to the "
                        "generator (authored on Save; move it after "
                        "reload)");
                }
                if (LevelEdit::PendingSpawnPointCount() > 0) {
                    ImGui::TextDisabled(
                        "%zu pending spawn point(s)",
                        LevelEdit::PendingSpawnPointCount());
                }
            }
            ImGui::End();
        } else if (g_sel_spawn_marker >= 0) {
            ImGui::End();
        }

        if (g_sel_spawn_marker >= 0 && editable) {
            float gpos[3] = {mk.x, mk.y, mk.z};
            {
                float d_pos[3], d_rot[3];
                if (LevelEdit::EditFor(edit_id, d_pos, d_rot)) {
                    gpos[0] += d_pos[0];
                    gpos[1] += d_pos[1];
                    gpos[2] += d_pos[2];
                }
            }
            LevelGizmo::Result gz = LevelGizmo::DrawAndHandle(
                g_flycam, origin, region, gpos, true);
            static bool s_mk_dragging = false;
            if (gz.dragging && !s_mk_dragging) {
                LevelEdit::PushUndoSnapshot({edit_id});
            }
            s_mk_dragging = gz.dragging;
            if (gz.moved &&
                (gz.step[0] != 0.0f || gz.step[1] != 0.0f ||
                 gz.step[2] != 0.0f)) {
                LevelEdit::InstInfo info;
                const float orig[3] = {mk.x, mk.y, mk.z};
                info.orig_pos = orig;
                info.gdb_off = mk.pos_off;
                info.gdb_rot_off = mk.rot_off;
                info.gdb_entity_hash = mk.entity_hash;
                LevelEdit::AddMove(edit_id, gz.step, info);
            }
        }
    }

    if (g_sel_pending_sp >= 0 && S.show_spawn_markers) {
        std::vector<LevelEdit::PendingSpawnPoint> psps;
        LevelEdit::GetPendingSpawnPoints(psps);
        const LevelEdit::PendingSpawnPoint* sel_sp = nullptr;
        for (const auto& sp : psps) {
            if (sp.id == g_sel_pending_sp) {
                sel_sp = &sp;
                break;
            }
        }
        if (!sel_sp) {
            g_sel_pending_sp = -1;
        } else {
            const bool sp_editable =
                LevelEdit::Enabled() && !LevelEdit::Saving();
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                g_sel_pending_sp = -1;
            } else if (sp_editable && !ImGui::GetIO().WantTextInput &&
                       ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                LevelEdit::RemovePendingSpawnPoint(sel_sp->id);
                OutputLog::info(
                    "level edit: pending spawn point removed");
                g_sel_pending_sp = -1;
                sel_sp = nullptr;
            }
        }
        if (sel_sp) {
            const bool sp_editable =
                LevelEdit::Enabled() && !LevelEdit::Saving();
            if (!details_panel_docked()) {
            const float kSpW = 220.0f;
            ImGui::SetNextWindowPos(
                ImVec2(origin.x + region.x - kSpW - 8.0f,
                       origin.y + 6.0f));
            ImGui::SetNextWindowSize(ImVec2(kSpW, 0), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.78f);
            ImGuiWindowFlags sfl = ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_AlwaysAutoResize;
            if (ImGui::Begin("##pending_sp_editor", nullptr, sfl)) {
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Position:");
                float ep[3] = {sel_sp->pos[0], sel_sp->pos[1],
                               sel_sp->pos[2]};
                bool commit = false;
                if (sp_editable) {
                    static const char* kAx[3] = {"X##psp", "Y##psp",
                                                 "Z##psp"};
                    for (int a = 0; a < 3; ++a) {
                        ImGui::SetNextItemWidth(120.0f);
                        ImGui::InputFloat(kAx[a], &ep[a], 0.0f, 0.0f,
                                          "%.3f");
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            commit = true;
                        }
                    }
                } else {
                    ImGui::Text("X: %.3f", ep[0]);
                    ImGui::Text("Y: %.3f", ep[1]);
                    ImGui::Text("Z: %.3f", ep[2]);
                }
                if (commit) {
                    LevelEdit::MovePendingSpawnPoint(sel_sp->id, ep);
                }
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.9f, 0.75f, 1.0f, 1.0f),
                                   "%s", sel_sp->label.c_str());
                ImGui::TextDisabled("unsaved - authored on Save");
                if (sp_editable &&
                    ImGui::Button("Delete spawn point")) {
                    LevelEdit::RemovePendingSpawnPoint(sel_sp->id);
                    OutputLog::info(
                        "level edit: pending spawn point removed");
                    g_sel_pending_sp = -1;
                }
            }
            ImGui::End();
            }   

            if (sp_editable && g_sel_pending_sp >= 0) {
                float gpos[3] = {sel_sp->pos[0], sel_sp->pos[1],
                                 sel_sp->pos[2]};
                LevelGizmo::Result gz = LevelGizmo::DrawAndHandle(
                    g_flycam, origin, region, gpos, true);
                if (gz.moved &&
                    (gz.step[0] != 0.0f || gz.step[1] != 0.0f ||
                     gz.step[2] != 0.0f)) {
                    const float np[3] = {gpos[0] + gz.step[0],
                                         gpos[1] + gz.step[1],
                                         gpos[2] + gz.step[2]};
                    LevelEdit::MovePendingSpawnPoint(sel_sp->id, np);
                }
            }
        }
    }

    if (g_sel_pending_gen >= 0 && S.show_spawn_markers) {
        std::vector<LevelEdit::GeneratorAddition> pgens;
        LevelEdit::GetGenerators(pgens);
        const bool gen_valid =
            g_sel_pending_gen < (int)pgens.size() &&
            !pgens[(size_t)g_sel_pending_gen].removed;
        if (!gen_valid) {
            g_sel_pending_gen = -1;
        } else {
            const auto& pg = pgens[(size_t)g_sel_pending_gen];
            const bool gen_editable =
                LevelEdit::Enabled() && !LevelEdit::Saving();
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                g_sel_pending_gen = -1;
            } else if (gen_editable && !ImGui::GetIO().WantTextInput &&
                       ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                LevelEdit::RemoveGenerator(g_sel_pending_gen);
                g_sel_pending_gen = -1;
            }
            if (g_sel_pending_gen >= 0) {
                if (!details_panel_docked()) {
                const float kGenW = 220.0f;
                ImGui::SetNextWindowPos(
                    ImVec2(origin.x + region.x - kGenW - 8.0f,
                           origin.y + 6.0f));
                ImGui::SetNextWindowSize(ImVec2(kGenW, 0),
                                         ImGuiCond_Always);
                ImGui::SetNextWindowBgAlpha(0.78f);
                ImGuiWindowFlags gfl =
                    ImGuiWindowFlags_NoTitleBar |
                    ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_AlwaysAutoResize;
                if (ImGui::Begin("##pending_gen_editor", nullptr,
                                 gfl)) {
                    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                       "Position:");
                    float ep[3] = {pg.pos[0], pg.pos[1], pg.pos[2]};
                    bool commit = false;
                    if (gen_editable) {
                        static const char* kAx[3] = {
                            "X##pgen", "Y##pgen", "Z##pgen"};
                        for (int a = 0; a < 3; ++a) {
                            ImGui::SetNextItemWidth(120.0f);
                            ImGui::InputFloat(kAx[a], &ep[a], 0.0f,
                                              0.0f, "%.3f");
                            if (ImGui::IsItemDeactivatedAfterEdit()) {
                                commit = true;
                            }
                        }
                    } else {
                        ImGui::Text("X: %.3f", ep[0]);
                        ImGui::Text("Y: %.3f", ep[1]);
                        ImGui::Text("Z: %.3f", ep[2]);
                    }
                    if (commit) {
                        LevelEdit::MovePendingGenerator(
                            g_sel_pending_gen, ep);
                    }
                    ImGui::Spacing();
                    ImGui::TextColored(
                        ImVec4(1.0f, 0.75f, 0.55f, 1.0f),
                        "Creature generator (unsaved)");
                    ImGui::TextDisabled("spawns: %s",
                                        pg.creature_name.c_str());
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                       "Spawn points (%zu):",
                                       pg.spawn_points.size());
                    for (size_t si = 0; si < pg.spawn_points.size();
                         ++si) {
                        ImGui::PushID(int(si) + 0x4000);
                        char lbl[48];
                        std::snprintf(lbl, sizeof(lbl),
                                      "#%zu (%.1f, %.1f, %.1f)",
                                      si + 1, pg.spawn_points[si][0],
                                      pg.spawn_points[si][1],
                                      pg.spawn_points[si][2]);
                        const int sp_id =
                            (g_sel_pending_gen << 8) | int(si);
                        if (ImGui::Selectable(
                                lbl, g_sel_pending_sp == sp_id)) {
                            g_sel_pending_sp = sp_id;
                            g_sel_pending_gen = -1;
                        }
                        ImGui::PopID();
                    }
                    if (gen_editable &&
                        ImGui::SmallButton("+ Add spawn point")) {
                        const float n = float(pg.spawn_points.size());
                        const float ang = n * 1.0471976f;
                        const float rad = 1.5f + 0.3f * n;
                        const float sp_pos[3] = {
                            pg.pos[0] + std::cos(ang) * rad,
                            pg.pos[1] + std::sin(ang) * rad,
                            pg.pos[2]};
                        LevelEdit::AddGeneratorSpawnPoint(
                            g_sel_pending_gen, sp_pos);
                    }
                    ImGui::TextDisabled("unsaved - authored on Save");
                    if (gen_editable &&
                        ImGui::Button("Delete generator")) {
                        LevelEdit::RemoveGenerator(g_sel_pending_gen);
                        OutputLog::info(
                            "level edit: unsaved generator removed");
                        g_sel_pending_gen = -1;
                    }
                }
                ImGui::End();
                }   

                if (gen_editable && g_sel_pending_gen >= 0) {
                    float gpos[3] = {pg.pos[0], pg.pos[1], pg.pos[2]};
                    LevelGizmo::Result gz = LevelGizmo::DrawAndHandle(
                        g_flycam, origin, region, gpos, true);
                    if (gz.moved &&
                        (gz.step[0] != 0.0f || gz.step[1] != 0.0f ||
                         gz.step[2] != 0.0f)) {
                        const float np[3] = {gpos[0] + gz.step[0],
                                             gpos[1] + gz.step[1],
                                             gpos[2] + gz.step[2]};
                        LevelEdit::MovePendingGenerator(
                            g_sel_pending_gen, np);
                    }
                }
            }
        }
    }

    float water_position[3] = {};
    if (DetailsPanel::SelectedWaterPosition(water_position)) {
        LevelGizmo::SetMode(LevelGizmo::Mode::Translate);
        LevelGizmo::Result gz = LevelGizmo::DrawAndHandle(
            g_flycam, origin, region, water_position,
            !Level::IsAsyncLoadInProgress());
        if (gz.moved &&
            (gz.step[0] != 0.0f || gz.step[1] != 0.0f ||
             gz.step[2] != 0.0f)) {
            DetailsPanel::MoveSelectedWater(gz.step);
        }
    }
