    if (g_mp.has_model && g_mp.bone_count > 0 && !S.anim_clips.empty() &&
        !S.item_model_active && !S.entity_model_active) {
        static float s_anim_alpha = 0.30f;
        const float kIdleAlpha   = 0.30f;
        const float kHoverAlpha  = 1.00f;

        const float kAnimW   = 280.0f;
        const float kAnimPad = 6.0f;

        const float anim_h = std::max(160.0f, region.y - 2 * kAnimPad);
        const ImVec2 anim_pos(origin.x + region.x - kAnimW - kAnimPad,
                              origin.y + kAnimPad);
        const ImVec2 anim_size(kAnimW, anim_h);

        ImGui::SetNextWindowPos(anim_pos);
        ImGui::SetNextWindowSize(anim_size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(s_anim_alpha * 0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_anim_alpha);

        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin("##anims_overlay", nullptr, fl)) {

            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            ImVec2 mp = ImGui::GetIO().MousePos;
            bool in_rect = mp.x >= wp.x && mp.x < wp.x + ws.x &&
                           mp.y >= wp.y && mp.y < wp.y + ws.y;
            static bool s_was_hovering = false;
            bool hovering = in_rect;

            if (!hovering && s_was_hovering &&
                ImGui::GetIO().MouseDown[0]) {
                hovering = true;
            }
            s_was_hovering = hovering;
            float target = hovering ? kHoverAlpha : kIdleAlpha;
            s_anim_alpha += (target - s_anim_alpha) * 0.18f;
            if (std::fabs(s_anim_alpha - target) < 0.005f) s_anim_alpha = target;

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Animations");
            ImGui::Separator();

            {
                auto& pl = Anim::global_player();
                const auto* cur = pl.clip();
                if (cur) {
                    const float dur_s = Anim::clip_duration_seconds(*cur);
                    const bool playing =
                        (pl.state() == Anim::AnimPlayer::State::Playing);
                    const bool paused  =
                        (pl.state() == Anim::AnimPlayer::State::Paused);

                    const float btn_lg = 36.0f;
                    const float btn_sm = 26.0f;
                    const float gap    = 10.0f;
                    const float row_w  = ImGui::GetContentRegionAvail().x;
                    const float group_w = btn_sm + gap + btn_lg + gap + btn_sm;
                    const float group_x = (row_w - group_w) * 0.5f;
                    const float row_y   = ImGui::GetCursorPosY();
                    const float sm_y    = row_y + (btn_lg - btn_sm) * 0.5f;

                    ImGui::SetCursorPos(ImVec2(group_x, sm_y));
                    if (UI::icon_button("##anim_stop", ICON_FA_STOP,
                                        btn_sm, false)) {
                        pl.stop();
                    }

                    ImGui::SetCursorPos(ImVec2(group_x + btn_sm + gap, row_y));
                    const char* play_glyph = playing ? ICON_FA_PAUSE : ICON_FA_PLAY;

                    float play_dx = playing ? 0.0f : 0.17f;
                    if (UI::icon_button("##anim_playpause", play_glyph,
                                        btn_lg, true, false, play_dx)) {
                        if (playing) pl.pause();
                        else if (paused) pl.resume();
                        else pl.play(cur, pl.is_loop());
                    }

                    ImGui::SetCursorPos(ImVec2(
                        group_x + btn_sm + gap + btn_lg + gap, sm_y));
                    bool loop = pl.is_loop();
                    if (UI::icon_button("##anim_loop", ICON_FA_REPEAT,
                                        btn_sm, false, loop)) {
                        pl.set_loop(!loop);
                    }

                    ImGui::Dummy(ImVec2(0, btn_lg + 4.0f));

                    ImGui::Text("%.2fs / %.2fs", pl.time(), dur_s);

                    {
                        const float scrub_h = 18.0f;
                        ImGui::InvisibleButton("##anim_scrub",
                                               ImVec2(-1, scrub_h));
                        ImVec2 r0 = ImGui::GetItemRectMin();
                        ImVec2 r1 = ImGui::GetItemRectMax();
                        bool active = ImGui::IsItemActive();
                        ImDrawList* dl = ImGui::GetWindowDrawList();

                        dl->AddRectFilled(r0, r1,
                                          IM_COL32(20, 22, 28, 255), 4.0f);

                        const float w = r1.x - r0.x;
                        const float cy = (r0.y + r1.y) * 0.5f;
                        const float prog = (dur_s > 0.0f)
                            ? (pl.time() / dur_s) : 0.0f;
                        const float playhead_x = r0.x + w * prog;

                        dl->AddRectFilled(r0,
                                          ImVec2(playhead_x, r1.y),
                                          IM_COL32(120, 200, 255, 200),
                                          4.0f);

                        bool hovered_event = false;
                        std::string ev_tip;
                        const ImVec2 mp = ImGui::GetIO().MousePos;
                        for (const auto& ev : cur->events) {
                            if (dur_s <= 0.0f) break;
                            float t = ev.time / dur_s;
                            if (t < 0.0f || t > 1.0f) continue;
                            float ex = r0.x + w * t;
                            dl->AddLine(ImVec2(ex, r0.y + 2),
                                        ImVec2(ex, r1.y - 2),
                                        IM_COL32(255, 200, 90, 230),
                                        1.5f);

                            if (ImGui::IsItemHovered() &&
                                std::fabs(mp.x - ex) <= 4.0f &&
                                !hovered_event) {
                                hovered_event = true;
                                ev_tip = ev.name;
                                if (!ev.param.empty())
                                    ev_tip += " - " + ev.param;
                                char tbuf[16];
                                std::snprintf(tbuf, sizeof(tbuf),
                                              "  @ %.2fs", ev.time);
                                ev_tip += tbuf;
                            }
                        }

                        dl->AddLine(ImVec2(playhead_x, r0.y + 1),
                                    ImVec2(playhead_x, r1.y - 1),
                                    IM_COL32(240, 245, 250, 255),
                                    2.0f);

                        if (active && dur_s > 0.0f) {
                            float t = (mp.x - r0.x) / w;
                            if (t < 0.0f) t = 0.0f;
                            if (t > 1.0f) t = 1.0f;
                            pl.seek(t * dur_s);
                        }

                        if (hovered_event) {
                            ImGui::SetTooltip("%s", ev_tip.c_str());
                        }
                    }

                    ImGui::Separator();
                }
            }

            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##anims_overlay_filter", "Filter",
                                     &S.anim_filter);

            const uint32_t want_bones = g_mp.bone_count;
            size_t authored_count = 0;
            const bool can_filter_by_authored =
                g_mp.has_model && S.current_mdl_path_hash != 0 &&
                !S.anim_clips.empty();
            if (can_filter_by_authored) {
                const uint64_t authored_sig =
                    Anim::model_animation_binding_revision() ^
                    (uint64_t(S.current_mdl_path_hash) << 32) ^
                    uint64_t(S.anim_clips.size());
                if (S.anim_authored_signature != authored_sig ||
                    S.anim_authored_cache.size() != S.anim_clips.size()) {
                    authored_count =
                        Anim::build_model_animation_cache_for_hash(
                            S.current_mdl_path_hash, S.anim_clips.size(),
                            S.anim_authored_cache);
                    S.anim_authored_signature = authored_sig;
                } else {
                    authored_count = 0;
                    for (uint8_t v : S.anim_authored_cache) {
                        if (v) ++authored_count;
                    }
                }
            }
            const bool has_authored_filter =
                can_filter_by_authored && authored_count > 0;
            if (has_authored_filter) {
                ImGui::Checkbox("Authored model",
                                &S.anim_authored_only);
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Show clips referenced by GDB animation records for "
                        "this exact model path hash.");
                }
            }
            const bool can_filter_by_skeleton =
                Anim::global_data_file().is_open() &&
                g_mp.has_model && want_bones > 0;
            if (can_filter_by_skeleton) {
                ImGui::Checkbox("Compatible rig",
                                &S.anim_compatible_only);
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Show clips whose AnimBank track map matches this "
                        "model's bone names. Falls back to the old %u-bone "
                        "track-count gate when no track map is available.",
                        want_bones);
                }
            }
            const bool filter_by_authored =
                S.anim_authored_only && has_authored_filter;
            const bool filter_by_bones =
                !filter_by_authored &&
                S.anim_compatible_only && can_filter_by_skeleton;
            if (filter_by_bones) {
                const uint64_t sig = Anim::rig_compatibility_signature(
                    S.mdl_info, want_bones, S.anim_clips,
                    Anim::global_data_file().is_open());
                if (S.anim_compat_signature != sig ||
                    S.anim_compat_cache.size() != S.anim_clips.size()) {
                    Anim::build_rig_compatibility_cache(
                        S.mdl_info, want_bones, S.anim_clips,
                        S.anim_compat_cache, S.anim_compat_matches,
                        S.anim_compat_named_tracks);
                    S.anim_compat_signature = sig;
                }
            }

            std::vector<int> vis;
            vis.reserve(S.anim_clips.size());
            std::string flow = S.anim_filter;
            std::transform(flow.begin(), flow.end(), flow.begin(), ::tolower);
            for (size_t i = 0; i < S.anim_clips.size(); ++i) {
                if (filter_by_authored) {
                    if (i >= S.anim_authored_cache.size() ||
                        !S.anim_authored_cache[i]) {
                        continue;
                    }
                } else if (filter_by_bones) {
                    if (i >= S.anim_compat_cache.size() ||
                        !S.anim_compat_cache[i]) {
                        continue;
                    }
                }
                if (flow.empty()) {
                    vis.push_back((int)i);
                } else {
                    std::string nlow = S.anim_clips[i].name;
                    std::transform(nlow.begin(), nlow.end(),
                                   nlow.begin(), ::tolower);
                    if (nlow.find(flow) != std::string::npos) {
                        vis.push_back((int)i);
                    }
                }
            }
            {
                ImGui::TextDisabled("%d / %zu%s",
                                    (int)vis.size(),
                                    S.anim_clips.size(),
                                    filter_by_authored
                                        ? " authored model"
                                        : (filter_by_bones ? " rig match" : ""));
                if (filter_by_bones) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%u bones)", want_bones);
                } else if (filter_by_authored) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%zu exact)", authored_count);
                }
            }

            ImGui::BeginChild("##anims_overlay_list", ImVec2(0, 0), false);
            ImGuiListClipper clipper;
            clipper.Begin((int)vis.size());
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart;
                     row < clipper.DisplayEnd; ++row) {
                    const int clip_idx = vis[(size_t)row];
                    const auto& c =
                        S.anim_clips[(size_t)clip_idx];
                    ImGui::PushID(row);
                    bool selected =
                        (S.anim_selected_clip == clip_idx);
                    char label[80];
                    float dur_s = Anim::clip_duration_seconds(c);
                    std::snprintf(label, sizeof(label), "%s  (%.2fs)",
                                  c.name.c_str(), dur_s);
                    if (ImGui::Selectable(label, selected,
                                          ImGuiSelectableFlags_SpanAllColumns)) {
                        S.anim_selected_clip = clip_idx;

                        Anim::global_player().play(
                            &S.anim_clips[(size_t)clip_idx],
                            Anim::global_player().is_loop());
                    }
                    if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted(c.name.c_str());
                        ImGui::Text("Duration: %.3f s  (%.0f fps)",
                                    dur_s, c.fps);
                        if (Anim::global_data_file().is_open()) {
                            auto h = Anim::global_data_file().parse_clip_header(c);
                            if (h.ok) {
                                ImGui::Text("Tracks: %u / model bones: %u%s",
                                            h.bone_count, want_bones,
                                            h.bone_count == want_bones
                                                ? "  track-count match"
                                                : "");
                            }
                        }
                        if (c.track_map) {
                            ImGui::Text("Track map: %zu / %zu model-name matches",
                                        (clip_idx >= 0 &&
                                         (size_t)clip_idx < S.anim_compat_matches.size())
                                            ? (size_t)S.anim_compat_matches[(size_t)clip_idx]
                                            : 0u,
                                        (clip_idx >= 0 &&
                                         (size_t)clip_idx < S.anim_compat_named_tracks.size())
                                            ? (size_t)S.anim_compat_named_tracks[(size_t)clip_idx]
                                            : 0u);
                        }
                        ImGui::Text("Events: %zu", c.events.size());
                        ImGui::EndTooltip();
                    }
                    ImGui::PopID();
                }
            }
            clipper.End();
            ImGui::EndChild();
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
