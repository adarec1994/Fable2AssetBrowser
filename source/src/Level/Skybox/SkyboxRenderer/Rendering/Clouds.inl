void DrawClouds(ID3D11DeviceContext* context,
                ModelPreview& preview,
                const FlyCam& camera,
                const CameraFrame& camera_frame,
                const FrameState& frame,
                const XMMATRIX& view,
                const XMMATRIX& projection)
{
    static int cloud_debug_frame = 0;
    const bool cloud_debug =
        (cloud_debug_frame < 5) || (cloud_debug_frame % 300 == 0);
    ++cloud_debug_frame;
    if (cloud_debug) {
        std::ostringstream log;
        log << "clouds frame " << cloud_debug_frame
            << ": has_cloud_theme=" << frame.has_cloud_theme
            << " show_sky=" << preview.show_sky
            << " vs_cloud=" << (preview.vs_cloud != nullptr)
            << " ps_cloud=" << (preview.ps_cloud != nullptr)
            << " layer_count=" << preview.cloud_layer_count
            << " density_srv=["
            << (preview.cloud_density_srv[0] != nullptr) << ','
            << (preview.cloud_density_srv[1] != nullptr) << ','
            << (preview.cloud_density_srv[2] != nullptr) << ','
            << (preview.cloud_density_srv[3] != nullptr) << ']';
        SkyDomeDebug(log.str());
    }
    if (!frame.has_cloud_theme || !preview.show_sky ||
        !preview.vs_cloud || !preview.ps_cloud || !preview.layout_cloud ||
        !preview.cbuffer_cloud || !preview.cloud_vb || !preview.cloud_ib ||
        !preview.sampler_cloud) {
        return;
    }

    if (!preview.cloud_runtime_initialised) {
        for (std::size_t i = 0; i < preview.cloud_runtime.size(); ++i) {
            CloudRuntime::InitialiseLayer(preview.cloud_runtime[i]);
            preview.cloud_density_binding[i] = {};
        }
        preview.cloud_runtime_initialised = true;
    }

    for (int i = 0; i < 4; ++i) {
        CloudRuntime::LayerThemeValues theme{};
        theme.transparency = frame.cloud_layer[i][0];
        theme.height = frame.cloud_layer[i][1];
        theme.texture_scale_x = frame.cloud_layer[i][2];
        theme.texture_scale_y = frame.cloud_layer[i][3];
        theme.size_x = frame.cloud_shape[i][0];
        theme.size_y = frame.cloud_shape[i][1];
        theme.position_x = frame.cloud_motion[i][0];
        theme.position_y = frame.cloud_motion[i][1];
        theme.velocity_x = frame.cloud_motion[i][2];
        theme.velocity_y = frame.cloud_motion[i][3];
        theme.brightness = frame.cloud_light[i][0];
        theme.ambient_light = frame.cloud_light[i][1];
        theme.normal_strength = frame.cloud_light[i][2];
        theme.translucency_strength = frame.cloud_light[i][3];
        theme.density_token = frame.cloud_density_token[i];

        const CloudRuntime::LayerConfig config =
            CloudRuntime::BuildConfig(theme);
        const CloudRuntime::DensityResourceBinding resolved_resource =
            preview.cloud_density_srv[i]
                ? CloudRuntime::DensityResourceBinding{
                      theme.density_token, theme.density_token}
                : CloudRuntime::DensityResourceBinding{};
        CloudRuntime::UpdateLayer(
            preview.cloud_runtime[i], config,
            frame.elapsed_time,
            preview.cloud_density_binding[i], resolved_resource);
    }

    const CloudRuntime::ActiveOrder order =
        CloudRuntime::BuildActiveOrder(preview.cloud_runtime);
    if (order.count == 0) return;

    struct CloudConstants {
        XMFLOAT4X4 view_projection;
        XMFLOAT4 viewer_position;
        XMFLOAT4 viewer_direction;
        XMFLOAT4 light_position;
        XMFLOAT4 light_colour;
        XMFLOAT4 layer_params;
        XMFLOAT4 uv_scale_offset;
        XMFLOAT4 globals;
    } constants{};
    static_assert(sizeof(CloudConstants) == 11 * 16);

    const XMMATRIX view_projection = XMMatrixTranspose(view * projection);
    XMStoreFloat4x4(&constants.view_projection, view_projection);
    constants.viewer_position = XMFLOAT4(
        camera.pos[0], camera.pos[1], camera.pos[2], 1.0f);
    constants.viewer_direction = XMFLOAT4(
        camera_frame.view_forward[0], camera_frame.view_forward[1],
        camera_frame.view_forward[2], 0.0f);
    constants.light_position = XMFLOAT4(
        camera.pos[0] - frame.sun_direction[0] * 2000.0f,
        camera.pos[1] - frame.sun_direction[1] * 2000.0f,
        camera.pos[2] - frame.sun_direction[2] * 2000.0f,
        1.0f);
    constants.light_colour = XMFLOAT4(
        frame.main_light_colour[0],
        frame.main_light_colour[1],
        frame.main_light_colour[2], 1.0f);

    const UINT vertex_stride = sizeof(CloudRuntime::Vertex);
    const UINT vertex_offset = 0;
    float blend_factor[4] = {0, 0, 0, 0};
    context->IASetInputLayout(preview.layout_cloud);
    context->IASetVertexBuffers(
        0, 1, &preview.cloud_vb, &vertex_stride, &vertex_offset);
    context->IASetIndexBuffer(
        preview.cloud_ib, DXGI_FORMAT_R16_UINT, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->RSSetState(preview.rs);

    context->OMSetDepthStencilState(preview.dssWrite, 0);
    context->OMSetBlendState(preview.bsAlpha, blend_factor, 0xFFFFFFFF);
    context->VSSetShader(preview.vs_cloud, nullptr, 0);
    context->PSSetShader(preview.ps_cloud, nullptr, 0);
    context->VSSetConstantBuffers(5, 1, &preview.cbuffer_cloud);
    context->PSSetConstantBuffers(5, 1, &preview.cbuffer_cloud);
    context->PSSetSamplers(0, 1, &preview.sampler_cloud);

    for (std::size_t sorted = 0; sorted < order.count; ++sorted) {
        const int layer_index = order.indices[sorted];
        const CloudRuntime::LayerRuntimeXex& layer =
            preview.cloud_runtime[layer_index];
        const auto vertices = CloudRuntime::BuildVertices(layer.config);

        D3D11_MAPPED_SUBRESOURCE vertex_map{};
        if (FAILED(context->Map(
                preview.cloud_vb, 0, D3D11_MAP_WRITE_DISCARD,
                0, &vertex_map))) {
            continue;
        }
        std::memcpy(vertex_map.pData, vertices.data(), sizeof(vertices));
        context->Unmap(preview.cloud_vb, 0);

        constants.layer_params = XMFLOAT4(
            layer.config.transparency,
            layer.config.ambient_light,
            layer.config.brightness,
            CloudRuntime::ShaderNormalStrength(
                layer.config.normal_strength));
        constants.uv_scale_offset = XMFLOAT4(
            layer.config.texture_scale_x,
            layer.config.texture_scale_y,
            layer.uv_x, layer.uv_y);

        constants.globals = XMFLOAT4(
            1.0f, 0.0f,
            CloudRuntime::AlphaTestThreshold(layer.config, true),
            0.0f);

        D3D11_MAPPED_SUBRESOURCE constant_map{};
        if (FAILED(context->Map(
                preview.cbuffer_cloud, 0, D3D11_MAP_WRITE_DISCARD,
                0, &constant_map))) {
            continue;
        }
        std::memcpy(
            constant_map.pData, &constants, sizeof(constants));
        context->Unmap(preview.cbuffer_cloud, 0);

        ID3D11ShaderResourceView* density =
            preview.cloud_density_srv[layer_index];
        context->PSSetShaderResources(8, 1, &density);
        context->DrawIndexed(6, 0, 0);
    }

    ID3D11ShaderResourceView* null_density = nullptr;
    context->PSSetShaderResources(8, 1, &null_density);
    ID3D11Buffer* null_buffer = nullptr;
    context->VSSetConstantBuffers(5, 1, &null_buffer);
    context->PSSetConstantBuffers(5, 1, &null_buffer);

    context->IASetInputLayout(preview.layout);
    context->VSSetShader(preview.vs, nullptr, 0);
    context->PSSetShader(preview.ps, nullptr, 0);
    ID3D11SamplerState* samplers[2] = {
        preview.sampler, preview.sampler_point,
    };
    context->PSSetSamplers(0, 2, samplers);
    context->RSSetState(
        (preview.wireframe && preview.rs_wire)
            ? preview.rs_wire
            : preview.rs);
}
