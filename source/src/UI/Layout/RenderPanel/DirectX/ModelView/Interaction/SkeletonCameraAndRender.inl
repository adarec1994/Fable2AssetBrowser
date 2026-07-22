    bool skel_visible = ::g_skel_overlay_show && (g_mp.bone_count > 0);

    static float s_rot_snapshot[4]    = {0, 0, 0, 1};
    static int   s_rot_snapshot_bone  = -1;
    static bool  s_rot_snapshot_valid = false;

    auto cancel_rotate = [&]() {
        if (s_rot_snapshot_valid &&
            s_rot_snapshot_bone >= 0 &&
            s_rot_snapshot_bone < (int)g_mp.bone_count &&
            (size_t)s_rot_snapshot_bone * 4 + 4 <= S.bone_rot_deltas.size()) {
            for (int k = 0; k < 4; ++k) {
                S.bone_rot_deltas[(size_t)s_rot_snapshot_bone * 4 + (size_t)k]
                    = s_rot_snapshot[k];
            }
        }
        s_rot_snapshot_valid = false;
        S.bone_rotate_mode   = false;
    };
    auto confirm_rotate = [&]() {
        s_rot_snapshot_valid = false;
        S.bone_rotate_mode   = false;
    };

    bool rotate_active = (skel_visible && S.bone_rotate_mode &&
                          S.selected_bone >= 0 &&
                          S.selected_bone < (int)g_mp.bone_count);

    if (rotate_active) {

        if (hovered) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            if (d.x != 0.0f || d.y != 0.0f) {
                const float kRotSensitivity = 0.01f;
                float a_y = d.x * kRotSensitivity;
                float a_x = d.y * kRotSensitivity;

                using namespace DirectX;
                XMVECTOR qx = XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), a_x);
                XMVECTOR qy = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), a_y);
                XMVECTOR delta = XMQuaternionMultiply(qx, qy);

                int b = S.selected_bone;
                XMVECTOR cur = XMVectorSet(
                    S.bone_rot_deltas[(size_t)b * 4 + 0],
                    S.bone_rot_deltas[(size_t)b * 4 + 1],
                    S.bone_rot_deltas[(size_t)b * 4 + 2],
                    S.bone_rot_deltas[(size_t)b * 4 + 3]);

                XMVECTOR nxt = XMQuaternionNormalize(XMQuaternionMultiply(cur, delta));
                XMFLOAT4 nf;
                XMStoreFloat4(&nf, nxt);
                S.bone_rot_deltas[(size_t)b * 4 + 0] = nf.x;
                S.bone_rot_deltas[(size_t)b * 4 + 1] = nf.y;
                S.bone_rot_deltas[(size_t)b * 4 + 2] = nf.z;
                S.bone_rot_deltas[(size_t)b * 4 + 3] = nf.w;
            }
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            cancel_rotate();
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            confirm_rotate();
        }
    }

    if (skel_visible && !rotate_active && hovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImVec2 mp = ImGui::GetIO().MousePos;
        int picked = pick_bone_at(mp, origin, region, 12.0f);
        S.selected_bone = picked;
    }

    
    
    const bool sculpt_click_owns_mouse =
        details_panel_docked() &&
        ((LandscapePanel::InSculptMode() && TerrainEdit::IsLoaded()) ||
         (LandscapePanel::InPaintMode() && TerrainPaint::Active()));
    if (g_mp.no_tilt && hovered && !rotate_active &&
        !sculpt_click_owns_mouse &&
        !LevelGizmo::WantsMouse() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f))
    {
        uint32_t picked_id = 0;
        uint64_t picked_hash = 0;
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const int picked = pick_level_mesh_at(mouse, origin, region,
                                              &picked_id, &picked_hash);
        ::g_selected_level_mesh_idx = picked;
        ::g_selected_level_pick_id = picked >= 0 ? picked_id : 0;
        ::g_selected_level_hash = picked >= 0 ? picked_hash : 0;
        if (picked >= 0) DetailsPanel::ClearSelection();
        if (picked >= 0 &&
            g_mp.meshes[static_cast<size_t>(picked)].is_entity_model) {


            g_sel_spawn_marker = -1;
            g_sel_pending_sp = -1;
            g_sel_pending_gen = -1;
        }
        if (picked < 0) LevelGizmo::CancelDrag();
    }

    if (S.terrain_mode) {
        const float dt = ImGui::GetIO().DeltaTime;
        if (hovered || g_flycam.is_looking ||
            g_flycam.right_press_pending) {
            ::render_panel_handle_flycam(dt);
        }
    } else {
        if (!rotate_active && active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            const float kOrbitSensitivity = 0.008f;
            S.cam_yaw   += d.x * kOrbitSensitivity;
            S.cam_pitch += d.y * kOrbitSensitivity;

            const float kPitchLimit = 1.5f;
            if (S.cam_pitch >  kPitchLimit) S.cam_pitch =  kPitchLimit;
            if (S.cam_pitch < -kPitchLimit) S.cam_pitch = -kPitchLimit;
        }

        if (hovered) {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {
                S.cam_dist *= (wheel > 0.0f) ? 0.9f : 1.111f;
                if (S.cam_dist < 0.3f)  S.cam_dist = 0.3f;
                if (S.cam_dist > 50.0f) S.cam_dist = 50.0f;
            }
        }
    }

    if (skel_visible && hovered && ImGui::IsKeyPressed(S.key_rotate_mode)) {
        if (S.selected_bone >= 0 && S.selected_bone < (int)g_mp.bone_count &&
            (size_t)S.selected_bone * 4 + 4 <= S.bone_rot_deltas.size()) {
            if (!S.bone_rotate_mode) {
                int b = S.selected_bone;
                for (int k = 0; k < 4; ++k) {
                    s_rot_snapshot[k] =
                        S.bone_rot_deltas[(size_t)b * 4 + (size_t)k];
                }
                s_rot_snapshot_bone  = b;
                s_rot_snapshot_valid = true;
                S.bone_rotate_mode   = true;
            } else {
                confirm_rotate();
            }
        }
    }

    for (size_t i = 0; i < g_mp.meshes.size(); ++i) {
        g_mp.meshes[i].highlight =
            ((int)i == ::g_highlight_mesh_idx) ||
            (::g_selected_level_pick_id == 0 &&
             (int)i == ::g_selected_level_mesh_idx) ||
            (DetailsPanel::WaterSelected() && g_mp.meshes[i].is_water);
        g_mp.meshes[i].isolated  = ((int)i == ::g_isolate_mesh_idx);
    }
    g_mp.selected_pick_id = ::g_selected_level_pick_id;
    g_mp.selected_pick_hash = ::g_selected_level_hash;

    if (!S.terrain_mode) apply_orbit_to_flycam();
    if (TerrainPaint::Active()) {
        TerrainPaint::SyncRenderResources(device);
    }
    MP_Render(device, g_mp, g_flycam);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (g_mp.srv) {
        dl->AddImage((ImTextureID)g_mp.srv,
                     origin,
                     ImVec2(origin.x + region.x, origin.y + region.y));
    }
