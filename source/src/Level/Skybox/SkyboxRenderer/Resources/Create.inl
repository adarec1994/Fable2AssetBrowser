void CreateD3D11Resources(ID3D11Device* device, ModelPreview& preview)
{
    {
        ID3DBlob* vertex_blob = nullptr;
        ID3DBlob* pixel_blob = nullptr;
#if FABLE_ENABLE_RECONSTRUCTED_SKY_PREVIEW
        if (CompileShader(kSkyVertexShader, "VS", "vs_5_0", &vertex_blob) &&
            CompileShader(kSkyPixelShader, "PS", "ps_5_0", &pixel_blob)) {
            device->CreateVertexShader(
                vertex_blob->GetBufferPointer(),
                vertex_blob->GetBufferSize(), nullptr, &preview.vs_sky);
            device->CreatePixelShader(
                pixel_blob->GetBufferPointer(),
                pixel_blob->GetBufferSize(), nullptr, &preview.ps_sky);
        }
#else
        OutputLog::info(
            "sky preview disabled: exact Xenos execution required");
#endif
        if (vertex_blob) vertex_blob->Release();
        if (pixel_blob) pixel_blob->Release();

        D3D11_BUFFER_DESC constant_buffer{};
        constant_buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constant_buffer.ByteWidth = 27 * 16;
        constant_buffer.Usage = D3D11_USAGE_DYNAMIC;
        constant_buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(
            &constant_buffer, nullptr, &preview.cbuffer_sky);
    }

    {

        ID3DBlob* vertex_blob = nullptr;
        ID3DBlob* pixel_blob = nullptr;
        if (CompileShader(SkyDomeXex::kDomeFullscreenVertexShaderHlsl,
                          "VSMainFullscreen", "vs_5_0", &vertex_blob) &&
            CompileShader(SkyDomeXex::kDomePixelShaderHlsl,
                          "PSMain", "ps_5_0", &pixel_blob)) {
            device->CreateVertexShader(
                vertex_blob->GetBufferPointer(),
                vertex_blob->GetBufferSize(), nullptr,
                &preview.vs_sky_dome);
            device->CreatePixelShader(
                pixel_blob->GetBufferPointer(),
                pixel_blob->GetBufferSize(), nullptr,
                &preview.ps_sky_dome);
            OutputLog::info("sky dome: retail xenos translation active");
        }
        if (vertex_blob) vertex_blob->Release();
        if (pixel_blob) pixel_blob->Release();

        D3D11_BUFFER_DESC constant_buffer{};
        constant_buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constant_buffer.ByteWidth =
            static_cast<UINT>(sizeof(SkyDomeXex::DomeConstantBuffer));
        constant_buffer.Usage = D3D11_USAGE_DYNAMIC;
        constant_buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(
            &constant_buffer, nullptr, &preview.cbuffer_sky_dome);

        D3D11_TEXTURE2D_DESC lut_desc{};
        lut_desc.Width = SkyDomeXex::kLutWidth;
        lut_desc.Height = 1;
        lut_desc.MipLevels = 1;
        lut_desc.ArraySize = 1;
        lut_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        lut_desc.SampleDesc.Count = 1;
        lut_desc.Usage = D3D11_USAGE_DYNAMIC;
        lut_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        lut_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (SUCCEEDED(device->CreateTexture2D(
                &lut_desc, nullptr, &preview.sky_lut_tex)) &&
            preview.sky_lut_tex) {
            device->CreateShaderResourceView(
                preview.sky_lut_tex, nullptr, &preview.sky_lut_srv);
        }

        D3D11_SAMPLER_DESC clamp_sampler{};
        clamp_sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        clamp_sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        clamp_sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        clamp_sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        clamp_sampler.MaxLOD = D3D11_FLOAT32_MAX;
        device->CreateSamplerState(&clamp_sampler,
                                   &preview.sampler_sky_clamp);
    }

    {

        ID3DBlob* vertex_blob = nullptr;
        ID3DBlob* pixel_blob = nullptr;
        if (CompileShader(kSkyElementVertexShader, "VS", "vs_5_0",
                          &vertex_blob) &&
            CompileShader(kSkyElementPixelShader, "PS", "ps_5_0",
                          &pixel_blob)) {
            device->CreateVertexShader(
                vertex_blob->GetBufferPointer(),
                vertex_blob->GetBufferSize(), nullptr,
                &preview.vs_sky_element);
            device->CreatePixelShader(
                pixel_blob->GetBufferPointer(),
                pixel_blob->GetBufferSize(), nullptr,
                &preview.ps_sky_element);
            D3D11_INPUT_ELEMENT_DESC element_layout[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,
                 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
                 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };
            device->CreateInputLayout(
                element_layout, 2,
                vertex_blob->GetBufferPointer(),
                vertex_blob->GetBufferSize(),
                &preview.layout_sky_element);
        }
        if (vertex_blob) vertex_blob->Release();
        if (pixel_blob) pixel_blob->Release();

        D3D11_BUFFER_DESC constant_buffer{};
        constant_buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constant_buffer.ByteWidth = 16;
        constant_buffer.Usage = D3D11_USAGE_DYNAMIC;
        constant_buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(
            &constant_buffer, nullptr, &preview.cbuffer_sky_element);

        D3D11_BUFFER_DESC vertex_buffer{};
        vertex_buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vertex_buffer.ByteWidth = 6 * 24;
        vertex_buffer.Usage = D3D11_USAGE_DYNAMIC;
        vertex_buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(
            &vertex_buffer, nullptr, &preview.sky_element_vb);

        ID3DBlob* stars_vs_blob = nullptr;
        ID3DBlob* stars_ps_blob = nullptr;
        if (CompileShader(SkyDomeXex::kStarsVertexShaderHlsl, "VSMain",
                          "vs_5_0", &stars_vs_blob) &&
            CompileShader(SkyDomeXex::kStarsPixelShaderHlsl, "PSMain",
                          "ps_5_0", &stars_ps_blob)) {
            device->CreateVertexShader(
                stars_vs_blob->GetBufferPointer(),
                stars_vs_blob->GetBufferSize(), nullptr,
                &preview.vs_sky_stars);
            device->CreatePixelShader(
                stars_ps_blob->GetBufferPointer(),
                stars_ps_blob->GetBufferSize(), nullptr,
                &preview.ps_sky_stars);
        }
        if (stars_vs_blob) stars_vs_blob->Release();
        if (stars_ps_blob) stars_ps_blob->Release();

    }

    {
        ID3DBlob* vertex_blob = nullptr;
        ID3DBlob* pixel_blob = nullptr;
#if FABLE_ENABLE_RECONSTRUCTED_CLOUD_PREVIEW
        if (CompileShader(
                kCloudVertexShader, "VS", "vs_5_0", &vertex_blob) &&
            CompileShader(
                kCloudPixelShader, "PS", "ps_5_0", &pixel_blob)) {
            device->CreateVertexShader(
                vertex_blob->GetBufferPointer(),
                vertex_blob->GetBufferSize(), nullptr, &preview.vs_cloud);
            device->CreatePixelShader(
                pixel_blob->GetBufferPointer(),
                pixel_blob->GetBufferSize(), nullptr, &preview.ps_cloud);
            D3D11_INPUT_ELEMENT_DESC cloud_layout[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
                 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
                 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };
            device->CreateInputLayout(
                cloud_layout, 2,
                vertex_blob->GetBufferPointer(),
                vertex_blob->GetBufferSize(),
                &preview.layout_cloud);
        }
#else
        OutputLog::info(
            "cloud preview disabled: exact Xenos execution required");
#endif
        if (vertex_blob) vertex_blob->Release();
        if (pixel_blob) pixel_blob->Release();

        D3D11_BUFFER_DESC constant_buffer{};
        constant_buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constant_buffer.ByteWidth = 11 * 16;
        constant_buffer.Usage = D3D11_USAGE_DYNAMIC;
        constant_buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(
            &constant_buffer, nullptr, &preview.cbuffer_cloud);

        D3D11_BUFFER_DESC vertex_buffer{};
        vertex_buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vertex_buffer.ByteWidth =
            4 * static_cast<UINT>(sizeof(CloudRuntime::Vertex));
        vertex_buffer.Usage = D3D11_USAGE_DYNAMIC;
        vertex_buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(
            &vertex_buffer, nullptr, &preview.cloud_vb);

        const auto& indices = CloudRuntime::Indices();
        D3D11_SUBRESOURCE_DATA index_data{};
        index_data.pSysMem = indices.data();
        D3D11_BUFFER_DESC index_buffer{};
        index_buffer.BindFlags = D3D11_BIND_INDEX_BUFFER;
        index_buffer.ByteWidth = static_cast<UINT>(
            indices.size() * sizeof(indices[0]));
        index_buffer.Usage = D3D11_USAGE_IMMUTABLE;
        device->CreateBuffer(
            &index_buffer, &index_data, &preview.cloud_ib);

        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        device->CreateSamplerState(&sampler, &preview.sampler_cloud);
    }
}
