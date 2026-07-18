    struct SkyConstants {
        XMFLOAT4 top;
        XMFLOAT4 bottom;
        XMFLOAT4 sunset;
        XMFLOAT4 params;
        XMFLOAT4 cloud_layer[4];
        XMFLOAT4 cloud_shape[4];
        XMFLOAT4 cloud_motion[4];
        XMFLOAT4 cloud_light[4];
        XMFLOAT4 cloud_global;
        XMFLOAT4 cloud_density_flags;
        XMFLOAT4 sky_right;
        XMFLOAT4 sky_up;
        XMFLOAT4 sky_forward;
        XMFLOAT4 sky_sun;
        XMFLOAT4 sky_texture_flags;
    } constants{};
    auto finite_clamped = [](float value, float fallback) {
        return std::isfinite(value)
            ? std::clamp(value, 0.0f, 4.0f)
            : fallback;
    };
    constants.top = XMFLOAT4(
        finite_clamped(frame.sky_top[0], 0.42f),
        finite_clamped(frame.sky_top[1], 0.56f),
        finite_clamped(frame.sky_top[2], 0.76f), 1.0f);
    constants.bottom = XMFLOAT4(
        finite_clamped(frame.sky_bottom[0], 0.55f),
        finite_clamped(frame.sky_bottom[1], 0.60f),
        finite_clamped(frame.sky_bottom[2], 0.65f), 1.0f);
    constants.sunset = XMFLOAT4(
        finite_clamped(frame.sky_sunset[0], 1.0f),
        finite_clamped(frame.sky_sunset[1], 0.47f),
        finite_clamped(frame.sky_sunset[2], 0.22f), 1.0f);
    constants.params = XMFLOAT4(
        std::max(frame.sky_params[0], 0.0f),
        std::clamp(frame.sky_params[1], 0.0f, 1.0f),
        std::max(frame.sky_params[2], 0.05f),
        std::max(frame.sky_params[3], 0.05f));
    constants.cloud_global = XMFLOAT4(
        0.0f, 0.0f, 0.0f,
        std::max(preview.sky_moon_tiles[0], 1.0f));
    constants.sky_right = XMFLOAT4(
        camera.right[0], camera.right[1], camera.right[2],
        camera.tan_half_x);
    constants.sky_up = XMFLOAT4(
        camera.up[0], camera.up[1], camera.up[2], camera.tan_half_y);
    constants.sky_forward = XMFLOAT4(
        camera.forward[0], camera.forward[1], camera.forward[2], 0.0f);
    constants.sky_sun = XMFLOAT4(
        frame.sun_direction[0], frame.sun_direction[1],
        frame.sun_direction[2], 0.0f);
    constants.sky_texture_flags = XMFLOAT4(
        preview.sky_moon_srv ? 1.0f : 0.0f,
        preview.sky_moon_glare_srv ? 1.0f : 0.0f,
        preview.sky_sun_disc_srv ? 1.0f : 0.0f,
        std::max(preview.sky_moon_tiles[1], 1.0f));

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(
            preview.cbuffer_sky, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &constants, sizeof(constants));
        context->Unmap(preview.cbuffer_sky, 0);
    }
    float blend_factor[4] = {0, 0, 0, 0};
    context->RSSetState(preview.rs);
    context->OMSetDepthStencilState(preview.dssNoWrite, 0);
    context->OMSetBlendState(preview.bs, blend_factor, 0xFFFFFFFF);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(preview.vs_sky, nullptr, 0);
    context->PSSetShader(preview.ps_sky, nullptr, 0);
    context->VSSetConstantBuffers(4, 1, &preview.cbuffer_sky);
    context->PSSetConstantBuffers(4, 1, &preview.cbuffer_sky);
    ID3D11ShaderResourceView* textures[3] = {
        preview.sky_moon_srv,
        preview.sky_moon_glare_srv,
        preview.sky_sun_disc_srv,
    };
    context->PSSetShaderResources(12, 3, textures);
    ID3D11SamplerState* sampler = preview.sampler;
    context->PSSetSamplers(0, 1, &sampler);
    context->Draw(3, 0);
    ID3D11Buffer* null_buffer = nullptr;
    context->VSSetConstantBuffers(4, 1, &null_buffer);
    context->PSSetConstantBuffers(4, 1, &null_buffer);
    ID3D11ShaderResourceView* null_textures[3] = {
        nullptr, nullptr, nullptr,
    };
    context->PSSetShaderResources(12, 3, null_textures);
}
