        }
        ImGui::End();
        }   
        if (edit_active) {
            LevelGizmo::Result gz = LevelGizmo::DrawAndHandle(
                g_flycam, origin, region, sel_pos, true);
            static bool s_was_dragging = false;
            if (gz.dragging && !s_was_dragging) {
                LevelEdit::PushUndoSnapshot(collect_group_ids());
            }
            s_was_dragging = gz.dragging;
            if (gz.moved) {
                if (gz.step[0] != 0.0f || gz.step[1] != 0.0f ||
                    gz.step[2] != 0.0f) {
                    apply_group_edit(kEditMove, gz.step);
                }
                if (gz.rot_step_deg[0] != 0.0f ||
                    gz.rot_step_deg[1] != 0.0f ||
                    gz.rot_step_deg[2] != 0.0f) {
                    apply_group_edit(kEditRotate, gz.rot_step_deg);
                }
            }
        } else {
            LevelGizmo::CancelDrag();
        }
        if (edit_active && !ImGui::GetIO().WantTextInput &&
            ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            bool removed_spawn_point = false;
            const int marker_index = selected_level_spawn_marker_index();
            if (marker_index >= 0) {
                const LevelSpawnMarker& marker =
                    g_level_spawn_markers[size_t(marker_index)];
                if (marker.kind == 2) {
                    for (const LevelSpawnMarker& owner :
                         g_level_spawn_markers) {
                        if (owner.kind != 1 ||
                            owner.spawn_points_record == 0 ||
                            std::find(owner.spawn_point_entities.begin(),
                                      owner.spawn_point_entities.end(),
                                      marker.entity_hash) ==
                                owner.spawn_point_entities.end()) {
                            continue;
                        }
                        LevelEdit::RemoveSpawnPointFromExisting(
                            owner.entity_hash, owner.spawn_points_record,
                            marker.entity_hash);
                        OutputLog::info(
                            "level edit: spawn point deletion queued");
                        removed_spawn_point = true;
                        break;
                    }
                }
            }
            if (!removed_spawn_point) {
                LevelEdit::PushUndoSnapshot(collect_group_ids());
                apply_group_edit(kEditDelete, nullptr);
            }
            ::g_selected_level_mesh_idx = -1;
            ::g_selected_level_pick_id = 0;
            ::g_selected_level_hash = 0;
            LevelGizmo::CancelDrag();
        }
    }
