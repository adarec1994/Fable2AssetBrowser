struct AuthoredRgbaMip {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

ID3D11ShaderResourceView* CreateSrvFromAuthoredMips(
    ID3D11Device* device,
    const std::vector<AuthoredRgbaMip>& mips)
{
    if (!device || mips.empty() || mips.size() > 15) return nullptr;
    for (std::size_t i = 0; i < mips.size(); ++i) {
        const AuthoredRgbaMip& mip = mips[i];
        if (mip.width <= 0 || mip.height <= 0 ||
            mip.width > 8192 || mip.height > 8192 ||
            mip.pixels.size() <
                std::size_t(mip.width) * std::size_t(mip.height) * 4) {
            return nullptr;
        }
        if (i != 0 &&
            (mip.width != std::max(1, mips[i - 1].width / 2) ||
             mip.height != std::max(1, mips[i - 1].height / 2))) {
            return nullptr;
        }
    }

    D3D11_TEXTURE2D_DESC description{};
    description.Width = static_cast<UINT>(mips[0].width);
    description.Height = static_cast<UINT>(mips[0].height);
    description.MipLevels = static_cast<UINT>(mips.size());
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    std::vector<D3D11_SUBRESOURCE_DATA> initial(mips.size());
    for (std::size_t i = 0; i < mips.size(); ++i) {
        initial[i].pSysMem = mips[i].pixels.data();
        initial[i].SysMemPitch = static_cast<UINT>(mips[i].width * 4);
        initial[i].SysMemSlicePitch = static_cast<UINT>(
            mips[i].width * mips[i].height * 4);
    }

    ID3D11Texture2D* texture = nullptr;
    if (FAILED(device->CreateTexture2D(
            &description, initial.data(), &texture)) || !texture) {
        return nullptr;
    }
    ID3D11ShaderResourceView* view = nullptr;
    if (FAILED(device->CreateShaderResourceView(texture, nullptr, &view))) {
        texture->Release();
        return nullptr;
    }
    texture->Release();
    return view;
}

bool CreateCloudSrv(ID3D11Device* device,
                    const std::vector<unsigned char>& blob,
                    ID3D11ShaderResourceView** out_view)
{
    *out_view = nullptr;
    TexInfo info{};
    if (!parse_tex_info(blob, info) || info.Mips.empty()) return false;

    std::vector<AuthoredRgbaMip> decoded;
    decoded.reserve(std::min<std::size_t>(info.Mips.size(), 15));
    for (std::size_t i = 0; i < info.Mips.size() && i < 64; ++i) {
        AuthoredRgbaMip mip;
        bool ignored_alpha = false;
        if (!decode_tex_to_rgba(blob, mip.pixels, mip.width, mip.height,
                                &ignored_alpha, static_cast<int>(i))) {
            continue;
        }
        const std::size_t expected =
            std::size_t(mip.width) * std::size_t(mip.height) * 4;
        if (mip.width <= 0 || mip.height <= 0 ||
            mip.pixels.size() < expected) {
            continue;
        }
        decoded.push_back(std::move(mip));
    }
    if (decoded.empty()) return false;

    auto area = [](const AuthoredRgbaMip& mip) {
        return std::uint64_t(mip.width) * std::uint64_t(mip.height);
    };
    const auto base = std::max_element(
        decoded.begin(), decoded.end(),
        [&](const AuthoredRgbaMip& lhs, const AuthoredRgbaMip& rhs) {
            return area(lhs) < area(rhs);
        });

    std::vector<AuthoredRgbaMip> chain;
    chain.push_back(std::move(*base));
    while (chain.size() < 15 &&
           (chain.back().width > 1 || chain.back().height > 1)) {
        const int wanted_width = std::max(1, chain.back().width / 2);
        const int wanted_height = std::max(1, chain.back().height / 2);
        auto next = std::find_if(
            decoded.begin(), decoded.end(),
            [&](const AuthoredRgbaMip& candidate) {
                return !candidate.pixels.empty() &&
                       candidate.width == wanted_width &&
                       candidate.height == wanted_height;
            });
        if (next == decoded.end()) break;
        chain.push_back(std::move(*next));
    }

    *out_view = CreateSrvFromAuthoredMips(device, chain);
    return *out_view != nullptr;
}

bool CreateOrdinarySrv(ID3D11Device* device,
                       const std::vector<unsigned char>& blob,
                       ID3D11ShaderResourceView** out_view,
                       int* out_width = nullptr,
                       int* out_height = nullptr)
{
    *out_view = nullptr;
    std::vector<std::uint8_t> rgba;
    int width = 0;
    int height = 0;
    bool has_alpha = false;
    if (!decode_tex_to_rgba(
            blob, rgba, width, height, &has_alpha)) {
        return false;
    }
    *out_view = create_srv_from_rgba(device, width, height, rgba);
    if (out_width) *out_width = width;
    if (out_height) *out_height = height;
    return *out_view != nullptr;
}
