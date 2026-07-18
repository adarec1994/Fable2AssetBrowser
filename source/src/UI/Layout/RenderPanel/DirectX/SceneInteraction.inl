void draw_skeleton_overlay(const ImVec2& origin, const ImVec2& region) {
    if (g_mp.bone_count == 0) return;

    std::vector<float> world_pose;
    MP_ComputeWorldPose(g_mp, S.bone_rot_deltas, world_pose);

    std::vector<ImVec2>  screen;
    std::vector<uint8_t> visible;
    project_bones_to_screen(world_pose, g_mp.bone_count, origin, region,
                            screen, visible);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 line_col   = IM_COL32(255, 220,  80, 220);
    const ImU32 dot_col    = IM_COL32(255, 240, 120, 255);
    const ImU32 sel_line   = IM_COL32(120, 220, 255, 255);
    const ImU32 sel_dot    = IM_COL32( 90, 240, 255, 255);

    const uint32_t n = g_mp.bone_count;

    for (uint32_t i = 0; i < n; ++i) {
        if (!visible[i]) continue;
        int pid = (i < g_mp.bone_parents.size()) ? g_mp.bone_parents[i] : -1;
        if (pid < 0 || pid >= (int)n) continue;
        if (!visible[(uint32_t)pid]) continue;
        bool sel = (S.selected_bone == (int)i || S.selected_bone == pid);
        dl->AddLine(screen[(uint32_t)pid], screen[i],
                    sel ? sel_line : line_col,
                    sel ? 2.0f : 1.5f);
    }

    for (uint32_t i = 0; i < n; ++i) {
        if (!visible[i]) continue;
        if ((int)i == S.selected_bone) {
            dl->AddCircleFilled(screen[i], 5.0f, sel_dot);
            dl->AddCircle      (screen[i], 7.5f, IM_COL32(0, 0, 0, 220), 0, 2.0f);
        } else {
            dl->AddCircleFilled(screen[i], 2.5f, dot_col);
        }
    }

    if (S.selected_bone >= 0 && S.selected_bone < (int)S.mdl_info.Bones.size()) {
        const std::string& bn = S.mdl_info.Bones[(size_t)S.selected_bone].Name;
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s  [%s]",
                      S.bone_rotate_mode ? "ROTATE" : "selected",
                      bn.c_str());
        ImVec2 ts = ImGui::CalcTextSize(buf);
        ImVec2 tp(origin.x + region.x - ts.x - 12.0f, origin.y + 8.0f);
        dl->AddRectFilled(ImVec2(tp.x - 6, tp.y - 4),
                          ImVec2(tp.x + ts.x + 6, tp.y + ts.y + 4),
                          IM_COL32(20, 22, 28, 200), 4.0f);
        dl->AddText(tp,
                    S.bone_rotate_mode ? IM_COL32(120, 220, 255, 255)
                                       : IM_COL32(220, 230, 240, 240),
                    buf);
    }
}

static int pick_bone_at(const ImVec2& mouse, const ImVec2& origin,
                        const ImVec2& region, float radius_px) {
    if (g_mp.bone_count == 0) return -1;
    std::vector<float> world_pose;
    MP_ComputeWorldPose(g_mp, S.bone_rot_deltas, world_pose);
    std::vector<ImVec2>  screen;
    std::vector<uint8_t> visible;
    project_bones_to_screen(world_pose, g_mp.bone_count, origin, region,
                            screen, visible);

    int   best = -1;
    float best_d2 = radius_px * radius_px;
    for (uint32_t i = 0; i < g_mp.bone_count; ++i) {
        if (!visible[i]) continue;
        float dx = screen[i].x - mouse.x;
        float dy = screen[i].y - mouse.y;
        float d2 = dx * dx + dy * dy;
        if (d2 < best_d2) { best_d2 = d2; best = (int)i; }
    }
    return best;
}

static void rp_quat_rot(const float q[4], const float v[3], float o[3]) {
    const float tx = 2.0f * (q[1]*v[2] - q[2]*v[1]);
    const float ty = 2.0f * (q[2]*v[0] - q[0]*v[2]);
    const float tz = 2.0f * (q[0]*v[1] - q[1]*v[0]);
    o[0] = v[0] + q[3]*tx + (q[1]*tz - q[2]*ty);
    o[1] = v[1] + q[3]*ty + (q[2]*tx - q[0]*tz);
    o[2] = v[2] + q[3]*tz + (q[0]*ty - q[1]*tx);
}

static void rp_quat_rot_inv(const float q[4], const float v[3], float o[3]) {
    const float qc[4] = { -q[0], -q[1], -q[2], q[3] };
    rp_quat_rot(qc, v, o);
}

static bool level_view_ray(const ImVec2& mouse,
                           const ImVec2& origin,
                           const ImVec2& region,
                           float out_origin[3],
                           float out_direction[3]) {
    const float fw = std::max(1.0f, region.x);
    const float fh = std::max(1.0f, region.y);
    const float mx = mouse.x - origin.x;
    const float my = mouse.y - origin.y;
    if (mx < 0.0f || my < 0.0f || mx > fw || my > fh) return false;

    const float fov      = 60.0f * 3.14159265f / 180.0f;
    const float aspect   = fw / fh;
    const float tan_half = std::tan(0.5f * fov);
    const float u_view   = (2.0f * mx / fw - 1.0f) * aspect * tan_half;
    const float v_view   = (1.0f - 2.0f * my / fh) * tan_half;

    const float cy = std::cos(g_flycam.yaw);
    const float sy = std::sin(g_flycam.yaw);
    const float cp = std::cos(g_flycam.pitch);
    const float sp = std::sin(g_flycam.pitch);
    const float fx = sy * cp,  fy = sp,  fz = cy * cp;
    const float rx = cy,       ry = 0.0f, rz = -sy;
    const float ux = -sp * sy, uy = cp,   uz = -sp * cy;

    float dx = rx * u_view + ux * v_view + fx;
    float dy = ry * u_view + uy * v_view + fy;
    float dz = rz * u_view + uz * v_view + fz;
    const float dlen = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dlen <= 1e-6f) return false;

    out_origin[0] = g_flycam.pos[0];
    out_origin[1] = g_flycam.pos[1];
    out_origin[2] = g_flycam.pos[2];
    out_direction[0] = dx / dlen;
    out_direction[1] = dy / dlen;
    out_direction[2] = dz / dlen;
    return true;
}

int pick_level_mesh_at(const ImVec2& mouse,
                       const ImVec2& origin,
                       const ImVec2& region,
                       uint32_t* out_pick_id = nullptr,
                       uint64_t* out_pick_hash = nullptr,
                       float* out_surface_distance = nullptr) {
    if (out_pick_id) *out_pick_id = 0;
    if (out_pick_hash) *out_pick_hash = 0;
    if (out_surface_distance) {
        *out_surface_distance = std::numeric_limits<float>::infinity();
    }
    if (!g_mp.has_model || !g_mp.no_tilt || g_mp.meshes.empty()) return -1;
    float ray_origin[3] = {};
    float ray_direction[3] = {};
    if (!level_view_ray(mouse, origin, region,
                        ray_origin, ray_direction)) return -1;
    const float ox = ray_origin[0];
    const float oy = ray_origin[1];
    const float oz = ray_origin[2];
    const float dx = ray_direction[0];
    const float dy = ray_direction[1];
    const float dz = ray_direction[2];

    auto hit_sphere = [&](const float center[3], float radius,
                          const float o[3], const float dv[3],
                          float& out_t) {
        const float lx = o[0] - center[0];
        const float ly = o[1] - center[1];
        const float lz = o[2] - center[2];
        const float l_dot_d = lx*dv[0] + ly*dv[1] + lz*dv[2];
        const float l_len2  = lx*lx + ly*ly + lz*lz;
        const float r2      = radius * radius;
        const float c       = l_len2 - r2;
        const float disc    = l_dot_d * l_dot_d - c;
        if (disc < 0.0f) return false;
        const float sq = std::sqrt(disc);
        float t = -l_dot_d - sq;
        if (t < 0.0f) t = -l_dot_d + sq;
        if (t < 0.0f) return false;
        out_t = t;
        return true;
    };

    auto hit_triangle = [&](const float* a,
                            const float* b,
                            const float* c,
                            const float ro[3],
                            const float rd[3],
                            float& out_t) {
        const float e1x = b[0] - a[0];
        const float e1y = b[1] - a[1];
        const float e1z = b[2] - a[2];
        const float e2x = c[0] - a[0];
        const float e2y = c[1] - a[1];
        const float e2z = c[2] - a[2];
        const float px = rd[1] * e2z - rd[2] * e2y;
        const float py = rd[2] * e2x - rd[0] * e2z;
        const float pz = rd[0] * e2y - rd[1] * e2x;
        const float det = e1x * px + e1y * py + e1z * pz;
        if (std::fabs(det) < 1e-7f) return false;
        const float inv_det = 1.0f / det;
        const float tx = ro[0] - a[0];
        const float ty = ro[1] - a[1];
        const float tz = ro[2] - a[2];
        const float u = (tx * px + ty * py + tz * pz) * inv_det;
        if (u < 0.0f || u > 1.0f) return false;
        const float qx = ty * e1z - tz * e1y;
        const float qy = tz * e1x - tx * e1z;
        const float qz = tx * e1y - ty * e1x;
        const float v = (rd[0] * qx + rd[1] * qy + rd[2] * qz) * inv_det;
        if (v < 0.0f || u + v > 1.0f) return false;
        const float t = (e2x * qx + e2y * qy + e2z * qz) * inv_det;
        if (t <= 0.0f) return false;
        out_t = t;
        return true;
    };

    int      best      = -1;
    uint32_t best_id   = 0;
    uint64_t best_hash = 0;
    float    best_t    = std::numeric_limits<float>::infinity();
    int      sph_best  = -1;
    float    sph_t     = std::numeric_limits<float>::infinity();
    for (size_t i = 0; i < g_mp.meshes.size(); ++i) {
        const auto& m = g_mp.meshes[i];
        if (m.index_count == 0 || m.radius <= 0.0f) continue;
        if (m.is_entity_model && !S.show_entity_models) continue;
        if (g_mp.selected_lod >= 0 &&
            m.lod_index != (uint32_t)g_mp.selected_lod) continue;
        if (m.is_terrain) continue;
        if (m.is_water)   continue;
        if (is_adjacent_terrain_mesh_name(m.name)) continue;
        if (!m.pick_ranges.empty()) {
            for (const auto& pr : m.pick_ranges) {
                if (pr.selection_id == 0 || pr.radius <= 0.0f) continue;

                float ro[3] = { ox, oy, oz };
                float rd[3] = { dx, dy, dz };
                float tscale = 1.0f;
                auto eo = g_mp.range_edit_xforms.find(pr.selection_id);
                if (eo != g_mp.range_edit_xforms.end() &&
                    eo->second.deleted) continue;
                if (eo != g_mp.range_edit_xforms.end()) {
                    const LevelEdit::EditXform& x = eo->second;
                    const float rel[3] = {
                        ox - x.pivot[0] - x.off[0],
                        oy - x.pivot[1] - x.off[1],
                        oz - x.pivot[2] - x.off[2],
                    };
                    float rrel[3];
                    rp_quat_rot_inv(x.quat, rel, rrel);
                    const float inv_s =
                        x.scale > 1e-6f ? 1.0f / x.scale : 1.0f;
                    ro[0] = rrel[0] * inv_s + x.pivot[0];
                    ro[1] = rrel[1] * inv_s + x.pivot[1];
                    ro[2] = rrel[2] * inv_s + x.pivot[2];
                    const float dv[3] = { dx, dy, dz };
                    rp_quat_rot_inv(x.quat, dv, rd);
                    tscale = x.scale;
                }

                float sphere_t = 0.0f;
                if (!hit_sphere(pr.center, pr.radius, ro, rd, sphere_t)) {
                    continue;
                }

                bool tri_hit = false;
                float tri_t = std::numeric_limits<float>::infinity();
                if (!m.pick_positions.empty() && !m.pick_indices.empty()) {
                    const uint32_t end = std::min<uint32_t>(
                        pr.index_start + pr.index_count,
                        (uint32_t)m.pick_indices.size());
                    for (uint32_t k = pr.index_start; k + 2 < end; k += 3) {
                        const uint32_t ia = m.pick_indices[k + 0];
                        const uint32_t ib = m.pick_indices[k + 1];
                        const uint32_t ic = m.pick_indices[k + 2];
                        const size_t pa = (size_t)ia * 3;
                        const size_t pb = (size_t)ib * 3;
                        const size_t pc = (size_t)ic * 3;
                        if (pa + 2 >= m.pick_positions.size() ||
                            pb + 2 >= m.pick_positions.size() ||
                            pc + 2 >= m.pick_positions.size()) {
                            continue;
                        }
                        float t = 0.0f;
                        if (!hit_triangle(&m.pick_positions[pa],
                                          &m.pick_positions[pb],
                                          &m.pick_positions[pc],
                                          ro, rd, t)) {
                            continue;
                        }
                        if (t < tri_t) {
                            tri_t = t;
                            tri_hit = true;
                        }
                    }
                }

                if (tri_hit && tri_t * tscale < best_t) {
                    best_t = tri_t * tscale;
                    best = (int)i;
                    best_id = pr.selection_id;
                    best_hash = pr.inst_hash;
                }
            }
            continue;
        }
        if (m.name.rfind("engine_level:", 0) == 0) continue;
        if (m.edit_xform.deleted) continue;

        float t = 0.0f;
        float ctr[3] = { m.center[0], m.center[1], m.center[2] };
        float rr = m.radius;
        if (m.edit_xform.active()) {
            const LevelEdit::EditXform& x = m.edit_xform;
            const float rel[3] = {
                (m.center[0] - x.pivot[0]) * x.scale,
                (m.center[1] - x.pivot[1]) * x.scale,
                (m.center[2] - x.pivot[2]) * x.scale,
            };
            float rrel[3];
            rp_quat_rot(x.quat, rel, rrel);
            ctr[0] = rrel[0] + x.pivot[0] + x.off[0];
            ctr[1] = rrel[1] + x.pivot[1] + x.off[1];
            ctr[2] = rrel[2] + x.pivot[2] + x.off[2];
            rr = m.radius * x.scale;
        }
        const float o0[3] = { ox, oy, oz };
        const float d0[3] = { dx, dy, dz };
        if (!hit_sphere(ctr, rr, o0, d0, t)) continue;
        if (t < sph_t) {
            sph_t = t;
            sph_best = (int)i;
        }
    }
    if (best < 0 && sph_best >= 0) {
        best = sph_best;
        best_id = 0;
        best_hash = 0;
    }
    if (out_surface_distance && std::isfinite(best_t)) {
        *out_surface_distance = best_t;
    }
    if (out_pick_id) *out_pick_id = best_id;
    if (out_pick_hash) *out_pick_hash = best_hash;
    return best;
}

static bool level_placement_surface_at(const ImVec2& mouse,
                                       const ImVec2& origin,
                                       const ImVec2& region,
                                       bool allow_forward_fallback,
                                       float out_engine_pos[3]) {
    float ray_origin[3] = {};
    float ray_direction[3] = {};
    if (!level_view_ray(mouse, origin, region,
                        ray_origin, ray_direction)) return false;



    float object_t = std::numeric_limits<float>::infinity();
    pick_level_mesh_at(mouse, origin, region, nullptr, nullptr, &object_t);

    float terrain_hit[3] = {};
    float terrain_t = std::numeric_limits<float>::infinity();
    if (TerrainEdit::Raycast(
            ray_origin[0], ray_origin[1], ray_origin[2],
            ray_direction[0], ray_direction[1], ray_direction[2],
            terrain_hit[0], terrain_hit[1], terrain_hit[2])) {
        const float tx = terrain_hit[0] - ray_origin[0];
        const float ty = terrain_hit[1] - ray_origin[1];
        const float tz = terrain_hit[2] - ray_origin[2];
        terrain_t = tx * ray_direction[0] +
                    ty * ray_direction[1] +
                    tz * ray_direction[2];
        if (terrain_t <= 0.0f) {
            terrain_t = std::numeric_limits<float>::infinity();
        }
    }

    float preview_pos[3] = {};
    const float surface_t = std::min(object_t, terrain_t);
    if (std::isfinite(surface_t)) {
        preview_pos[0] = ray_origin[0] + ray_direction[0] * surface_t;
        preview_pos[1] = ray_origin[1] + ray_direction[1] * surface_t;
        preview_pos[2] = ray_origin[2] + ray_direction[2] * surface_t;
    } else if (std::fabs(ray_direction[1]) > 1e-4f) {
        const float t = (g_mp.center[1] - ray_origin[1]) /
                        ray_direction[1];
        if (t <= 0.0f) return false;
        preview_pos[0] = ray_origin[0] + ray_direction[0] * t;
        preview_pos[1] = ray_origin[1] + ray_direction[1] * t;
        preview_pos[2] = ray_origin[2] + ray_direction[2] * t;
    } else if (allow_forward_fallback) {
        preview_pos[0] = ray_origin[0] + ray_direction[0] * 10.0f;
        preview_pos[1] = ray_origin[1] + ray_direction[1] * 10.0f;
        preview_pos[2] = ray_origin[2] + ray_direction[2] * 10.0f;
    } else {
        return false;
    }


    out_engine_pos[0] = preview_pos[0];
    out_engine_pos[1] = preview_pos[2];
    out_engine_pos[2] = preview_pos[1];
    return true;
}



static bool level_water_surface_at(const ImVec2& mouse,
                                   const ImVec2& origin,
                                   const ImVec2& region,
                                   float out_engine_pos[3]) {
    float ray_origin[3] = {};
    float ray_direction[3] = {};
    if (!level_view_ray(mouse, origin, region, ray_origin,
                        ray_direction)) {
        return false;
    }
    float best_t = std::numeric_limits<float>::infinity();
    for (const auto& mesh : g_mp.meshes) {
        if (!mesh.is_water) continue;
        const float y = mesh.water_params[0];
        if (std::fabs(ray_direction[1]) < 1e-5f) continue;
        const float t = (y - ray_origin[1]) / ray_direction[1];
        if (t <= 0.0f || t >= best_t) continue;
        best_t = t;
    }
    if (!std::isfinite(best_t)) return false;
    const float px = ray_origin[0] + ray_direction[0] * best_t;
    const float py = ray_origin[1] + ray_direction[1] * best_t;
    const float pz = ray_origin[2] + ray_direction[2] * best_t;
    out_engine_pos[0] = px;
    out_engine_pos[1] = pz;
    out_engine_pos[2] = py;
    return true;
}
