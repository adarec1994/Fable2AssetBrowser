        if (s_active_tab == 5) {

            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##anim_filter", "Filter",
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
            } else if (g_mp.has_model && S.current_mdl_path_hash != 0) {
                ImGui::TextDisabled("No authored animation set for model");
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
                    std::transform(nlow.begin(), nlow.end(), nlow.begin(), ::tolower);
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
            ImGui::BeginChild("anim_list", ImVec2(0, 0), false);
            if (S.anim_clips.empty()) {
                ImGui::TextDisabled("No animation TOC loaded.");
            } else {
                ImGuiListClipper clipper;
                clipper.Begin((int)vis.size());
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart;
                         row < clipper.DisplayEnd; ++row) {
                        const int clip_idx = vis[(size_t)row];
                        const auto& c = S.anim_clips[(size_t)clip_idx];
                        ImGui::PushID(row);
                        bool selected =
                            (S.anim_selected_clip == clip_idx);
                        char label[64];
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
            }
            ImGui::EndChild();
        }
