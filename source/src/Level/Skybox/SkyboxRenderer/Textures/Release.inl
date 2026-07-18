void ReleaseCloudTextures(ModelPreview& preview)
{
    for (int i = 0; i < 4; ++i) {
        if (preview.cloud_density_srv[i]) {
            preview.cloud_density_srv[i]->Release();
            preview.cloud_density_srv[i] = nullptr;
        }
        preview.cloud_density_tried[i] = false;
        preview.cloud_density_tex_name[i].clear();
    }
}

void ReleaseSkyTextures(ModelPreview& preview)
{
    if (preview.sky_overlay_srv) {
        preview.sky_overlay_srv->Release();
        preview.sky_overlay_srv = nullptr;
    }
    if (preview.sky_sun_disc_srv) {
        preview.sky_sun_disc_srv->Release();
        preview.sky_sun_disc_srv = nullptr;
    }
    if (preview.sky_moon_srv) {
        preview.sky_moon_srv->Release();
        preview.sky_moon_srv = nullptr;
    }
    if (preview.sky_moon_glare_srv) {
        preview.sky_moon_glare_srv->Release();
        preview.sky_moon_glare_srv = nullptr;
    }
    if (preview.sky_sun_beams_srv) {
        preview.sky_sun_beams_srv->Release();
        preview.sky_sun_beams_srv = nullptr;
    }
    if (preview.sky_sun_glare_srv) {
        preview.sky_sun_glare_srv->Release();
        preview.sky_sun_glare_srv = nullptr;
    }
    preview.sky_overlay_tried = false;
    preview.sky_sun_disc_tried = false;
    preview.sky_moon_tried = false;
    preview.sky_moon_glare_tried = false;
    preview.sky_sun_beams_tried = false;
    preview.sky_sun_glare_tried = false;
    preview.sky_overlay_tex_name.clear();
    preview.sky_sun_disc_tex_name.clear();
    preview.sky_moon_tex_name.clear();
    preview.sky_moon_glare_tex_name.clear();
    preview.sky_sun_beams_tex_name.clear();
    preview.sky_sun_glare_tex_name.clear();
}
