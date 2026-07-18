void draw_details_overlays_gl(const ImVec2& origin, const ImVec2& region);

void draw_model_in_panel_gl() {
    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    int w = std::max(1, (int)region.x);
    int h = std::max(1, (int)region.y);

    if (!g_mp_initialized) {
        g_mp_initialized = MP_Init(g_mp, w, h);
    }
    if (!g_mp_initialized) {
        ImGui::Dummy(region);
        return;
    }

    MP_Resize(g_mp, w, h);
    ImGui::InvisibleButton("##model_render", region);
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();

    if (S.terrain_mode) {
        float dt = ImGui::GetIO().DeltaTime;
        if (hovered || g_flycam.is_looking ||
            g_flycam.right_press_pending) {
            ::render_panel_handle_flycam(dt);
        }
    } else {
        if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            const float kOrbitSensitivity = 0.008f;
            S.cam_yaw += d.x * kOrbitSensitivity;
            S.cam_pitch += d.y * kOrbitSensitivity;
            const float kPitchLimit = 1.5f;
            if (S.cam_pitch > kPitchLimit) S.cam_pitch = kPitchLimit;
            if (S.cam_pitch < -kPitchLimit) S.cam_pitch = -kPitchLimit;
        }
        if (hovered) {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {
                S.cam_dist *= (wheel > 0.0f) ? 0.9f : 1.111f;
                if (S.cam_dist < 0.3f) S.cam_dist = 0.3f;
                if (S.cam_dist > 50.0f) S.cam_dist = 50.0f;
            }
        }
        apply_orbit_to_flycam_gl();
    }

    for (size_t i = 0; i < g_mp.meshes.size(); ++i) {
        g_mp.meshes[i].highlight = ((int)i == ::g_highlight_mesh_idx);
        g_mp.meshes[i].isolated = ((int)i == ::g_isolate_mesh_idx);
    }

    MP_Render(g_mp, g_flycam);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    unsigned int tex = MP_GetTexture(g_mp);
    if (tex) {
        dl->AddImage((ImTextureID)(intptr_t)tex,
                     origin,
                     ImVec2(origin.x + region.x, origin.y + region.y),
                     ImVec2(0.0f, 1.0f),
                     ImVec2(1.0f, 0.0f));
    }

    if (hovered && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
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

    dl->AddRectFilled(ImVec2(origin.x + 6, origin.y + 6),
                      ImVec2(origin.x + 196, origin.y + 70),
                      IM_COL32(20, 22, 28, 200), 4.0f);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 12));
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Controls");
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 30));
    ImGui::TextDisabled(S.terrain_mode ? "R-Drag  look" : "L-Drag  rotate");
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 14, origin.y + 46));
    ImGui::TextDisabled(S.terrain_mode ? "WASD/QE  move" : "Wheel  zoom  /  ESC  close");

    if (S.terrain_mode) draw_gdb_placements_overlay_gl(origin, region);
    draw_materials_overlay_gl(origin, region, origin.y + 76.0f);
    draw_details_overlays_gl(origin, region);
}

void draw_details_overlays_gl(const ImVec2& origin, const ImVec2& region) {
    if (S.show_item_details && S.selected_item >= 0 &&
        S.selected_item < (int)g_item_details.size() &&
        !LevelEdit::Enabled()) {
        const auto& it = g_item_details[(size_t)S.selected_item];
        static unsigned int icon_tex = 0;
        static uint32_t icon_for = 0xFFFFFFFFu;
        static int icon_w = 0, icon_h = 0;
        if (g_item_icon_dirty.exchange(false) || icon_for != it.record_hash) {
            icon_for = it.record_hash;
            if (icon_tex) glDeleteTextures(1, &icon_tex);
            icon_tex = 0; icon_w = icon_h = 0;
            std::vector<unsigned char> tex_buf;
            std::vector<uint8_t> rgba;
            bool has_alpha = false;
            if (!it.icon_tex.empty() &&
                build_any_tex_buffer_for_name(it.icon_tex, tex_buf, {}) &&
                decode_tex_to_rgba(tex_buf, rgba, icon_w, icon_h,
                                   &has_alpha, -1) && icon_w > 0 && icon_h > 0) {
                icon_tex = create_gl_texture_from_rgba(
                    icon_w, icon_h, rgba.data());
            }
        }
        static float alpha = 0.30f;
        constexpr float width = 300.0f, pad = 6.0f;
        ImGui::SetNextWindowPos(
            ImVec2(origin.x + region.x - width - pad, origin.y + pad));
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(width, 0), ImVec2(width, std::max(200.0f, region.y - 12.0f)));
        ImGui::SetNextWindowBgAlpha(alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##item_details_overlay", nullptr, flags)) {
            const ImVec2 wp = ImGui::GetWindowPos();
            const ImVec2 ws = ImGui::GetWindowSize();
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            bool hovered = mouse.x >= wp.x && mouse.x < wp.x + ws.x &&
                           mouse.y >= wp.y && mouse.y < wp.y + ws.y;
            static bool was_hovered = false;
            if (!hovered && was_hovered && ImGui::GetIO().MouseDown[0])
                hovered = true;
            was_hovered = hovered;
            const float target = hovered ? 1.0f : 0.30f;
            alpha += (target - alpha) * 0.18f;
            if (std::fabs(alpha - target) < 0.005f) alpha = target;
            ImGui::TextColored(ImVec4(1, .9f, .5f, 1), "Item Details");
            ImGui::Separator();
            std::string name;
            if (it.name_tag) TextBank::Lookup(it.name_tag, name);
            if (name.empty()) name = it.label;
            ImGui::TextColored(ImVec4(.65f, .85f, 1, 1), "%s", name.c_str());
            if (icon_tex) {
                float w = (float)icon_w, h = (float)icon_h;
                if (std::max(w, h) > 80.0f) {
                    float s = 80.0f / std::max(w, h); w *= s; h *= s;
                }
                ImGui::Image((ImTextureID)(intptr_t)icon_tex, ImVec2(w, h));
            }
            if (it.money >= 0) ImGui::Text("Value: %d gold", it.money);
            std::string desc;
            if (it.desc_tag) TextBank::Lookup(it.desc_tag, desc);
            if (!desc.empty()) {
                ImGui::Spacing(); ImGui::TextColored(
                    ImVec4(.65f, .85f, 1, 1), "Description");
                ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(desc.c_str());
                ImGui::PopTextWrapPos();
            }
            if (!it.stats.empty()) {
                ImGui::Spacing(); ImGui::TextColored(
                    ImVec4(.65f, .85f, 1, 1), "Stats");
                if (ImGui::BeginTable("##item_stats", 2,
                        ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Field");
                    ImGui::TableSetupColumn(
                        "Value", ImGuiTableColumnFlags_WidthFixed, 84.0f);
                    for (const auto& value : it.stats) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(value.first.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(value.second.c_str());
                    }
                    ImGui::EndTable();
                }
            }
        }
        ImGui::End(); ImGui::PopStyleVar();
    }

    if (S.show_entity_details && S.selected_entity >= 0 &&
        S.selected_entity < (int)g_global_entity_catalog.size() &&
        !LevelEdit::Enabled() &&
        ContentTabs::ActiveKind() == ContentTabs::Kind::Entity) {
        const auto& entity = g_global_entity_catalog[(size_t)S.selected_entity];
        static float alpha = 0.30f;
        constexpr float width = 350.0f, pad = 6.0f;
        const float height = std::min(620.0f, std::max(180.0f, region.y - 12.0f));
        ImGui::SetNextWindowPos(
            ImVec2(origin.x + region.x - width - pad, origin.y + pad));
        ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar;
        if (ImGui::Begin("##entity_details_overlay", nullptr, flags)) {
            const ImVec2 wp = ImGui::GetWindowPos();
            const ImVec2 ws = ImGui::GetWindowSize();
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            bool hovered = mouse.x >= wp.x && mouse.x < wp.x + ws.x &&
                           mouse.y >= wp.y && mouse.y < wp.y + ws.y;
            static bool was_hovered = false;
            if (!hovered && was_hovered && ImGui::GetIO().MouseDown[0])
                hovered = true;
            was_hovered = hovered;
            const float target = hovered ? 1.0f : 0.30f;
            alpha += (target - alpha) * 0.18f;
            if (std::fabs(alpha - target) < 0.005f) alpha = target;
            ImGui::TextColored(ImVec4(1, .9f, .5f, 1), "Entity Details");
            ImGui::Separator();
            ImGui::BeginChild("##entity_details_scroll");
            const std::string& name = entity.display_name.empty()
                ? entity.name : entity.display_name;
            ImGui::TextColored(ImVec4(.65f, .85f, 1, 1), "%s", name.c_str());
            static uint32_t cached_entity = 0;
            static uint64_t cached_bindings = 0;
            static uint64_t cached_catalog = 0;
            static size_t cached_clips = 0;
            static std::string anim_filter;
            static std::vector<std::pair<size_t, std::string>> animations;
            const uint64_t bindings = Anim::model_animation_binding_revision();
            if (cached_entity != entity.entity_hash ||
                cached_bindings != bindings ||
                cached_catalog != g_global_entity_catalog_revision ||
                cached_clips != S.anim_clips.size()) {
                animations.clear();
                anim_filter.clear();
                std::unordered_set<size_t> seen;
                const std::unordered_set<uint32_t> models(
                    entity.model_hashes.begin(), entity.model_hashes.end());
                for (const auto& binding : Anim::model_animation_bindings()) {
                    if (!models.count(binding.model_path_hash) ||
                        binding.clip_index >= S.anim_clips.size() ||
                        !seen.insert(binding.clip_index).second)
                        continue;
                    std::string anim_name = binding.animation_name.empty()
                        ? binding.source_name : binding.animation_name;
                    if (anim_name.empty())
                        anim_name = S.anim_clips[binding.clip_index].name;
                    animations.emplace_back(
                        binding.clip_index, std::move(anim_name));
                }
                cached_entity = entity.entity_hash;
                cached_bindings = bindings;
                cached_catalog = g_global_entity_catalog_revision;
                cached_clips = S.anim_clips.size();
            }

            ImGui::Spacing(); ImGui::Separator();
            ImGui::TextColored(ImVec4(.55f, .9f, 1, 1),
                               "Animations (%zu)", animations.size());
            if (animations.empty()) {
                ImGui::TextDisabled("None indexed");
            } else {
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##entity_animation_filter",
                    "Filter animations...", &anim_filter);
                std::string needle = anim_filter;
                std::transform(needle.begin(), needle.end(),
                               needle.begin(), ::tolower);
                std::vector<size_t> visible;
                for (size_t i = 0; i < animations.size(); ++i) {
                    std::string candidate = animations[i].second;
                    std::transform(candidate.begin(), candidate.end(),
                                   candidate.begin(), ::tolower);
                    if (needle.empty() ||
                        candidate.find(needle) != std::string::npos)
                        visible.push_back(i);
                }
                auto& player = Anim::global_player();
                const Anim::AnimClip* current = player.clip();
                if (current) {
                    const bool playing =
                        player.state() == Anim::AnimPlayer::State::Playing;
                    const bool paused =
                        player.state() == Anim::AnimPlayer::State::Paused;
                    if (UI::icon_button("##entity_anim_stop", ICON_FA_STOP,
                                        26.0f, false))
                        player.stop();
                    ImGui::SameLine();
                    if (UI::icon_button("##entity_anim_play",
                            playing ? ICON_FA_PAUSE : ICON_FA_PLAY,
                            30.0f, true)) {
                        if (playing) player.pause();
                        else if (paused) player.resume();
                        else player.play(current, player.is_loop());
                    }
                    ImGui::SameLine();
                    if (UI::icon_button("##entity_anim_loop", ICON_FA_REPEAT,
                            26.0f, false, player.is_loop()))
                        player.set_loop(!player.is_loop());
                    float time = player.time();
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::SliderFloat("##entity_anim_time", &time, 0.0f,
                            std::max(Anim::clip_duration_seconds(*current),
                                     0.001f), "%.2fs"))
                        player.seek(time);
                } else {
                    ImGui::TextDisabled("Select an animation to play it");
                }
                const float list_h = std::min(
                    240.0f, std::max(100.0f,
                                     ImGui::GetContentRegionAvail().y));
                ImGui::BeginChild("##entity_animation_list",
                                  ImVec2(0.0f, list_h), false);
                ImGuiListClipper clipper;
                clipper.Begin((int)visible.size());
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart;
                         row < clipper.DisplayEnd; ++row) {
                        const auto& animation =
                            animations[visible[(size_t)row]];
                        char label[192];
                        std::snprintf(label, sizeof(label), "%s  (%.2fs)",
                            animation.second.c_str(),
                            Anim::clip_duration_seconds(
                                S.anim_clips[animation.first]));
                        ImGui::PushID(row);
                        if (ImGui::Selectable(label,
                                S.anim_selected_clip ==
                                    (int)animation.first)) {
                            S.anim_selected_clip = (int)animation.first;
                            player.play(&S.anim_clips[animation.first],
                                        player.is_loop());
                        }
                        ImGui::PopID();
                    }
                }
                clipper.End();
                ImGui::EndChild();
            }
            ImGui::Spacing(); ImGui::Separator();
            ImGui::TextColored(ImVec4(.55f, .9f, 1, 1), "Model parts");
            if (entity.model_hashes.empty()) ImGui::TextDisabled("None");
            for (uint32_t hash : entity.model_hashes) {
                const FlatAssetEntry* match = FindGlobalModelAssetByPathHash(hash);
                if (match) ImGui::BulletText("%s", match->full_path.c_str());
                else ImGui::BulletText("Unresolved model 0x%08X", hash);
            }
            const auto gameplay = g_global_entity_gameplay.find(entity.entity_hash);
            if (gameplay != g_global_entity_gameplay.end())
                draw_entity_gameplay_details(gameplay->second, false);
            ImGui::EndChild();
        }
        ImGui::End(); ImGui::PopStyleVar();
    }
}
