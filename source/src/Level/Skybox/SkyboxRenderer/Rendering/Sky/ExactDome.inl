    if (exact_dome_ready) {

        SkyDomeXex::ThemeInputs theme{};
        theme.sun_intensity = frame.sky_params[0];
        theme.complementary_bias = frame.sky_params[1];
        theme.beta_rayleigh_multiplier = frame.sky_params[2];
        theme.beta_mie_multiplier = frame.sky_params[3];
        for (int i = 0; i < 3; ++i) {
            theme.sky_colour[i] = frame.sky_top[i];
            theme.complementary_colour[i] = frame.sky_bottom[i];
            theme.sunset_colour[i] = frame.sky_sunset[i];
        }
        theme.fogging_start = frame.fog_range[0];
        theme.close_fog_max_distance = frame.fog_range[3];

        theme.sun_direction[0] = frame.sun_direction[0];
        theme.sun_direction[1] = frame.sun_direction[2];
        theme.sun_direction[2] = frame.sun_direction[1];
        theme.sun_direction[3] = 0.0f;

        const SkyDomeXex::AtmosphereState state =
            SkyDomeXex::ComputeAtmosphereState(theme);

        std::array<std::uint16_t, SkyDomeXex::kLutWidth * 4> lut{};
        SkyDomeXex::BuildInScatterLut(state, lut);
        D3D11_MAPPED_SUBRESOURCE lut_mapped{};
        if (SUCCEEDED(context->Map(preview.sky_lut_tex, 0,
                                   D3D11_MAP_WRITE_DISCARD, 0,
                                   &lut_mapped))) {
            std::memcpy(lut_mapped.pData, lut.data(),
                        lut.size() * sizeof(lut[0]));
            context->Unmap(preview.sky_lut_tex, 0);
        }

        SkyDomeXex::DomeConstantBuffer constants{};
        std::memcpy(constants.beta_rayleigh, state.beta_rayleigh,
                    sizeof(constants.beta_rayleigh));
        std::memcpy(constants.beta_mie, state.beta_mie,
                    sizeof(constants.beta_mie));
        std::memcpy(constants.sun_direction, state.sun_direction,
                    sizeof(constants.sun_direction));
        std::memcpy(constants.scattering_misc, state.scattering_misc,
                    sizeof(constants.scattering_misc));

        constants.overlay_blend[0] = 0.0f;

        constants.dome_misc[2] = 10.0f;
        auto to_engine = [](const float v[3], float w, float out[4]) {
            out[0] = v[0];
            out[1] = v[2];
            out[2] = v[1];
            out[3] = w;
        };
        to_engine(camera.right, camera.tan_half_x, constants.camera_right);
        to_engine(camera.up, camera.tan_half_y, constants.camera_up);
        to_engine(camera.forward, 0.0f, constants.camera_forward);

        D3D11_MAPPED_SUBRESOURCE cb_mapped{};
        if (SUCCEEDED(context->Map(preview.cbuffer_sky_dome, 0,
                                   D3D11_MAP_WRITE_DISCARD, 0,
                                   &cb_mapped))) {
            std::memcpy(cb_mapped.pData, &constants, sizeof(constants));
            context->Unmap(preview.cbuffer_sky_dome, 0);
        }

        if (debug_now) {
            std::ostringstream log;
            log << "  theme: sun_int=" << theme.sun_intensity
                << " bias=" << theme.complementary_bias
                << " rayM=" << theme.beta_rayleigh_multiplier
                << " mieM=" << theme.beta_mie_multiplier
                << " fogstart=" << theme.fogging_start
                << "\n  sky=(" << theme.sky_colour[0] << ','
                << theme.sky_colour[1] << ',' << theme.sky_colour[2]
                << ") comp=(" << theme.complementary_colour[0] << ','
                << theme.complementary_colour[1] << ','
                << theme.complementary_colour[2]
                << ") sun_engine=(" << theme.sun_direction[0] << ','
                << theme.sun_direction[1] << ',' << theme.sun_direction[2]
                << ")\n  betaR=(" << state.beta_rayleigh[0] << ','
                << state.beta_rayleigh[1] << ',' << state.beta_rayleigh[2]
                << ") betaM=(" << state.beta_mie[0] << ','
                << state.beta_mie[1] << ',' << state.beta_mie[2]
                << ") hg=(" << state.lut_hg[0] << ',' << state.lut_hg[1]
                << ',' << state.lut_hg[2] << ") cosElev="
                << state.lut_cos_sun_elevation;
            std::array<float, SkyDomeXex::kLutWidth * 4> lut_debug{};
            SkyDomeXex::BuildInScatterLutFloat(state, lut_debug);
            log << "\n  lut[0]=(" << lut_debug[0] << ',' << lut_debug[1]
                << ',' << lut_debug[2] << ") lut[63]=("
                << lut_debug[63 * 4 + 0] << ',' << lut_debug[63 * 4 + 1]
                << ',' << lut_debug[63 * 4 + 2] << ')'
                << "\n  cam fwd_engine=(" << constants.camera_forward[0]
                << ',' << constants.camera_forward[1] << ','
                << constants.camera_forward[2] << ") tan=("
                << constants.camera_right[3] << ','
                << constants.camera_up[3] << ") exposure="
                << constants.dome_misc[2]
                << " overlay_srv=" << (preview.sky_overlay_srv != nullptr)
                << " -> draw";
            SkyDomeDebug(log.str());
        }

        float blend_factor[4] = {0, 0, 0, 0};
        context->RSSetState(preview.rs);
        context->OMSetDepthStencilState(preview.dssNoWrite, 0);
        context->OMSetBlendState(preview.bs, blend_factor, 0xFFFFFFFF);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(preview.vs_sky_dome, nullptr, 0);
        context->PSSetShader(preview.ps_sky_dome, nullptr, 0);
        context->VSSetConstantBuffers(6, 1, &preview.cbuffer_sky_dome);
        context->PSSetConstantBuffers(6, 1, &preview.cbuffer_sky_dome);
        ID3D11ShaderResourceView* lut_srv = preview.sky_lut_srv;
        context->PSSetShaderResources(4, 1, &lut_srv);
        ID3D11ShaderResourceView* overlay_srv = preview.sky_overlay_srv;
        context->PSSetShaderResources(13, 1, &overlay_srv);
        ID3D11SamplerState* samplers[2] = {
            preview.sampler_cloud ? preview.sampler_cloud
                                  : preview.sampler,
            preview.sampler_sky_clamp,
        };
        context->PSSetSamplers(0, 2, samplers);
        context->Draw(3, 0);

        ID3D11Buffer* null_buffer = nullptr;
        context->VSSetConstantBuffers(6, 1, &null_buffer);
        context->PSSetConstantBuffers(6, 1, &null_buffer);
        ID3D11ShaderResourceView* null_srv = nullptr;
        context->PSSetShaderResources(4, 1, &null_srv);
        context->PSSetShaderResources(13, 1, &null_srv);

        if (preview.vs_sky_element && preview.ps_sky_element &&
            preview.layout_sky_element && preview.sky_element_vb &&
            preview.cbuffer_sky_element) {
            struct ElementVertex {
                float position[4];
                float uv[2];
            };
            auto dot3 = [](const float* a, const float* b) {
                return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
            };
            auto draw_billboard = [&](const float direction[3],
                                      float distance,
                                      float half_width,
                                      float half_height,
                                      ID3D11ShaderResourceView* texture,
                                      const float colour[4],
                                      bool additive,
                                      float u0, float u1) {
                if (!texture) return;
                const float centre[3] = {direction[0] * distance,
                                         direction[1] * distance,
                                         direction[2] * distance};
                if (dot3(centre, camera.forward) <= 0.0f) return;

                ElementVertex quad[4]{};
                const float corner_x[4] = {-1.0f, 1.0f, -1.0f, 1.0f};
                const float corner_y[4] = {1.0f, 1.0f, -1.0f, -1.0f};
                for (int corner = 0; corner < 4; ++corner) {
                    float world[3];
                    for (int axis = 0; axis < 3; ++axis) {
                        world[axis] = centre[axis] +
                            camera.right[axis] * corner_x[corner] *
                                half_width +
                            camera.up[axis] * corner_y[corner] *
                                half_height;
                    }
                    float depth = dot3(world, camera.forward);
                    if (depth < 0.01f) depth = 0.01f;
                    quad[corner].position[0] =
                        dot3(world, camera.right) /
                        (depth * camera.tan_half_x);
                    quad[corner].position[1] =
                        dot3(world, camera.up) /
                        (depth * camera.tan_half_y);
                    quad[corner].position[2] = 0.9985f;
                    quad[corner].position[3] = 1.0f;
                    quad[corner].uv[0] =
                        corner_x[corner] > 0.0f ? u1 : u0;
                    quad[corner].uv[1] =
                        corner_y[corner] > 0.0f ? 0.0f : 1.0f;
                }
                const ElementVertex vertices[6] = {
                    quad[0], quad[1], quad[2],
                    quad[2], quad[1], quad[3]};

                D3D11_MAPPED_SUBRESOURCE mapped_vb{};
                if (FAILED(context->Map(preview.sky_element_vb, 0,
                                        D3D11_MAP_WRITE_DISCARD, 0,
                                        &mapped_vb))) {
                    return;
                }
                std::memcpy(mapped_vb.pData, vertices, sizeof(vertices));
                context->Unmap(preview.sky_element_vb, 0);

                D3D11_MAPPED_SUBRESOURCE mapped_cb{};
                if (FAILED(context->Map(preview.cbuffer_sky_element, 0,
                                        D3D11_MAP_WRITE_DISCARD, 0,
                                        &mapped_cb))) {
                    return;
                }
                std::memcpy(mapped_cb.pData, colour, 16);
                context->Unmap(preview.cbuffer_sky_element, 0);

                context->OMSetBlendState(
                    additive && preview.bs_fx_add ? preview.bs_fx_add
                                                  : preview.bsAlpha,
                    blend_factor, 0xFFFFFFFF);
                const UINT stride = sizeof(ElementVertex);
                const UINT offset = 0;
                context->IASetInputLayout(preview.layout_sky_element);
                context->IASetVertexBuffers(
                    0, 1, &preview.sky_element_vb, &stride, &offset);
                context->VSSetShader(preview.vs_sky_element, nullptr, 0);
                context->PSSetShader(preview.ps_sky_element, nullptr, 0);
                context->PSSetConstantBuffers(
                    6, 1, &preview.cbuffer_sky_element);
                context->PSSetShaderResources(0, 1, &texture);
                ID3D11SamplerState* element_sampler =
                    preview.sampler_sky_clamp ? preview.sampler_sky_clamp
                                              : preview.sampler;
                context->PSSetSamplers(0, 1, &element_sampler);
                context->Draw(6, 0);
            };

            const float* element = frame.element_params;
            const float exposure = constants.dome_misc[2];
            const float gate = 0.000099999997f;

            auto tonemap_colour = [](float c[4]) {
                for (int i = 0; i < 3; ++i) c[i] = c[i] / (1.0f + c[i]);
            };

            const float* sun_tint = state.sun_element_colour;

            if (frame.sky_params[0] >= gate) {
                if (element[13] >= gate) {
                    float colour[4] = {
                        sun_tint[0] * element[13] * exposure,
                        sun_tint[1] * element[13] * exposure,
                        sun_tint[2] * element[13] * exposure,
                        1.0f};
                    tonemap_colour(colour);
                    draw_billboard(frame.sun_direction,
                                   SkyXex::kSunBeamsDistance,
                                   element[11] * SkyXex::kSunBeamsSizeScale,
                                   element[12] * SkyXex::kSunBeamsSizeScale,
                                   preview.sky_sun_beams_srv, colour,
                                   true, 0.0f, 1.0f);
                }
                {
                    float colour[4] = {
                        element[8] * element[7] * exposure,
                        element[9] * element[7] * exposure,
                        element[10] * element[7] * exposure,
                        1.0f};
                    tonemap_colour(colour);
                    draw_billboard(frame.sun_direction,
                                   SkyXex::kSunDiscDistance,
                                   element[6] * SkyXex::kSunDiscSizeScale,
                                   element[6] * SkyXex::kSunDiscSizeScale,
                                   preview.sky_sun_disc_srv, colour,
                                   false, 0.0f, 1.0f);
                }
                const float facing = dot3(frame.sun_direction,
                                          camera.forward);
                if (element[14] >= gate && facing > 0.0f) {

                    const float scale =
                        element[14] * facing * facing * exposure;
                    float colour[4] = {
                        sun_tint[0] * scale,
                        sun_tint[1] * scale,
                        sun_tint[2] * scale, 1.0f};
                    tonemap_colour(colour);
                    draw_billboard(frame.sun_direction,
                                   SkyXex::kSunGlareDistance,
                                   element[15] * SkyXex::kSunGlareSizeScale,
                                   element[15] * SkyXex::kSunGlareSizeScale,
                                   preview.sky_sun_glare_srv, colour,
                                   true, 0.0f, 1.0f);
                }
            }

            if (element[0] >= gate) {
                const int phase =
                    std::clamp(preview.moon_phase, 0, 7);
                const float u0 =
                    static_cast<float>(phase) * SkyXex::kMoonPhaseUStep;
                const float u1 = static_cast<float>(phase + 1) *
                                 SkyXex::kMoonPhaseUStep;
                {

                    float colour[4] = {
                        0.5f * element[0] * exposure,
                        0.5f * element[0] * exposure,
                        element[4] * exposure,
                        element[4]};
                    tonemap_colour(colour);
                    draw_billboard(frame.moon_direction,
                                   SkyXex::kMoonBillboardDistance,
                                   element[1] * SkyXex::kMoonSizeScale,
                                   element[1] * SkyXex::kMoonSizeScale,
                                   preview.sky_moon_srv, colour,
                                   false, u0, u1);
                }
                if (element[2] >= gate) {
                    const float scale = element[2] * exposure;
                    float colour[4] = {scale, scale, scale, 1.0f};
                    tonemap_colour(colour);
                    draw_billboard(frame.moon_direction,
                                   SkyXex::kMoonGlareDistance,
                                   element[3] * SkyXex::kMoonGlareSizeScale,
                                   element[3] * SkyXex::kMoonGlareSizeScale,
                                   preview.sky_moon_glare_srv, colour,
                                   true, 0.0f, 1.0f);
                }
            }

            if (element[5] >= gate && preview.vs_sky_stars &&
                preview.ps_sky_stars) {
                const float star_constants[4] = {
                    static_cast<float>(frame.elapsed_time),
                    element[5],

                    SkyDomeXex::kStarPointSize /
                        static_cast<float>(std::max(preview.width, 1)),
                    SkyDomeXex::kStarPointSize /
                        static_cast<float>(std::max(preview.height, 1))};
                D3D11_MAPPED_SUBRESOURCE star_cb{};
                if (SUCCEEDED(context->Map(preview.cbuffer_sky_element, 0,
                                           D3D11_MAP_WRITE_DISCARD, 0,
                                           &star_cb))) {
                    std::memcpy(star_cb.pData, star_constants, 16);
                    context->Unmap(preview.cbuffer_sky_element, 0);
                    context->OMSetBlendState(
                        preview.bs_fx_add ? preview.bs_fx_add
                                          : preview.bsAlpha,
                        blend_factor, 0xFFFFFFFF);
                    context->IASetInputLayout(nullptr);
                    context->VSSetShader(preview.vs_sky_stars, nullptr, 0);
                    context->PSSetShader(preview.ps_sky_stars, nullptr, 0);
                    context->VSSetConstantBuffers(
                        6, 1, &preview.cbuffer_sky_dome);
                    context->VSSetConstantBuffers(
                        7, 1, &preview.cbuffer_sky_element);
                    context->Draw(SkyXex::kStarCount * 6, 0);
                    context->VSSetConstantBuffers(7, 1, &null_buffer);
                }
            }

            context->OMSetBlendState(preview.bs, blend_factor, 0xFFFFFFFF);
            context->VSSetConstantBuffers(6, 1, &null_buffer);
            context->PSSetConstantBuffers(6, 1, &null_buffer);
            context->PSSetShaderResources(0, 1, &null_srv);
            if (debug_now) {
                std::ostringstream log;
                log << "  elements: beams_srv="
                    << (preview.sky_sun_beams_srv != nullptr)
                    << " disc_srv="
                    << (preview.sky_sun_disc_srv != nullptr)
                    << " glare_srv="
                    << (preview.sky_sun_glare_srv != nullptr)
                    << " moon_srv=" << (preview.sky_moon_srv != nullptr)
                    << " moonglare_srv="
                    << (preview.sky_moon_glare_srv != nullptr)
                    << " ep=[";
                for (int i = 0; i < 16; ++i) {
                    log << element[i] << (i == 15 ? "]" : ",");
                }
                log << " moon_dir=(" << frame.moon_direction[0] << ','
                    << frame.moon_direction[1] << ','
                    << frame.moon_direction[2] << ')';
                SkyDomeDebug(log.str());
            }
        }
        return;
    }
