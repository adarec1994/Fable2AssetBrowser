    if (S.show_entity_details && S.selected_entity >= 0 &&
        S.selected_entity < static_cast<int>(g_global_entity_catalog.size()) &&
        !LevelEdit::Enabled() &&
        ContentTabs::ActiveKind() == ContentTabs::Kind::Entity) {
        const auto& entity = g_global_entity_catalog[
            static_cast<std::size_t>(S.selected_entity)];
        static float s_entity_alpha = 0.30f;
        constexpr float kIdleAlpha = 0.30f;
        constexpr float kHoverAlpha = 1.00f;
        constexpr float kEntityW = 350.0f;
        constexpr float kEntityPad = 6.0f;
        const float entity_h = (std::min)(
            620.0f, (std::max)(180.0f, region.y - 2.0f * kEntityPad));
        ImGui::SetNextWindowPos(
            ImVec2(origin.x + region.x - kEntityW - kEntityPad,
                   origin.y + kEntityPad));
        ImGui::SetNextWindowSize(ImVec2(kEntityW, entity_h),
                                 ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(s_entity_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_entity_alpha);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar;
        if (ImGui::Begin("##entity_details_overlay", nullptr, flags)) {
            const ImVec2 window_pos = ImGui::GetWindowPos();
            const ImVec2 window_size = ImGui::GetWindowSize();
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            bool hovering = mouse.x >= window_pos.x &&
                            mouse.x < window_pos.x + window_size.x &&
                            mouse.y >= window_pos.y &&
                            mouse.y < window_pos.y + window_size.y;
            static bool s_was_hovering = false;
            if (!hovering && s_was_hovering &&
                ImGui::GetIO().MouseDown[0]) {
                hovering = true;
            }
            s_was_hovering = hovering;
            const float target = hovering ? kHoverAlpha : kIdleAlpha;
            s_entity_alpha += (target - s_entity_alpha) * 0.18f;
            if (std::fabs(s_entity_alpha - target) < 0.005f) {
                s_entity_alpha = target;
            }

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                               "Entity Details");
            ImGui::Separator();
            ImGui::BeginChild("##entity_details_scroll", ImVec2(0.0f, 0.0f),
                              false);
            ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "%s",
                               (entity.display_name.empty()
                                    ? entity.name : entity.display_name)
                                   .c_str());
            static std::uint32_t cached_animation_entity = 0;
            static std::uint64_t cached_animation_binding_revision = 0;
            static std::uint64_t cached_animation_catalog_revision = 0;
            static std::size_t cached_animation_clip_count = 0;
            static std::string entity_animation_filter;
            static std::vector<std::pair<std::size_t, std::string>> animations;
            const std::uint64_t binding_revision =
                Anim::model_animation_binding_revision();
            if (cached_animation_entity != entity.entity_hash ||
                cached_animation_binding_revision != binding_revision ||
                cached_animation_catalog_revision !=
                    g_global_entity_catalog_revision ||
                cached_animation_clip_count != S.anim_clips.size()) {
                animations.clear();
                entity_animation_filter.clear();
                std::unordered_set<std::size_t> seen_animations;
                const std::unordered_set<std::uint32_t> model_hashes(
                    entity.model_hashes.begin(), entity.model_hashes.end());
                for (const Anim::ModelAnimationBinding& binding :
                     Anim::model_animation_bindings()) {
                    if (model_hashes.find(binding.model_path_hash) ==
                        model_hashes.end()) {
                        continue;
                    }
                    if (binding.clip_index >= S.anim_clips.size() ||
                        !seen_animations.insert(binding.clip_index).second) {
                        continue;
                    }
                    std::string name = binding.animation_name.empty()
                        ? binding.source_name : binding.animation_name;
                    if (name.empty()) {
                        name = S.anim_clips[binding.clip_index].name;
                    }
                    animations.emplace_back(binding.clip_index,
                                             std::move(name));
                }
                cached_animation_entity = entity.entity_hash;
                cached_animation_binding_revision = binding_revision;
                cached_animation_catalog_revision =
                    g_global_entity_catalog_revision;
                cached_animation_clip_count = S.anim_clips.size();
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f),
                               "Animations (%zu)", animations.size());
            if (animations.empty()) {
                ImGui::TextDisabled("None indexed");
            } else {
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##entity_animation_filter",
                                         "Filter animations...",
                                         &entity_animation_filter);
                std::string filter_lower = entity_animation_filter;
                std::transform(filter_lower.begin(), filter_lower.end(),
                               filter_lower.begin(), ::tolower);
                std::vector<std::size_t> visible_animations;
                visible_animations.reserve(animations.size());
                for (std::size_t i = 0; i < animations.size(); ++i) {
                    if (filter_lower.empty()) {
                        visible_animations.push_back(i);
                        continue;
                    }
                    std::string name_lower = animations[i].second;
                    std::transform(name_lower.begin(), name_lower.end(),
                                   name_lower.begin(), ::tolower);
                    if (name_lower.find(filter_lower) != std::string::npos) {
                        visible_animations.push_back(i);
                    }
                }
                auto& player = Anim::global_player();
                const Anim::AnimClip* current = player.clip();
                if (current) {
                    const bool playing = player.state() ==
                        Anim::AnimPlayer::State::Playing;
                    const bool paused = player.state() ==
                        Anim::AnimPlayer::State::Paused;
                    if (UI::icon_button("##entity_anim_stop", ICON_FA_STOP,
                                        26.0f, false)) {
                        player.stop();
                    }
                    ImGui::SameLine();
                    const char* glyph = playing ? ICON_FA_PAUSE : ICON_FA_PLAY;
                    if (UI::icon_button("##entity_anim_play", glyph,
                                        30.0f, true)) {
                        if (playing) player.pause();
                        else if (paused) player.resume();
                        else player.play(current, player.is_loop());
                    }
                    ImGui::SameLine();
                    if (UI::icon_button("##entity_anim_loop", ICON_FA_REPEAT,
                                        26.0f, false,
                                        player.is_loop())) {
                        player.set_loop(!player.is_loop());
                    }
                    const float duration =
                        Anim::clip_duration_seconds(*current);
                    float time = player.time();
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::SliderFloat("##entity_anim_time", &time,
                                           0.0f,
                                           (std::max)(duration, 0.001f),
                                           "%.2fs")) {
                        player.seek(time);
                    }
                } else {
                    ImGui::TextDisabled("Select an animation to play it");
                }
                const float animation_list_height = (std::min)(
                    240.0f,
                    (std::max)(100.0f, ImGui::GetContentRegionAvail().y));
                ImGui::BeginChild("##entity_animation_list",
                                  ImVec2(0.0f, animation_list_height),
                                  false);
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(visible_animations.size()));
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart;
                         row < clipper.DisplayEnd; ++row) {
                        const auto& animation =
                            animations[visible_animations[
                                static_cast<std::size_t>(row)]];
                        const bool selected =
                            S.anim_selected_clip ==
                            static_cast<int>(animation.first);
                        ImGui::PushID(row);
                        const float duration = Anim::clip_duration_seconds(
                            S.anim_clips[animation.first]);
                        char animation_label[192];
                        std::snprintf(animation_label,
                                      sizeof(animation_label),
                                      "%s  (%.2fs)",
                                      animation.second.c_str(), duration);
                        if (ImGui::Selectable(animation_label, selected)) {
                            S.anim_selected_clip =
                                static_cast<int>(animation.first);
                            player.play(&S.anim_clips[animation.first],
                                        player.is_loop());
                        }
                        ImGui::PopID();
                    }
                }
                clipper.End();
                ImGui::EndChild();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f),
                               "Model parts");
            if (entity.model_hashes.empty()) {
                ImGui::TextDisabled("None");
            } else {
                for (std::uint32_t hash : entity.model_hashes) {
                    const FlatAssetEntry* match =
                        FindGlobalModelAssetByPathHash(hash);
                    if (match) {
                        ImGui::Bullet();
                        ImGui::SameLine();
                        ImGui::TextWrapped("%s", match->full_path.c_str());
                    } else {
                        ImGui::BulletText("Unresolved model 0x%08X", hash);
                    }
                }
            }

            const auto gameplay =
                g_global_entity_gameplay.find(entity.entity_hash);
            if (gameplay != g_global_entity_gameplay.end()) {
                draw_entity_gameplay_details(gameplay->second, false);
            }
            ImGui::EndChild();
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
