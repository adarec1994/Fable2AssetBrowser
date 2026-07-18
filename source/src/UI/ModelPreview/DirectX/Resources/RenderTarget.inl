static bool create_target(ID3D11Device* dev, ModelPreview& mp, int w, int h){
    if(mp.rtv){ mp.rtv->Release(); mp.rtv=nullptr; }
    if(mp.srv){ mp.srv->Release(); mp.srv=nullptr; }
    if(mp.color){ mp.color->Release(); mp.color=nullptr; }
    if(mp.dsv){ mp.dsv->Release(); mp.dsv=nullptr; }
    if(mp.depth){ mp.depth->Release(); mp.depth=nullptr; }
    mp.width = w; mp.height = h;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w; td.Height = h; td.MipLevels=1; td.ArraySize=1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count=1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if(FAILED(dev->CreateTexture2D(&td, nullptr, &mp.color))) return false;
    if(FAILED(dev->CreateRenderTargetView(mp.color, nullptr, &mp.rtv))) return false;
    if(FAILED(dev->CreateShaderResourceView(mp.color, nullptr, &mp.srv))) return false;
    D3D11_TEXTURE2D_DESC dd{};
    dd.Width=w; dd.Height=h; dd.MipLevels=1; dd.ArraySize=1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc.Count=1;
    dd.Usage = D3D11_USAGE_DEFAULT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if(FAILED(dev->CreateTexture2D(&dd,nullptr,&mp.depth))) return false;
    if(FAILED(dev->CreateDepthStencilView(mp.depth,nullptr,&mp.dsv))) return false;
    return true;
}
