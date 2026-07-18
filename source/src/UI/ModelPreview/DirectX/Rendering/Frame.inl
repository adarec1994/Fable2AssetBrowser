void MP_Render(ID3D11Device* dev, ModelPreview& mp, const FlyCam& cam){
    if(!mp.has_model) return;
    ID3D11DeviceContext* ctx=nullptr; dev->GetImmediateContext(&ctx); if(!ctx) return;
    D3D11_VIEWPORT vp{}; vp.TopLeftX=0; vp.TopLeftY=0; vp.Width=(FLOAT)mp.width; vp.Height=(FLOAT)mp.height; vp.MinDepth=0; vp.MaxDepth=1;
    ctx->RSSetViewports(1,&vp);
    const Skybox::CameraFrame sky_camera =
        Skybox::BuildCameraFrame(cam, mp.width, mp.height);
    const float* forward = sky_camera.view_forward;
    const XMFLOAT3 sky_right_f{
        sky_camera.right[0], sky_camera.right[1], sky_camera.right[2]};
    const XMFLOAT3 sky_up_f{
        sky_camera.up[0], sky_camera.up[1], sky_camera.up[2]};
    const float fov = sky_camera.fov_radians;
    const float aspect = sky_camera.aspect;

    const Skybox::FrameState sky_frame = Skybox::EvaluateFrame(mp);
    const float sky_time = static_cast<float>(sky_frame.elapsed_time);
    const auto& render_sky_bottom = sky_frame.sky_bottom;
    const auto& render_weather = sky_frame.weather;
    const float render_mist = sky_frame.mist;
    const auto& render_fog_range = sky_frame.fog_range;
    const auto& render_fog_density = sky_frame.fog_density;
    const bool render_has_fog = sky_frame.has_fog;
    const XMFLOAT3 sun_dir_f{
        sky_frame.sun_direction[0],
        sky_frame.sun_direction[1],
        sky_frame.sun_direction[2]};

    float clear[4]{};
    Skybox::GetClearColour(mp, sky_frame, clear);
    ctx->OMSetRenderTargets(1,&mp.rtv, mp.dsv);
    ctx->ClearRenderTargetView(mp.rtv, clear);
    ctx->ClearDepthStencilView(mp.dsv, D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL, 1.0f, 0);

    Skybox::DrawSky(dev, ctx, mp, sky_camera, sky_frame);
    ctx->IASetInputLayout(mp.layout);
    ctx->VSSetShader(mp.vs,nullptr,0);
    ctx->PSSetShader(mp.ps,nullptr,0);
    ID3D11SamplerState* samplers[2] = { mp.sampler, mp.sampler_point };
    ctx->PSSetSamplers(0, 2, samplers);

    ctx->RSSetState((mp.wireframe && mp.rs_wire) ? mp.rs_wire : mp.rs);
    XMVECTOR eye = XMVectorSet(cam.pos[0], cam.pos[1], cam.pos[2], 1);
    XMVECTOR at = XMVectorSet(cam.pos[0] + forward[0], cam.pos[1] + forward[1], cam.pos[2] + forward[2], 1);
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);
    XMMATRIX V = XMMatrixLookAtLH(eye, at, up);
    const float far_plane = mp.radius * 100.0f;
    XMMATRIX P = XMMatrixPerspectiveFovLH(fov, aspect, 0.05f, far_plane);
    XMMATRIX W = XMMatrixIdentity();
    if (!mp.no_tilt) {
        const float tiltX = -XM_PIDIV2;
        XMMATRIX Tm = XMMatrixTranslation(-mp.center[0], -mp.center[1], -mp.center[2]);
        XMMATRIX R  = XMMatrixRotationX(tiltX);
        XMMATRIX Tp = XMMatrixTranslation( mp.center[0],  mp.center[1],  mp.center[2]);
        W  = Tm * R * Tp;
        XMMATRIX FlipX = XMMatrixScaling(-1.0f, 1.0f, 1.0f);
        W = W * FlipX;
    }

    Skybox::DrawClouds(
        ctx, mp, cam, sky_camera, sky_frame, V, P);

    XMMATRIX MVP = XMMatrixTranspose(W * V * P);
    XMMATRIX MV  = XMMatrixTranspose(W * V);
    XMVECTOR lightDirV = XMVector3Normalize(
        XMVectorSet(sun_dir_f.x,
                    std::max(sun_dir_f.y, 0.15f),
                    sun_dir_f.z,
                    0.0f));
    XMFLOAT4 lightDirF; XMStoreFloat4(&lightDirF, lightDirV);
    struct CB {
        XMFLOAT4X4 mvp;
        XMFLOAT4 lightDir;
        XMFLOAT4X4 mv;
        XMFLOAT4 params;
        XMFLOAT4 edit_off;
        XMFLOAT4 edit_rot;
        XMFLOAT4 edit_piv;
    } cb{};
    XMStoreFloat4x4(&cb.mvp, MVP);
    XMStoreFloat4x4(&cb.mv,  MV);
    cb.lightDir = lightDirF;
    cb.params   = XMFLOAT4(0.4f, 48.0f, 0.0f, 0.0f);
    D3D11_MAPPED_SUBRESOURCE ms{};
    if(SUCCEEDED(ctx->Map(mp.cbuffer,0,D3D11_MAP_WRITE_DISCARD,0,&ms))){
        memcpy(ms.pData, &cb, sizeof(cb));
        ctx->Unmap(mp.cbuffer,0);
    }
    ctx->VSSetConstantBuffers(0,1,&mp.cbuffer);
    ctx->PSSetConstantBuffers(0,1,&mp.cbuffer);

    if (mp.cbuffer_fog) {
        const bool fog_enabled =
            mp.show_mist && mp.no_tilt &&
            (render_has_fog || render_mist > 0.0001f);
        float mist_base = mp.center[1];
        bool have_terrain = false;
        for (const auto& m : mp.meshes) {
            if (!m.is_terrain) continue;
            if (!have_terrain || m.center[1] < mist_base) {
                mist_base = m.center[1];
            }
            have_terrain = true;
        }
        struct FogCBData {
            XMFLOAT4 colour;
            XMFLOAT4 dist;
            XMFLOAT4 density;
            XMFLOAT4 misc;
        } fog_cb{};
        auto fin = [](float v, float fb) {
            return std::isfinite(v) ? v : fb;
        };

        const float fog_start = std::max(fin(render_fog_range[0], 0.0f),
                                         0.0f);
        const float near_dist = fin(render_fog_range[1], 60.0f);
        const float far_dist = fin(render_fog_range[2], 400.0f);
        const float span1 = std::max(near_dist - fog_start, 1.0f);
        const float span2 = std::max(far_dist - fog_start, span1 + 9.0f);
        const float d1 =
            std::max(std::clamp(fin(render_fog_density[0], 0.0f),
                                0.0f, 1.0f), 0.01f);
        const float d2 =
            std::max(std::clamp(fin(render_fog_density[1], 0.0f),
                                0.0f, 1.0f), 0.01f);
        const float od_far = d2 * span2 + (d1 - d2) * span1;
        float fog_e = 1.0f;
        if (od_far > 0.0001f) {
            const float num = std::log10(d1 * span1 / od_far);
            const float den = std::log10(span1 / span2);
            if (std::isfinite(num) && std::isfinite(den) &&
                std::fabs(den) > 0.0001f) {
                fog_e = std::clamp(num / den, 0.05f, 8.0f);
            }
        }
        const float fog_k =
            -std::log(std::max(1.0f - d2, 0.02f)) /
            std::max(od_far, 0.0001f);
        const float fog_amp =
            (render_has_fog ? od_far * fog_k : 0.0f);
        fog_cb.colour = XMFLOAT4(
            std::clamp(fin(render_sky_bottom[0], 0.48f), 0.0f, 1.0f),
            std::clamp(fin(render_sky_bottom[1], 0.55f), 0.0f, 1.0f),
            std::clamp(fin(render_sky_bottom[2], 0.62f), 0.0f, 1.0f),
            fog_enabled ? 1.0f : 0.0f);
        fog_cb.dist = XMFLOAT4(
            fog_start,
            1.0f / span2,
            fog_e,
            fog_amp);
        fog_cb.density = XMFLOAT4(
            0.0f,
            0.0f,
            std::clamp(fin(render_mist, 0.0f), 0.0f, 1.0f) * 0.75f,
            25.0f);
        fog_cb.misc = XMFLOAT4(
            mist_base + 4.0f,
            4.0f,
            sky_time,
            0.0f);
        D3D11_MAPPED_SUBRESOURCE fms{};
        if (SUCCEEDED(ctx->Map(mp.cbuffer_fog, 0,
                               D3D11_MAP_WRITE_DISCARD, 0, &fms))) {
            std::memcpy(fms.pData, &fog_cb, sizeof(fog_cb));
            ctx->Unmap(mp.cbuffer_fog, 0);
        }
        ctx->PSSetConstantBuffers(5, 1, &mp.cbuffer_fog);
    }

    std::vector<XMFLOAT4X4> bone_mats(MP_MAX_BONES);
    for (uint32_t i = 0; i < MP_MAX_BONES; ++i){
        XMStoreFloat4x4(&bone_mats[i], XMMatrixIdentity());
    }
    if (mp.bone_count > 0) {
        const uint32_t n = mp.bone_count;

        std::vector<XMFLOAT4X4> local(n);
        bool have_deltas = (S.bone_rot_deltas.size() >= (size_t)n * 4);
        const bool have_anim_pose =
            S.bone_anim_pose_active &&
            S.bone_anim_rot_absolute.size() >= (size_t)n * 4 &&
            S.bone_anim_rot_present.size() >= (size_t)n;
        const bool have_anim_trans =
            S.bone_anim_trans_delta.size() >= (size_t)n * 3 &&
            S.bone_anim_trans_present.size() >= (size_t)n;
        for (uint32_t i = 0; i < n; ++i){
            const float* tf = &mp.local_rest[(size_t)i * 11];
            const float* dq = have_deltas ? &S.bone_rot_deltas[(size_t)i * 4] : nullptr;
            const float* aq =
                (have_anim_pose && S.bone_anim_rot_present[(size_t)i])
                    ? &S.bone_anim_rot_absolute[(size_t)i * 4]
                    : nullptr;
            const float* at =
                (have_anim_pose && have_anim_trans &&
                 S.bone_anim_trans_present[(size_t)i])
                    ? &S.bone_anim_trans_delta[(size_t)i * 3]
                    : nullptr;
            XMMATRIX L = have_anim_pose
                ? bone_local_matrix_anim_delta(tf, aq, at)
                : bone_local_matrix(tf, dq);
            XMStoreFloat4x4(&local[i], L);
        }

        std::vector<XMFLOAT4X4> world(n);
        std::vector<uint8_t> done(n, 0);
        for (uint32_t i = 0; i < n; ++i){
            if (done[i]) continue;
            std::vector<int> chain;
            int cur = (int)i;
            while (cur >= 0 && cur < (int)n && !done[cur]){
                chain.push_back(cur);
                cur = mp.bone_parents[cur];
            }
            XMMATRIX accum;
            if (cur >= 0 && cur < (int)n) accum = XMLoadFloat4x4(&world[cur]);
            else                          accum = XMMatrixIdentity();
            for (auto it = chain.rbegin(); it != chain.rend(); ++it){
                XMMATRIX L = XMLoadFloat4x4(&local[*it]);
                accum = L * accum;
                XMStoreFloat4x4(&world[*it], accum);
                done[*it] = 1;
            }
        }

        for (uint32_t i = 0; i < n; ++i){
            XMFLOAT4X4 ib_f;
            std::memcpy(&ib_f, &mp.inv_bind[(size_t)i * 16], sizeof(float) * 16);
            XMMATRIX ib = XMLoadFloat4x4(&ib_f);
            XMMATRIX W  = XMLoadFloat4x4(&world[i]);
            XMMATRIX skin = ib * W;
            XMStoreFloat4x4(&bone_mats[i], skin);
        }
    }
    if (mp.bone_cb) {
        D3D11_MAPPED_SUBRESOURCE bms{};
        if (SUCCEEDED(ctx->Map(mp.bone_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &bms))){
            std::memcpy(bms.pData, bone_mats.data(),
                        sizeof(XMFLOAT4X4) * MP_MAX_BONES);
            ctx->Unmap(mp.bone_cb, 0);
        }
        ctx->VSSetConstantBuffers(1, 1, &mp.bone_cb);
    }

    for (auto& m : mp.meshes){
        if (!m.cloth_sim || !m.cloth || !m.vb) continue;
        mp_step_cloth(*m.cloth, bone_mats.data(), MP_MAX_BONES);
        D3D11_MAPPED_SUBRESOURCE cms{};
        if (SUCCEEDED(ctx->Map(m.vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &cms))){
            std::memcpy(cms.pData, m.cloth->vtx.data(),
                        m.cloth->vtx.size() * sizeof(MPVertex));
            ctx->Unmap(m.vb, 0);
        }
    }

    bool any_isolated = false;
    for (const auto& mm : mp.meshes) {
        if (mp_should_hide_mesh(mm)) continue;
        if (mm.isolated) { any_isolated = true; break; }
    }

    const float water_time = (float)ImGui::GetTime();
    auto upload_per_mesh_cb = [&](bool highlight, bool is_cloth, bool alpha_test,
                                  bool no_skin = false,
                                  const LevelEdit::EditXform* ex = nullptr){

        cb.params = XMFLOAT4(alpha_test ? 1.0f : 0.0f, no_skin ? 1.0f : 0.0f,
                             is_cloth ? 2.0f : (highlight ? 1.0f : 0.0f),
                             water_time);
        if (ex) {
            cb.edit_off = XMFLOAT4(ex->off[0], ex->off[1], ex->off[2], 1.0f);
            cb.edit_rot = XMFLOAT4(ex->quat[0], ex->quat[1], ex->quat[2],
                                   ex->quat[3]);
            cb.edit_piv = XMFLOAT4(ex->pivot[0], ex->pivot[1], ex->pivot[2],
                                   ex->scale);
        } else {
            cb.edit_off = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
            cb.edit_rot = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
            cb.edit_piv = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
        }
        D3D11_MAPPED_SUBRESOURCE pms{};
        if (SUCCEEDED(ctx->Map(mp.cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &pms))) {
            std::memcpy(pms.pData, &cb, sizeof(cb));
            ctx->Unmap(mp.cbuffer, 0);
        }
    };

    float blend_factor[4] = {0,0,0,0};

    auto draw_one = [&](const MPPerMesh& m, ID3D11BlendState* bs) {
        UINT stride=sizeof(MPVertex), offset=0;
        ctx->IASetVertexBuffers(0,1,&m.vb,&stride,&offset);
        ctx->IASetIndexBuffer(m.ib, DXGI_FORMAT_R32_UINT, 0);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->OMSetBlendState(bs, blend_factor, 0xFFFFFFFF);

        const auto& paint = TerrainPaint::GetRenderResources();
        const bool use_painted_terrain =
            m.is_terrain && !mp.meshes.empty() &&
            &m == &mp.meshes.front() && paint.ok && paint.weights &&
            paint.layer_count > 0 && mp.vs_terrain &&
            mp.ps_terrain_paint && mp.cbuffer_terrain_paint;
        if (use_painted_terrain) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(ctx->Map(mp.cbuffer_terrain_paint, 0,
                                   D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                struct PaintCB {
                    float info[4];
                    float scales[4][4];
                    uint32_t flags[4];
                } cb{};
                cb.info[0] = (float)paint.layer_count;
                cb.info[1] = paint.weight_w > 0
                                 ? 1.0f / (float)paint.weight_w
                                 : 1.0f;
                cb.info[2] = paint.weight_h > 0
                                 ? 1.0f / (float)paint.weight_h
                                 : 1.0f;
                for (int i = 0; i < paint.layer_count; ++i) {
                    cb.scales[i / 4][i % 4] = paint.tile_scale[i];
                }
                cb.flags[0] = paint.normal_mask;
                std::memcpy(mapped.pData, &cb, sizeof(cb));
                ctx->Unmap(mp.cbuffer_terrain_paint, 0);
            }

            ID3D11ShaderResourceView* diffuse[TerrainPaint::kMaxLayers]{};
            ID3D11ShaderResourceView* normal[TerrainPaint::kMaxLayers]{};
            for (int i = 0; i < TerrainPaint::kMaxLayers; ++i) {
                diffuse[i] = paint.diffuse[i]
                                 ? paint.diffuse[i]
                                 : mp.default_srv;
                normal[i] = paint.normal[i];
            }
            ctx->VSSetShader(mp.vs_terrain, nullptr, 0);
            ctx->PSSetShader(mp.ps_terrain_paint, nullptr, 0);
            ctx->VSSetConstantBuffers(2, 1, &mp.cbuffer_terrain_paint);
            ctx->PSSetConstantBuffers(2, 1, &mp.cbuffer_terrain_paint);
            ctx->PSSetShaderResources(0, TerrainPaint::kMaxLayers, diffuse);
            ctx->PSSetShaderResources(16, TerrainPaint::kMaxLayers, normal);
            ctx->PSSetShaderResources(32, 1, &paint.weights);
            ctx->DrawIndexed(m.index_count, 0, 0);

            ID3D11ShaderResourceView* nulls[33]{};
            ctx->PSSetShaderResources(0, 33, nulls);
            ID3D11Buffer* null_cb = nullptr;
            ctx->VSSetConstantBuffers(2, 1, &null_cb);
            ctx->PSSetConstantBuffers(2, 1, &null_cb);
            ctx->VSSetShader(mp.vs, nullptr, 0);
            ctx->PSSetShader(mp.ps, nullptr, 0);
            return;
        }

        const auto& R = TerrainSplat::Get();
        const bool use_direct_terrain =
            !R.material_weight_array && mp.ps_terrain_direct;
        const bool use_terrain_shader =
            m.is_terrain && R.ok &&
            R.lod_diffuse_array && R.lod_detail_array &&
            R.chunk_idx_array && R.chunk_blend_array && R.chunk_uv_array &&
            R.splat_mask && R.lightmap &&
            (use_direct_terrain || R.material_weight_array) &&
            mp.vs_terrain && mp.ps_terrain && mp.cbuffer_terrain;

        if (use_terrain_shader) {
            static uint32_t s_logged_splat_generation = 0;
            if (s_logged_splat_generation != R.generation) {
                s_logged_splat_generation = R.generation;
                OutputLog::success(std::string(use_direct_terrain
                    ? "terrain direct layer fallback draw active: "
                    : "terrain SPLAT shader draw active: ")
                    + std::to_string(R.chunk_w) + "x"
                    + std::to_string(R.chunk_h) + " chunks, "
                    + std::to_string(R.lod_count) + " material slices");
            }
            D3D11_MAPPED_SUBRESOURCE tms{};
            if (SUCCEEDED(ctx->Map(mp.cbuffer_terrain, 0,
                                   D3D11_MAP_WRITE_DISCARD, 0, &tms))) {
                struct TCB {
                    float origin_extent[4];
                    float grid_size[4];
                    float splat_params[4];
                    float mesh_xform[4];
                    float weight_size[4];
                    float material_params[32][4];
                } t{};
                t.origin_extent[0] = R.world_origin_x;
                t.origin_extent[1] = R.world_origin_z;
                t.origin_extent[2] = R.chunk_extent_x;
                t.origin_extent[3] = R.chunk_extent_z;
                t.grid_size[0] = (float)R.chunk_w;
                t.grid_size[1] = (float)R.chunk_h;
                t.grid_size[2] = (float)R.lod_count;
                t.grid_size[3] = 255.f;
                t.splat_params[0] = 3.0f;
                t.splat_params[1] = (R.tile_scale > 0.0f)
                    ? R.tile_scale : 0.125f;
                t.splat_params[2] = (R.splat_w > 0)
                    ? 32.0f / float(R.splat_w) : 0.0f;
                t.splat_params[3] = (R.splat_h > 0)
                    ? 32.0f / float(R.splat_h) : 0.0f;
                t.mesh_xform[0] = 1.0f;
                t.mesh_xform[1] = 1.0f;
                t.mesh_xform[2] = R.mesh_to_world_x;
                t.mesh_xform[3] = R.mesh_to_world_z;
                t.weight_size[0] = (float)R.weight_w;
                t.weight_size[1] = (float)R.weight_h;
                t.weight_size[2] = (R.weight_w > 0)
                    ? 1.0f / (float)R.weight_w : 1.0f;
                t.weight_size[3] = (R.weight_h > 0)
                    ? 1.0f / (float)R.weight_h : 1.0f;
                for (int mi = 0; mi < 32; ++mi) {
                    for (int mj = 0; mj < 4; ++mj) {
                        t.material_params[mi][mj] =
                            R.material_params[mi][mj];
                    }
                }
                std::memcpy(tms.pData, &t, sizeof(t));
                ctx->Unmap(mp.cbuffer_terrain, 0);
            }

            ctx->VSSetShader(mp.vs_terrain, nullptr, 0);
            ctx->PSSetShader(use_direct_terrain ? mp.ps_terrain_direct
                                                : mp.ps_terrain, nullptr, 0);
            ctx->VSSetConstantBuffers(2, 1, &mp.cbuffer_terrain);
            ctx->PSSetConstantBuffers(2, 1, &mp.cbuffer_terrain);

            if (use_direct_terrain) {
                ID3D11ShaderResourceView* srvs[6] = {
                    R.lod_diffuse_array,
                    R.chunk_idx_array,
                    R.chunk_blend_array,
                    R.chunk_uv_array,
                    R.splat_mask,
                    R.lightmap
                };
                ctx->PSSetShaderResources(0, 6, srvs);
            } else {
                ID3D11ShaderResourceView* srvs[8] = {
                    R.lod_diffuse_array,
                    R.chunk_idx_array,
                    R.chunk_blend_array,
                    R.chunk_uv_array,
                    R.splat_mask,
                    R.lightmap,
                    R.lod_detail_array,
                    R.material_weight_array
                };
                ctx->PSSetShaderResources(0, 8, srvs);
            }

            ctx->DrawIndexed(m.index_count, 0, 0);

            ID3D11ShaderResourceView* nulls[8] = {
                nullptr, nullptr, nullptr, nullptr,
                nullptr, nullptr, nullptr, nullptr
            };
            ctx->PSSetShaderResources(0, 8, nulls);
            ID3D11Buffer* null_cb = nullptr;
            ctx->VSSetConstantBuffers(2, 1, &null_cb);
            ctx->PSSetConstantBuffers(2, 1, &null_cb);

            ctx->VSSetShader(mp.vs, nullptr, 0);
            ctx->PSSetShader(mp.ps, nullptr, 0);
            return;
        }

        const bool use_water_shader =
            m.is_water && mp.vs_water && mp.ps_water && mp.cbuffer_water;

        if (use_water_shader) {

            D3D11_MAPPED_SUBRESOURCE wms{};
            if (SUCCEEDED(ctx->Map(mp.cbuffer_water, 0,
                                   D3D11_MAP_WRITE_DISCARD, 0, &wms))) {
                struct WCB {
                    float w_bias[4];
                    float w_ph01[4];
                    float w_sc01[4];
                    float w_surface[4];
                    float w_deep[4];
                    float w_reflp[4];
                    float w_glit[4];
                    float w_eye[4];
                    float w_sun[4];
                    float w_light[4];
                } w{};

                auto p = [&](int idx, float fallback) {
                    const float v = (idx >= 0 && idx + 1 < 38)
                        ? m.water_params[idx + 1] : fallback;
                    return std::isfinite(v) ? v : fallback;
                };
                auto phase = [&](float speed) {
                    const double ph = double(water_time) * double(speed);
                    return float(ph - std::floor(ph));
                };

                w.w_bias[0] = p(0, 0.2f);
                w.w_bias[1] = p(1, 0.0f);
                w.w_bias[2] = p(24, 0.05f);
                w.w_bias[3] = std::clamp(m.has_water_theme
                    ? m.water_opacity : 0.78f, 0.05f, 1.0f);

                w.w_ph01[0] = phase(p(2,  0.052f));
                w.w_ph01[1] = phase(p(3,  0.011f));
                w.w_ph01[2] = phase(p(4, -0.019f));
                w.w_ph01[3] = phase(p(5,  0.019f));
                w.w_sc01[0] = p(6, 0.188f);
                w.w_sc01[1] = p(7, 0.188f);
                w.w_sc01[2] = p(8, 0.220f);
                w.w_sc01[3] = p(9, 0.220f);

                w.w_surface[0] = m.water_shallow_colour[0];
                w.w_surface[1] = m.water_shallow_colour[1];
                w.w_surface[2] = m.water_shallow_colour[2];
                w.w_surface[3] = p(30, 0.1f);
                w.w_deep[0] = m.water_deep_colour[0];
                w.w_deep[1] = m.water_deep_colour[1];
                w.w_deep[2] = m.water_deep_colour[2];
                w.w_deep[3] = p(29, 0.75f);

                w.w_reflp[0] = p(25, 2.0f);
                w.w_reflp[1] = p(27, 2.0f);
                w.w_reflp[2] = p(34, 0.3f);
                w.w_reflp[3] = p(35, 5.0f);

                const bool sky_ok = mp.has_sky_theme && mp.show_sky &&
                                    mp.cbuffer_sky;

                const bool dome_ok = sky_ok && mp.cbuffer_sky_dome &&
                                     mp.sky_lut_srv && mp.sampler_sky_clamp;
                w.w_glit[0] = p(36, 128.0f);
                w.w_glit[1] = 0.0f;
                w.w_glit[2] = sky_ok ? 1.0f : 0.0f;
                w.w_glit[3] = dome_ok ? 1.0f : 0.0f;

                w.w_eye[0] = cam.pos[0];
                w.w_eye[1] = cam.pos[1];
                w.w_eye[2] = cam.pos[2];
                w.w_eye[3] = water_time;

                if (mp.has_sky_theme) {
                    w.w_sun[0] = sky_frame.main_light_colour[0];
                    w.w_sun[1] = sky_frame.main_light_colour[1];
                    w.w_sun[2] = sky_frame.main_light_colour[2];
                } else {
                    const float sun_up =
                        std::clamp(sun_dir_f.y * 3.0f, 0.0f, 1.0f);
                    w.w_sun[0] = 1.0f;
                    w.w_sun[1] = 0.55f + 0.42f * sun_up;
                    w.w_sun[2] = 0.30f + 0.60f * sun_up;
                }
                w.w_sun[3] = sun_dir_f.y;

                if (sun_dir_f.y < 0.0f && mp.has_moon_axis) {
                    w.w_light[0] = -sky_frame.moon_direction[0];
                    w.w_light[1] = -sky_frame.moon_direction[1];
                    w.w_light[2] = -sky_frame.moon_direction[2];
                } else {
                    w.w_light[0] = sun_dir_f.x;
                    w.w_light[1] = sun_dir_f.y;
                    w.w_light[2] = sun_dir_f.z;
                }
                w.w_light[3] = 0.0f;

                std::memcpy(wms.pData, &w, sizeof(w));
                ctx->Unmap(mp.cbuffer_water, 0);

            }

            ctx->VSSetShader(mp.vs_water, nullptr, 0);
            ctx->PSSetShader(mp.ps_water, nullptr, 0);
            ctx->VSSetConstantBuffers(3, 1, &mp.cbuffer_water);
            ctx->PSSetConstantBuffers(3, 1, &mp.cbuffer_water);
            if (mp.cbuffer_sky) {
                ctx->PSSetConstantBuffers(4, 1, &mp.cbuffer_sky);
            }

            if (mp.cbuffer_sky_dome) {
                ctx->PSSetConstantBuffers(6, 1, &mp.cbuffer_sky_dome);
            }
            if (mp.sky_lut_srv) {
                ctx->PSSetShaderResources(4, 1, &mp.sky_lut_srv);
            }
            if (mp.sampler_sky_clamp) {
                ctx->PSSetSamplers(2, 1, &mp.sampler_sky_clamp);
            }

            ID3D11ShaderResourceView* nrm_srv =
                m.srv_diffuse ? m.srv_diffuse : mp.default_srv;
            ctx->PSSetShaderResources(0, 1, &nrm_srv);

            ctx->DrawIndexed(m.index_count, 0, 0);

            ID3D11ShaderResourceView* null_srv = nullptr;
            ctx->PSSetShaderResources(0, 1, &null_srv);
            ID3D11Buffer* null_cb = nullptr;
            ctx->VSSetConstantBuffers(3, 1, &null_cb);
            ctx->PSSetConstantBuffers(3, 1, &null_cb);

            ctx->VSSetShader(mp.vs, nullptr, 0);
            ctx->PSSetShader(mp.ps, nullptr, 0);
            return;
        }

        ID3D11ShaderResourceView* diffuse_to_use  =
            (m.diffuse_visible  && m.srv_diffuse)  ? m.srv_diffuse  : mp.default_srv;
        ID3D11ShaderResourceView* normal_to_use   =
            (m.normal_visible   && m.srv_normal)   ? m.srv_normal   : mp.default_srv;
        ID3D11ShaderResourceView* specular_to_use =
            (m.specular_visible && m.srv_specular) ? m.srv_specular : mp.default_srv;
        ID3D11ShaderResourceView* metallic_to_use =
            (m.metallic_visible && m.srv_metallic) ? m.srv_metallic : mp.default_srv;
        ID3D11ShaderResourceView* extra_to_use    =
            (m.extra_visible    && m.srv_extra)    ? m.srv_extra    : mp.default_srv;
        ID3D11ShaderResourceView* srvs[5] = { diffuse_to_use, normal_to_use, specular_to_use,
                                              metallic_to_use, extra_to_use };
        ctx->PSSetShaderResources(0, 5, srvs);
        ctx->DrawIndexed(m.index_count, 0, 0);
        ID3D11ShaderResourceView* nullsrvs[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
        ctx->PSSetShaderResources(0, 5, nullsrvs);
    };

    auto draw_regular_range = [&](const MPPerMesh& m,
                                  uint32_t index_start,
                                  uint32_t index_count,
                                  ID3D11BlendState* bs) {
        if (index_count == 0 || index_start >= m.index_count) return;
        if (index_start + index_count > m.index_count) {
            index_count = m.index_count - index_start;
        }
        UINT stride=sizeof(MPVertex), offset=0;
        ctx->IASetInputLayout(mp.layout);
        ctx->VSSetShader(mp.vs, nullptr, 0);
        ctx->PSSetShader(mp.ps, nullptr, 0);
        ctx->IASetVertexBuffers(0,1,&m.vb,&stride,&offset);
        ctx->IASetIndexBuffer(m.ib, DXGI_FORMAT_R32_UINT, 0);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->OMSetBlendState(bs, blend_factor, 0xFFFFFFFF);

        ID3D11ShaderResourceView* diffuse_to_use  =
            (m.diffuse_visible  && m.srv_diffuse)  ? m.srv_diffuse  : mp.default_srv;
        ID3D11ShaderResourceView* normal_to_use   =
            (m.normal_visible   && m.srv_normal)   ? m.srv_normal   : mp.default_srv;
        ID3D11ShaderResourceView* specular_to_use =
            (m.specular_visible && m.srv_specular) ? m.srv_specular : mp.default_srv;
        ID3D11ShaderResourceView* metallic_to_use =
            (m.metallic_visible && m.srv_metallic) ? m.srv_metallic : mp.default_srv;
        ID3D11ShaderResourceView* extra_to_use    =
            (m.extra_visible    && m.srv_extra)    ? m.srv_extra    : mp.default_srv;
        ID3D11ShaderResourceView* srvs[5] = {
            diffuse_to_use, normal_to_use, specular_to_use,
            metallic_to_use, extra_to_use
        };
        ctx->PSSetShaderResources(0, 5, srvs);
        ctx->DrawIndexed(index_count, index_start, 0);
        ID3D11ShaderResourceView* nullsrvs[5] = {
            nullptr, nullptr, nullptr, nullptr, nullptr
        };
        ctx->PSSetShaderResources(0, 5, nullsrvs);
    };

    auto draw_one_with_edits = [&](const MPPerMesh& m, ID3D11BlendState* bs) {
        if (m.edit_xform.deleted) return;
        const LevelEdit::EditXform* base =
            m.edit_xform.active() ? &m.edit_xform : nullptr;
        struct Seg {
            uint32_t start, count;
            const LevelEdit::EditXform* ex;
        };
        std::vector<Seg> edited;
        if (!mp.range_edit_xforms.empty() &&
            !m.pick_ranges.empty() &&
            !m.is_terrain && !m.is_water && !m.cloth_sim) {
            for (const auto& pr : m.pick_ranges) {
                auto it = mp.range_edit_xforms.find(pr.selection_id);
                if (it == mp.range_edit_xforms.end()) continue;
                edited.push_back({pr.index_start, pr.index_count,
                                  &it->second});
            }
        }
        upload_per_mesh_cb(m.highlight, m.is_cloth, m.alpha_test,
                           m.cloth_sim, base);
        if (edited.empty()) {
            draw_one(m, bs);
            return;
        }
        std::sort(edited.begin(), edited.end(),
                  [](const Seg& a, const Seg& b) {
                      return a.start < b.start;
                  });
        uint32_t cursor = 0;
        for (const auto& s : edited) {
            if (s.start > cursor) {
                draw_regular_range(m, cursor, s.start - cursor, bs);
            }
            if (s.ex && !s.ex->deleted) {
                upload_per_mesh_cb(m.highlight, m.is_cloth, m.alpha_test,
                                   m.cloth_sim, s.ex);
                draw_regular_range(m, s.start, s.count, bs);
                upload_per_mesh_cb(m.highlight, m.is_cloth, m.alpha_test,
                                   m.cloth_sim, base);
            }
            if (s.start + s.count > cursor) cursor = s.start + s.count;
        }
        if (cursor < m.index_count) {
            draw_regular_range(m, cursor, m.index_count - cursor, bs);
        }
    };

    ctx->OMSetDepthStencilState(mp.dssWrite, 0);
    for(const auto& m : mp.meshes){
        if (mp_should_hide_mesh(m)) continue;
        if(!m.vb || !m.ib || m.index_count==0 || (m.has_alpha && m.alpha_test)) continue;
        if (m.is_water) continue;
        if (any_isolated && !m.isolated) continue;
        if (mp.selected_lod >= 0 &&
            m.lod_index != (uint32_t)mp.selected_lod) continue;
        draw_one_with_edits(m, mp.bs);
    }
    ctx->OMSetDepthStencilState(mp.dssNoWrite, 0);
    for(const auto& m : mp.meshes){
        if (mp_should_hide_mesh(m)) continue;
        if(!m.vb || !m.ib || m.index_count==0 || !m.has_alpha || !m.alpha_test) continue;
        if (any_isolated && !m.isolated) continue;
        if (mp.selected_lod >= 0 &&
            m.lod_index != (uint32_t)mp.selected_lod) continue;
        draw_one_with_edits(m, mp.bsAlpha);
    }

    ctx->OMSetDepthStencilState(
        mp.dssWaterOnce ? mp.dssWaterOnce : mp.dssWrite, 0);
    for(const auto& m : mp.meshes){
        if (mp_should_hide_mesh(m)) continue;
        if(!m.vb || !m.ib || m.index_count==0 || !m.is_water) continue;
        if (any_isolated && !m.isolated) continue;
        if (mp.selected_lod >= 0 &&
            m.lod_index != (uint32_t)mp.selected_lod) continue;
        const LevelEdit::EditXform* water_xform =
            m.edit_xform.active() ? &m.edit_xform : nullptr;
        upload_per_mesh_cb(
            m.highlight, m.is_cloth, m.alpha_test, m.cloth_sim,
            water_xform);
        const bool water_ok =
            mp.vs_water && mp.ps_water && mp.cbuffer_water && mp.bs_water;
        draw_one(m, water_ok ? mp.bs_water : mp.bs);
    }
    ctx->OMSetDepthStencilState(mp.dssNoWrite, 0);

    if (mp.show_weather && mp.no_tilt && mp.has_weather_theme &&
        mp.vs_weather && mp.ps_weather && mp.cbuffer_weather) {
        const float rain_density = std::clamp(
            render_weather[0] * std::max(mp.rain_intensity_mult, 0.0f),
            0.0f, 1.0f);
        const float rain_size = std::clamp(render_weather[1], 0.0f, 4.0f);
        const float snow_speed = std::clamp(render_weather[2], 0.0f, 6.0f);
        const float snow_size = std::clamp(render_weather[3], 0.0f, 4.0f);
        const bool has_snow = snow_speed > 0.001f && snow_size > 0.001f;
        int rain_count = (int)(rain_density * 15000.0f);
        rain_count = std::clamp(rain_count, 0, 20000);
        if (rain_size <= 0.001f) rain_count = 0;

        int snow_count = has_snow
            ? (int)(4096.0f * std::max(mp.snow_intensity_mult, 0.0f))
            : 0;
        snow_count = std::clamp(snow_count, 0, 8000);

        if (rain_count > 0 || snow_count > 0) {
            struct WeatherCBData {
                XMFLOAT4X4 wvp;
                XMFLOAT4 cam_time;
                XMFLOAT4 wind_vec;
                XMFLOAT4 rain_p;
                XMFLOAT4 snow_p;
                XMFLOAT4 area;
                XMFLOAT4 cam_right;
                XMFLOAT4 cam_up;
            } wx{};
            XMMATRIX VP = XMMatrixTranspose(V * P);
            XMStoreFloat4x4(&wx.wvp, VP);
            wx.cam_time = XMFLOAT4(cam.pos[0], cam.pos[1], cam.pos[2],
                                   sky_time);
            const float wind_az = mp.weather_wind[0];
            const float wind_str = std::clamp(mp.weather_wind[1],
                                              0.0f, 40.0f);
            wx.wind_vec = XMFLOAT4(std::cos(wind_az) * wind_str,
                                   0.0f,
                                   std::sin(wind_az) * wind_str,
                                   0.0f);
            wx.rain_p = XMFLOAT4((float)rain_count,
                                 std::max(rain_size, 0.15f),
                                 14.0f,
                                 0.34f);
            wx.snow_p = XMFLOAT4((float)snow_count,
                                 std::max(snow_size, 0.15f),
                                 std::max(snow_speed, 0.15f),
                                 1.0f);
            wx.area = XMFLOAT4(30.0f, 24.0f, 22.0f, 18.0f);
            wx.cam_right = XMFLOAT4(sky_right_f.x, sky_right_f.y,
                                    sky_right_f.z,
                                    sky_camera.tan_half_x);
            wx.cam_up = XMFLOAT4(sky_up_f.x, sky_up_f.y,
                                 sky_up_f.z, sky_camera.tan_half_y);
            D3D11_MAPPED_SUBRESOURCE xms{};
            if (SUCCEEDED(ctx->Map(mp.cbuffer_weather, 0,
                                   D3D11_MAP_WRITE_DISCARD, 0, &xms))) {
                std::memcpy(xms.pData, &wx, sizeof(wx));
                ctx->Unmap(mp.cbuffer_weather, 0);
            }

            ctx->IASetInputLayout(nullptr);
            ctx->IASetPrimitiveTopology(
                D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ctx->RSSetState(mp.rs);
            ctx->OMSetDepthStencilState(mp.dssNoWrite, 0);
            ctx->OMSetBlendState(mp.bsAlpha, blend_factor, 0xFFFFFFFF);
            ctx->VSSetShader(mp.vs_weather, nullptr, 0);
            ctx->PSSetShader(mp.ps_weather, nullptr, 0);
            ctx->VSSetConstantBuffers(6, 1, &mp.cbuffer_weather);
            ctx->PSSetConstantBuffers(6, 1, &mp.cbuffer_weather);
            ctx->Draw((UINT)(rain_count + snow_count) * 6, 0);
            ID3D11Buffer* null_cb = nullptr;
            ctx->VSSetConstantBuffers(6, 1, &null_cb);
            ctx->PSSetConstantBuffers(6, 1, &null_cb);
            ctx->IASetInputLayout(mp.layout);
            ctx->VSSetShader(mp.vs, nullptr, 0);
            ctx->PSSetShader(mp.ps, nullptr, 0);
            ctx->RSSetState((mp.wireframe && mp.rs_wire) ? mp.rs_wire
                                                         : mp.rs);
        }
    }

    if (mp.fx_show && mp.vs_fx && mp.ps_fx && mp.layout_fx && mp.cbuffer_fx &&
        !mp.fx_system.empty()) {
        double now = ImGui::GetTime();
        float dt = (float)(now - mp.fx_last_time);
        mp.fx_last_time = now;
        if (dt < 0.0f || dt > 0.25f) dt = 1.0f / 60.0f;
        mp.fx_system.update(dt);

        const float cr[3] = { sky_right_f.x, sky_right_f.y, sky_right_f.z };
        const float cu[3] = { sky_up_f.x, sky_up_f.y, sky_up_f.z };
        const float ce[3] = { cam.pos[0], cam.pos[1], cam.pos[2] };
        std::vector<Fx::FxBatch> batches;
        mp.fx_system.build_batches(cr, cu, ce, batches);

        XMMATRIX VP = XMMatrixTranspose(V * P);
        D3D11_MAPPED_SUBRESOURCE fms{};
        if (SUCCEEDED(ctx->Map(mp.cbuffer_fx, 0,
                               D3D11_MAP_WRITE_DISCARD, 0, &fms))) {
            XMFLOAT4X4 m; XMStoreFloat4x4(&m, VP);
            std::memcpy(fms.pData, &m, sizeof(m));
            ctx->Unmap(mp.cbuffer_fx, 0);
        }
        ctx->IASetInputLayout(mp.layout_fx);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->RSSetState(mp.rs);
        ctx->OMSetDepthStencilState(mp.dssNoWrite, 0);
        ctx->VSSetShader(mp.vs_fx, nullptr, 0);
        ctx->PSSetShader(mp.ps_fx, nullptr, 0);
        ctx->VSSetConstantBuffers(0, 1, &mp.cbuffer_fx);
        ID3D11SamplerState* fx_smp = mp.sampler;
        ctx->PSSetSamplers(0, 1, &fx_smp);
        const UINT fstride = sizeof(Fx::FxVertex), foff = 0;
        for (auto& b : batches) {
            if (b.verts.empty()) continue;
            if (mp.fx_vb_capacity < b.verts.size()) {
                if (mp.fx_vb) { mp.fx_vb->Release(); mp.fx_vb = nullptr; }
                size_t cap = b.verts.size() + b.verts.size() / 2 + 2048;
                D3D11_BUFFER_DESC vd{};
                vd.ByteWidth = (UINT)(cap * sizeof(Fx::FxVertex));
                vd.Usage = D3D11_USAGE_DYNAMIC;
                vd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                vd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                if (FAILED(dev->CreateBuffer(&vd, nullptr, &mp.fx_vb))) {
                    mp.fx_vb_capacity = 0; continue;
                }
                mp.fx_vb_capacity = cap;
            }
            D3D11_MAPPED_SUBRESOURCE vms{};
            if (FAILED(ctx->Map(mp.fx_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &vms)))
                continue;
            std::memcpy(vms.pData, b.verts.data(),
                        b.verts.size() * sizeof(Fx::FxVertex));
            ctx->Unmap(mp.fx_vb, 0);
            ctx->IASetVertexBuffers(0, 1, &mp.fx_vb, &fstride, &foff);
            auto it = mp.fx_tex_srv.find(b.texture);
            ID3D11ShaderResourceView* srv =
                (it != mp.fx_tex_srv.end() && it->second) ? it->second
                                                          : mp.default_srv;
            ctx->PSSetShaderResources(0, 1, &srv);

            ID3D11BlendState* fx_bs = nullptr;
            {
                const int key = b.src_blend * 100 + b.dst_blend;
                auto bit = mp.fx_blend_states.find(key);
                if (bit != mp.fx_blend_states.end()) {
                    fx_bs = bit->second;
                } else {
                    D3D11_BLEND_DESC bdx{};
                    bdx.RenderTarget[0].BlendEnable = TRUE;
                    bdx.RenderTarget[0].SrcBlend =
                        (D3D11_BLEND)std::clamp(b.src_blend, 1, 19);
                    bdx.RenderTarget[0].DestBlend =
                        (D3D11_BLEND)std::clamp(b.dst_blend, 1, 19);
                    bdx.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
                    bdx.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
                    bdx.RenderTarget[0].DestBlendAlpha =
                        D3D11_BLEND_INV_SRC_ALPHA;
                    bdx.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
                    bdx.RenderTarget[0].RenderTargetWriteMask =
                        D3D11_COLOR_WRITE_ENABLE_ALL;
                    dev->CreateBlendState(&bdx, &fx_bs);
                    mp.fx_blend_states.emplace(key, fx_bs);
                }
            }
            ctx->OMSetBlendState(fx_bs ? fx_bs : mp.bs_fx_alpha,
                                 blend_factor, 0xFFFFFFFF);
            ctx->Draw((UINT)b.verts.size(), 0);
        }
        ID3D11Buffer* null_cb = nullptr;
        ctx->VSSetConstantBuffers(0, 1, &null_cb);
        ctx->IASetInputLayout(mp.layout);
        ctx->VSSetShader(mp.vs, nullptr, 0);
        ctx->PSSetShader(mp.ps, nullptr, 0);
        ctx->RSSetState((mp.wireframe && mp.rs_wire) ? mp.rs_wire : mp.rs);
    }

    if ((mp.selected_pick_id != 0 || mp.selected_pick_hash != 0) &&
        mp.dssNoWriteLEqual) {
        ctx->VSSetConstantBuffers(0, 1, &mp.cbuffer);
        ctx->PSSetConstantBuffers(0, 1, &mp.cbuffer);
        ctx->OMSetDepthStencilState(mp.dssNoWriteLEqual, 0);
        for (const auto& m : mp.meshes) {
            if (mp_should_hide_mesh(m)) continue;
            if (!m.vb || !m.ib || m.index_count == 0) continue;
            if (m.is_terrain || m.is_water) continue;
            if (any_isolated && !m.isolated) continue;
            if (mp.selected_lod >= 0 &&
                m.lod_index != (uint32_t)mp.selected_lod) continue;
            ID3D11BlendState* bs = m.has_alpha ? mp.bsAlpha : mp.bs;
            for (const auto& pr : m.pick_ranges) {
                const bool match =
                    pr.selection_id == mp.selected_pick_id ||
                    (mp.selected_pick_hash != 0 && pr.inst_hash != 0 &&
                     pr.inst_hash == mp.selected_pick_hash);
                if (!match) continue;
                const LevelEdit::EditXform* ex = nullptr;
                auto it = mp.range_edit_xforms.find(pr.selection_id);
                if (it != mp.range_edit_xforms.end()) ex = &it->second;
                else if (m.edit_xform.active()) ex = &m.edit_xform;
                if (ex && ex->deleted) continue;
                upload_per_mesh_cb(true, false, false, false, ex);
                draw_regular_range(m, pr.index_start, pr.index_count, bs);
            }
        }
    }
    ctx->Release();
}
