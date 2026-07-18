void ReleaseD3D11Resources(ModelPreview& preview)
{
    ReleaseCloudTextures(preview);
    ReleaseSkyTextures(preview);
    if (preview.vs_sky_element) {
        preview.vs_sky_element->Release();
        preview.vs_sky_element = nullptr;
    }
    if (preview.ps_sky_element) {
        preview.ps_sky_element->Release();
        preview.ps_sky_element = nullptr;
    }
    if (preview.vs_sky_stars) {
        preview.vs_sky_stars->Release();
        preview.vs_sky_stars = nullptr;
    }
    if (preview.ps_sky_stars) {
        preview.ps_sky_stars->Release();
        preview.ps_sky_stars = nullptr;
    }
    if (preview.layout_sky_element) {
        preview.layout_sky_element->Release();
        preview.layout_sky_element = nullptr;
    }
    if (preview.cbuffer_sky_element) {
        preview.cbuffer_sky_element->Release();
        preview.cbuffer_sky_element = nullptr;
    }
    if (preview.sky_element_vb) {
        preview.sky_element_vb->Release();
        preview.sky_element_vb = nullptr;
    }
    if (preview.vs_sky_dome) {
        preview.vs_sky_dome->Release();
        preview.vs_sky_dome = nullptr;
    }
    if (preview.ps_sky_dome) {
        preview.ps_sky_dome->Release();
        preview.ps_sky_dome = nullptr;
    }
    if (preview.cbuffer_sky_dome) {
        preview.cbuffer_sky_dome->Release();
        preview.cbuffer_sky_dome = nullptr;
    }
    if (preview.sky_lut_srv) {
        preview.sky_lut_srv->Release();
        preview.sky_lut_srv = nullptr;
    }
    if (preview.sky_lut_tex) {
        preview.sky_lut_tex->Release();
        preview.sky_lut_tex = nullptr;
    }
    if (preview.sampler_sky_clamp) {
        preview.sampler_sky_clamp->Release();
        preview.sampler_sky_clamp = nullptr;
    }
    if (preview.vs_sky) {
        preview.vs_sky->Release();
        preview.vs_sky = nullptr;
    }
    if (preview.ps_sky) {
        preview.ps_sky->Release();
        preview.ps_sky = nullptr;
    }
    if (preview.cbuffer_sky) {
        preview.cbuffer_sky->Release();
        preview.cbuffer_sky = nullptr;
    }
    if (preview.vs_cloud) {
        preview.vs_cloud->Release();
        preview.vs_cloud = nullptr;
    }
    if (preview.ps_cloud) {
        preview.ps_cloud->Release();
        preview.ps_cloud = nullptr;
    }
    if (preview.layout_cloud) {
        preview.layout_cloud->Release();
        preview.layout_cloud = nullptr;
    }
    if (preview.cbuffer_cloud) {
        preview.cbuffer_cloud->Release();
        preview.cbuffer_cloud = nullptr;
    }
    if (preview.cloud_vb) {
        preview.cloud_vb->Release();
        preview.cloud_vb = nullptr;
    }
    if (preview.cloud_ib) {
        preview.cloud_ib->Release();
        preview.cloud_ib = nullptr;
    }
    if (preview.sampler_cloud) {
        preview.sampler_cloud->Release();
        preview.sampler_cloud = nullptr;
    }
}
