    if (g_mp.no_tilt && LevelEdit::Enabled() && hovered &&
        !g_flycam.is_looking && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
            LevelGizmo::SetMode(LevelGizmo::Mode::Translate);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
            LevelGizmo::SetMode(LevelGizmo::Mode::Rotate);
        }
    }

    if (LevelEdit::Enabled() && !ImGui::GetIO().WantTextInput &&
        (ImGui::GetIO().KeyAlt || ImGui::GetIO().KeyCtrl) &&
        ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (!LevelEdit::Undo()) {
            OutputLog::info("level edit: nothing to undo");
        }
    }

    if (hovered && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (S.bone_rotate_mode) {
            cancel_rotate();
        } else if (g_mp.no_tilt && ::g_selected_level_mesh_idx >= 0) {
            ::g_selected_level_mesh_idx = -1;
            ::g_selected_level_pick_id = 0;
            ::g_selected_level_hash = 0;
            LevelGizmo::CancelDrag();
        } else if (g_mp.no_tilt) {

        } else {
            if (S.content_tabs_visible && ContentTabs::HasTabs()) {
                ContentTabs::CloseActive();
            } else {
                MP_Release(g_mp);
                g_mp.has_model = false;
                g_mp_initialized = false;
                S.show_model_preview = false;
                S.model_preview_open = false;
                S.selected_bone = -1;
            }
        }
    }

    dl->AddRectFilled(ImVec2(origin.x + 6, origin.y + 6),
                      ImVec2(origin.x + 196, origin.y + 70),
                      IM_COL32(20, 22, 28, 200), 4.0f);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 12));
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Controls");
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 30));
    ImGui::TextDisabled("L-Drag  rotate");
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 46));
    ImGui::TextDisabled("Wheel  zoom  /  ESC  close");

    float next_overlay_y = origin.y + 76.0f;

    
    
    const bool custom_level_clean_viewport = details_panel_docked();

    bool has_skeleton = g_mp.has_model && g_mp.bone_count > 0 &&
                        !custom_level_clean_viewport;
    if (has_skeleton) {

        static float s_skel_alpha    = 0.30f;

        const float kIdleAlpha   = 0.30f;
        const float kHoverAlpha  = 1.00f;

        const ImVec2 win_pos (origin.x + 6, origin.y + 76);
        const ImVec2 win_size(190, 0);
        ImGui::SetNextWindowPos(win_pos);
        ImGui::SetNextWindowSize(win_size, ImGuiCond_Always);

        ImGui::SetNextWindowBgAlpha(s_skel_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_skel_alpha);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##skeleton_overlay", nullptr, fl)) {

            bool hovering = ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                ImGuiHoveredFlags_ChildWindows);
            float target = hovering ? kHoverAlpha : kIdleAlpha;

            s_skel_alpha += (target - s_skel_alpha) * 0.18f;
            if (std::fabs(s_skel_alpha - target) < 0.005f) s_skel_alpha = target;

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Skeleton");
            ImGui::Checkbox("Show", &::g_skel_overlay_show);
            if (S.selected_bone >= 0 && S.selected_bone < (int)S.mdl_info.Bones.size()) {

                ImGui::TextDisabled(S.bone_rotate_mode
                                        ? "RMB cancel  /  LMB confirm"
                                        : "R: rotate selected");
            }

            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            next_overlay_y = wp.y + ws.y + 6.0f;
        }
        ImGui::End();
        ImGui::PopStyleVar();

        if (!::g_skel_overlay_show) {
            S.selected_bone     = -1;
            S.bone_rotate_mode  = false;
        }

        if (::g_skel_overlay_show) {
            draw_skeleton_overlay(origin, region);
        }
    } else {

        ::g_skel_overlay_show = false;
        S.selected_bone       = -1;
        S.bone_rotate_mode    = false;
    }

    if (g_mp.has_model && !custom_level_clean_viewport) {
        static float s_wire_alpha = 0.30f;
        const float kIdleAlpha   = 0.30f;
        const float kHoverAlpha  = 1.00f;

        const ImVec2 wire_pos (origin.x + 6, next_overlay_y);
        const ImVec2 wire_size(190, 0);
        ImGui::SetNextWindowPos(wire_pos);
        ImGui::SetNextWindowSize(wire_size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(s_wire_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_wire_alpha);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##wireframe_overlay", nullptr, fl)) {
            bool hovering = ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                ImGuiHoveredFlags_ChildWindows);
            float target = hovering ? kHoverAlpha : kIdleAlpha;
            s_wire_alpha += (target - s_wire_alpha) * 0.18f;
            if (std::fabs(s_wire_alpha - target) < 0.005f) s_wire_alpha = target;

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Wireframe");
            ImGui::Checkbox("Show", &g_mp.wireframe);
            if (g_mp.no_tilt && (!g_level_spawn_markers.empty() ||
                                 !g_level_entity_text.empty())) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Entities");
                ImGui::Checkbox("Generators / spawn points",
                                &S.show_spawn_markers);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Creature generators (red) and their spawn "
                        "points (orange). In edit mode: click to "
                        "select, Right-click ground to add a "
                        "new generator.");
                ImGui::Checkbox("NPC / creature markers",
                                &S.show_ent_npcs);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Show the labelled diamond at each authored NPC or "
                        "creature position. Markers are not selectable.");
                }
                if (ImGui::Checkbox("Entity models",
                                    &S.show_entity_models) &&
                    !S.show_entity_models &&
                    ::g_selected_level_mesh_idx >= 0 &&
                    ::g_selected_level_mesh_idx <
                        static_cast<int>(g_mp.meshes.size()) &&
                    g_mp.meshes[static_cast<size_t>(
                        ::g_selected_level_mesh_idx)].is_entity_model) {
                    ::g_selected_level_mesh_idx = -1;
                    ::g_selected_level_pick_id = 0;
                    ::g_selected_level_hash = 0;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Render the resolved full NPC and creature models. "
                        "Select and move entities by clicking their model.");
                }
                ImGui::Checkbox("Containers", &S.show_containers);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "All inventory-bearing objects (purple), including "
                        "chests, cupboards, registers, and barrels. "
                        "Dig spots remain under their own "
                        "checkbox.");
                }
                ImGui::Checkbox("Dig spots", &S.show_dig_spots);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Dig spots (blue). Click one to inspect its loot, "
                        "search radius, priority, and respawn chance when "
                        "those values are present.");
                }
                ImGui::Checkbox("Text objects", &S.show_ent_text);
            }
            if (S.dev_mode) {
                ImGui::Checkbox("Terrain: engine blend",
                                &S.terrain_landscape_blend);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Dev-only: engine-reconciled LANDSCAPEMATERIAL terrain "
                        "blend (per-material tiling, 16/dim). A/B vs the current "
                        "shared-scale shader.");
            }
            if (g_mp.has_sky_theme) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Time");

                const bool has_cycle =
                    g_mp.has_day_night_cycle &&
                    g_mp.day_night_keyframes.size() >= 2;
                bool auto_time =
                    has_cycle && !g_mp.time_of_day_override;
                ImGui::BeginDisabled(!has_cycle);
                if (ImGui::Checkbox("Auto", &auto_time)) {
                    if (auto_time) {
                        g_mp.time_of_day_override = false;
                    } else {
                        g_mp.time_of_day_override = true;
                        g_mp.time_of_day_override_value =
                            g_mp.current_time_of_day;
                    }
                }
                ImGui::EndDisabled();

                float hour =
                    (g_mp.time_of_day_override
                         ? g_mp.time_of_day_override_value
                         : g_mp.current_time_of_day) * 24.0f;
                hour = std::clamp(hour, 0.0f, 24.0f);
                ImGui::SetNextItemWidth(150.0f);
                if (ImGui::SliderFloat("##time_of_day", &hour,
                                       0.0f, 24.0f, "%.2f h",
                                       ImGuiSliderFlags_AlwaysClamp)) {
                    g_mp.time_of_day_override = true;
                    g_mp.time_of_day_override_value =
                        std::clamp(hour / 24.0f, 0.0f, 1.0f);
                }
            }
            if (S.terrain_mode || g_mp.has_sky_theme ||
                g_mp.has_weather_theme ||
                g_mp.has_fog_theme) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Environment");
                if (S.terrain_mode) {
                    ImGui::Checkbox("Adjacent terrain",
                                    &S.show_adjacent_terrain);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Show the neighbouring levels' heightfields "
                            "around this level (textured with their baked "
                            "ground).");
                    }
                }
                if (g_mp.has_sky_theme) {
                    ImGui::Checkbox("Sky", &g_mp.show_sky);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            "Procedural sky, sun/moon and cloud layers "
                            "from the level's environment theme.");
                }
                if (g_mp.has_weather_theme) {
                    const bool theme_has_rain =
                        g_mp.weather_precip[0] > 0.0001f &&
                        g_mp.weather_precip[1] > 0.0001f;
                    const bool theme_has_snow =
                        g_mp.weather_precip[2] > 0.0001f &&
                        g_mp.weather_precip[3] > 0.0001f;
                    ImGui::Checkbox("Weather", &g_mp.show_weather);
                    if (ImGui::IsItemHovered()) {
                        if (theme_has_rain || theme_has_snow) {
                            char buf[160];
                            std::snprintf(
                                buf, sizeof(buf),
                                "Theme precipitation:%s%s\n"
                                "rain density %.2f size %.2f\n"
                                "snow fallspeed %.2f size %.2f",
                                theme_has_rain ? " rain" : "",
                                theme_has_snow ? " snow" : "",
                                g_mp.weather_precip[0],
                                g_mp.weather_precip[1],
                                g_mp.weather_precip[2],
                                g_mp.weather_precip[3]);
                            ImGui::SetTooltip("%s", buf);
                        } else {
                            ImGui::SetTooltip(
                                "This level's theme has no rain or "
                                "snow at the current time of day.");
                        }
                    }
                }
                if (g_mp.has_weather_theme || g_mp.has_fog_theme) {
                    ImGui::Checkbox("Mist / fog", &g_mp.show_mist);
                    if (ImGui::IsItemHovered()) {
                        if (g_mp.weather_mist_strength > 0.0001f ||
                            g_mp.has_fog_theme) {
                            char buf[120];
                            std::snprintf(
                                buf, sizeof(buf),
                                "Theme fogging + ground mist "
                                "(GroundMist strength %.2f).",
                                g_mp.weather_mist_strength);
                            ImGui::SetTooltip("%s", buf);
                        } else {
                            ImGui::SetTooltip(
                                "This level's theme has no fogging or "
                                "ground mist parameters.");
                        }
                    }
                }
            }

            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            next_overlay_y = wp.y + ws.y + 6.0f;
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
