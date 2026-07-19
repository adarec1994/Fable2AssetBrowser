        if (!details_panel_docked()) {
        const float kOverlayW = (sel_contents || sel_gameplay ||
                                 sel_property ||
                                 sel_chest_addition >= 0 || sel_text ||
                                 sel_readable_addition >= 0)
            ? 290.0f
            : (LevelEdit::Enabled() ? 190.0f : 150.0f);
        ImGui::SetNextWindowPos(ImVec2(origin.x + region.x - kOverlayW - 8.0f,
                                       origin.y + 6.0f));
        ImGui::SetNextWindowSize(ImVec2(kOverlayW, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.78f);
        ImGuiWindowFlags ofl = ImGuiWindowFlags_NoTitleBar
                             | ImGuiWindowFlags_NoResize
                             | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoCollapse
                             | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_AlwaysAutoResize
                             | ImGuiWindowFlags_NoFocusOnAppearing;
        if (ImGui::Begin("##sel_transform_overlay", nullptr, ofl)) {
            if (edit_active) {
                const char* mode_name =
                    LevelGizmo::GetMode() == LevelGizmo::Mode::Rotate
                        ? "Rotate (E)"
                        : "Move (W)";
                ImGui::TextDisabled("%s", mode_name);
            }
            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Position:");
            if (edit_active) {
                static const char* kAxis[3] = { "X##selpos", "Y##selpos",
                                                "Z##selpos" };
                float edit_pos[3] = { sel_pos[0], sel_pos[1], sel_pos[2] };
                bool commit = false;
                for (int a = 0; a < 3; ++a) {
                    ImGui::SetNextItemWidth(120.0f);
                    ImGui::InputFloat(kAxis[a], &edit_pos[a], 0.0f, 0.0f,
                                      "%.3f");
                    if (ImGui::IsItemDeactivatedAfterEdit()) commit = true;
                }
                if (commit) {
                    const float step[3] = { edit_pos[0] - sel_pos[0],
                                            edit_pos[1] - sel_pos[1],
                                            edit_pos[2] - sel_pos[2] };
                    if (step[0] != 0.0f || step[1] != 0.0f ||
                        step[2] != 0.0f) {
                        LevelEdit::PushUndoSnapshot(collect_group_ids());
                        apply_group_edit(kEditMove, step);
                    }
                }
            } else {
                ImGui::Text("X: %.3f", sel_pos[0]);
                ImGui::Text("Y: %.3f", sel_pos[1]);
                ImGui::Text("Z: %.3f", sel_pos[2]);
            }
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Rotation:");
            if (edit_active) {
                static const char* kRAxis[3] = { "X##selrot", "Y##selrot",
                                                 "Z##selrot" };
                float edit_rot[3] = { sel_rot[0], sel_rot[1], sel_rot[2] };
                bool commit = false;
                for (int a = 0; a < 3; ++a) {
                    ImGui::SetNextItemWidth(120.0f);
                    ImGui::InputFloat(kRAxis[a], &edit_rot[a], 0.0f, 0.0f,
                                      "%.1f");
                    if (ImGui::IsItemDeactivatedAfterEdit()) commit = true;
                }
                if (commit) {
                    const float step[3] = { edit_rot[0] - sel_rot[0],
                                            edit_rot[1] - sel_rot[1],
                                            edit_rot[2] - sel_rot[2] };
                    if (step[0] != 0.0f || step[1] != 0.0f ||
                        step[2] != 0.0f) {
                        LevelEdit::PushUndoSnapshot(collect_group_ids());
                        apply_group_edit(kEditRotate, step);
                    }
                }
            } else if (sel_has_rot) {
                ImGui::Text("X: %.1f", sel_rot[0]);
                ImGui::Text("Y: %.1f", sel_rot[1]);
                ImGui::Text("Z: %.1f", sel_rot[2]);
            } else {
                ImGui::TextDisabled("n/a");
            }
            if (sel_text) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Text:");
                static uint32_t s_text_sel = 0;
                static std::vector<std::string> s_text_bufs;
                const uint32_t text_key = sel_text->tag_hashes.front();
                if (s_text_sel != text_key ||
                    s_text_bufs.size() != sel_text->tag_hashes.size()) {
                    s_text_sel = text_key;
                    s_text_bufs.clear();
                    for (uint32_t th : sel_text->tag_hashes) {
                        std::string t;
                        if (!LevelEdit::GetEntityTextEdit(th, t)) {
                            TextBank::Lookup(th, t);
                        }
                        s_text_bufs.push_back(std::move(t));
                    }
                }
                const bool text_editable =
                    LevelEdit::Enabled() && !LevelEdit::Saving();
                for (size_t pi = 0; pi < s_text_bufs.size(); ++pi) {
                    ImGui::PushID(int(pi) + 0x2000);
                    if (s_text_bufs.size() > 1) {
                        if (pi == 0) {
                            ImGui::TextDisabled("Item Name");
                        } else if (pi == 1) {
                            ImGui::TextDisabled("Description");
                        } else {
                            ImGui::TextDisabled("Page %d", int(pi) + 1);
                        }
                    }
                    if (text_editable) {
                        ImGui::InputTextMultiline(
                            "##entity_text", &s_text_bufs[pi],
                            ImVec2(268.0f, 110.0f));
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            LevelEdit::SetEntityTextEdit(
                                sel_text->tag_hashes[pi],
                                s_text_bufs[pi]);
                        }
                    } else {
                        std::string shown = s_text_bufs[pi];
                        if (shown.size() > 1200) {
                            shown.resize(1200);
                            shown += " [...]";
                        }
                        ImGui::TextWrapped("%s", shown.c_str());
                    }
                    ImGui::PopID();
                }
                if (text_editable) {
                    ImGui::TextDisabled(
                        "Text is written to book.babel on Save");
                }
            }
            if (sel_readable_addition >= 0) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
                                   "New readable (unsaved)");
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Text:");
                static int s_addtext_sel = -1;
                static std::string s_addtext_buf;
                if (s_addtext_sel != sel_readable_addition) {
                    s_addtext_sel = sel_readable_addition;
                    s_addtext_buf.clear();
                    LevelEdit::GetAdditionReadableText(
                        sel_readable_addition, s_addtext_buf);
                }
                const bool add_text_editable =
                    LevelEdit::Enabled() && !LevelEdit::Saving();
                ImGui::BeginDisabled(!add_text_editable);
                ImGui::InputTextMultiline("##add_readable_text",
                                          &s_addtext_buf,
                                          ImVec2(268.0f, 110.0f));
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    LevelEdit::SetAdditionReadableText(
                        sel_readable_addition, s_addtext_buf);
                }
                ImGui::EndDisabled();
            }
            if (sel_gameplay) {
                draw_entity_gameplay_details(*sel_gameplay);
            }
            {
                uint32_t anim_tree_hash = 0;
                std::string anim_tree_title = sel_gameplay
                    ? sel_gameplay->entity_name : std::string();
                uint32_t candidates[3] = {0, 0, 0};
                if (::g_selected_level_hash != 0 &&
                    ::g_selected_level_hash <= 0xFFFFFFFFull) {
                    candidates[0] = uint32_t(::g_selected_level_hash);
                }
                candidates[1] = sel_gdb_entity_hash;
                {
                    const int selected_marker =
                        selected_level_spawn_marker_index();
                    if (selected_marker >= 0) {
                        const auto& marker = g_level_spawn_markers[
                            size_t(selected_marker)];
                        candidates[2] = marker.creature_entity_hash;
                        if (anim_tree_title.empty()) {
                            anim_tree_title = marker.creature_name;
                        }
                    }
                }
                for (uint32_t candidate : candidates) {
                    if (candidate != 0 &&
                        AnimTreeUI::Available(candidate)) {
                        anim_tree_hash = candidate;
                        break;
                    }
                }
                if (anim_tree_hash != 0) {
                    ImGui::Spacing();
                    if (ImGui::Button("Show Animation Tree")) {
                        AnimTreeUI::Open(anim_tree_hash,
                                         anim_tree_title.empty()
                                             ? "Entity" : anim_tree_title);
                    }
                }
            }
            if (sel_property) {
                draw_property_details(*sel_property);
            }
